/* nvb_media.dll, part 1: AAC encoder MFT for Wine, built on FFmpeg's native AAC-LC encoder (static, LGPL).
   (part 2, the mp4 sink writer, is mp4sink.c)
   Wine ships no AAC encoder, so the core's file post-processing stops with "No AAC encoder MFT found" (0xc00d5212)
   when it writes an mp4. nvb_aac_register() adds this transform to the process's local MFT list; the core's
   MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, output = AAC) then finds it.

   Behaves like the Windows encoder: input 16-bit PCM, output raw AAC access units of 1024 samples; the output type
   carries MF_MT_USER_DATA = HEAACWAVEINFO (12 bytes) + AudioSpecificConfig, which the mp4 sink needs for the esds box. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>

/* the nvcuda shim hands us its logger so that everything ends up in one log file */
static void (*shim_log)(const char *fmt, ...);
__declspec(dllexport) void nvb_media_set_logger(void (*fn)(const char *fmt, ...)) { shim_log = fn; }
void nvb_log(const char *fmt, ...)
{
    char b[400];
    va_list ap;
    if (!shim_log) return;
    va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    shim_log("%s", b);
}

/* time of the last ProcessInput/ProcessOutput; the shim delays the source reader's end of stream until this is idle */
static volatile LONGLONG last_activity;
static void touch(void) { last_activity = (LONGLONG)GetTickCount64(); }
__declspec(dllexport) unsigned long long nvb_aac_idle_ms(void)
{
    LONGLONG t = last_activity;
    return t ? (unsigned long long)((LONGLONG)GetTickCount64() - t) : ~0ULL;
}

#define FRAME_SAMPLES 1024
#define MAX_PACKET    8192
#define HUNDRED_NS    10000000LL

static const UINT32 rates[] = { 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 64000, 88200, 96000 };
static const UINT32 bytes_per_sec[] = { 16000, 20000, 24000, 12000 };   /* the values the Windows encoder offers */

typedef struct pkt_node { struct pkt_node *next; int size; LONGLONG time; BYTE data[MAX_PACKET]; } pkt_node;

typedef struct {
    const IMFTransformVtbl *lpVtbl;
    LONG ref;
    CRITICAL_SECTION cs;
    IMFMediaType *in_type, *out_type;
    UINT32 rate, channels, bitrate;
    AVCodecContext *ctx;
    AVFrame *frame;
    AVPacket *pkt;
    short *pcm;                     /* interleaved samples not yet encoded */
    size_t pcm_frames, pcm_cap;     /* in sample frames (all channels) */
    LONGLONG base_time;             /* time of the first sample */
    BOOL have_base;
    LONGLONG frames_sent, packets_out;
    BOOL drained;
    pkt_node *head, *tail;
    unsigned n_in, n_out;
} aac_mft;

static int rate_index(UINT32 r)
{
    static const UINT32 t[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000 };
    int i;
    for (i = 0; i < 12; i++) if (t[i] == r) return i;
    return -1;
}

static BOOL rate_ok(UINT32 r)
{
    size_t i;
    for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) if (rates[i] == r) return TRUE;
    return FALSE;
}

/* AudioSpecificConfig for AAC-LC */
static void make_asc(UINT32 rate, UINT32 ch, BYTE out[2])
{
    int idx = rate_index(rate);
    out[0] = (BYTE)((2 << 3) | (idx >> 1));
    out[1] = (BYTE)(((idx & 1) << 7) | (ch << 3));
}

static HRESULT make_pcm_type(UINT32 rate, UINT32 ch, IMFMediaType **out)
{
    IMFMediaType *t;
    HRESULT hr = MFCreateMediaType(&t);
    if (FAILED(hr)) return hr;
    IMFMediaType_SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(t, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_NUM_CHANNELS, ch);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_BLOCK_ALIGNMENT, ch * 2);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * ch * 2);
    IMFMediaType_SetUINT32(t, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
    *out = t;
    return S_OK;
}

static HRESULT make_aac_type(UINT32 rate, UINT32 ch, UINT32 avg, IMFMediaType **out)
{
    IMFMediaType *t;
    BYTE ud[14] = { 0 };            /* HEAACWAVEINFO tail: payload 0 (raw), profile level 0x29, struct type 0 ... */
    HRESULT hr = MFCreateMediaType(&t);
    if (FAILED(hr)) return hr;
    ud[2] = 0x29;
    make_asc(rate, ch, ud + 12);
    IMFMediaType_SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(t, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_NUM_CHANNELS, ch);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, avg);
    IMFMediaType_SetUINT32(t, &MF_MT_AAC_PAYLOAD_TYPE, 0);
    IMFMediaType_SetUINT32(t, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    IMFMediaType_SetBlob(t, &MF_MT_USER_DATA, ud, sizeof(ud));
    *out = t;
    return S_OK;
}

static void codec_close(aac_mft *m)
{
    if (m->ctx) avcodec_free_context(&m->ctx);
    if (m->frame) av_frame_free(&m->frame);
    if (m->pkt) av_packet_free(&m->pkt);
    m->drained = FALSE;
}

static void queue_clear(aac_mft *m)
{
    while (m->head) { pkt_node *n = m->head; m->head = n->next; free(n); }
    m->tail = NULL;
}

static HRESULT codec_open(aac_mft *m)
{
    const AVCodec *c;
    if (m->ctx) return S_OK;
    c = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!c) return MF_E_TRANSFORM_TYPE_NOT_SET;
    m->ctx = avcodec_alloc_context3(c);
    if (!m->ctx) return E_OUTOFMEMORY;
    m->ctx->sample_rate = m->rate;
    av_channel_layout_default(&m->ctx->ch_layout, m->channels);
    m->ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m->ctx->bit_rate = m->bitrate;
    m->ctx->profile = AV_PROFILE_AAC_LOW;
    m->ctx->time_base.num = 1; m->ctx->time_base.den = m->rate;
    m->ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(m->ctx, c, NULL) < 0) { codec_close(m); return MF_E_TRANSFORM_TYPE_NOT_SET; }
    m->frame = av_frame_alloc();
    m->pkt = av_packet_alloc();
    if (!m->frame || !m->pkt) { codec_close(m); return E_OUTOFMEMORY; }
    m->frame->nb_samples = FRAME_SAMPLES;
    m->frame->format = AV_SAMPLE_FMT_FLTP;
    m->frame->sample_rate = m->rate;
    av_channel_layout_copy(&m->frame->ch_layout, &m->ctx->ch_layout);
    if (av_frame_get_buffer(m->frame, 0) < 0) { codec_close(m); return E_OUTOFMEMORY; }
    m->frames_sent = m->packets_out = 0;
    return S_OK;
}

static void collect_packets(aac_mft *m)
{
    while (avcodec_receive_packet(m->ctx, m->pkt) >= 0) {
        if (m->pkt->size > 0 && m->pkt->size <= MAX_PACKET) {
            pkt_node *n = malloc(sizeof(*n));
            if (n) {
                n->next = NULL;
                n->size = m->pkt->size;
                memcpy(n->data, m->pkt->data, m->pkt->size);
                n->time = m->base_time + m->packets_out * FRAME_SAMPLES * HUNDRED_NS / m->rate;
                if (m->tail) m->tail->next = n; else m->head = n;
                m->tail = n;
                m->packets_out++;
            }
        }
        av_packet_unref(m->pkt);
    }
}

/* encode one 1024-sample frame from the head of the PCM buffer; `count` sample frames are valid, the rest is silence */
static HRESULT encode_frame(aac_mft *m, size_t count)
{
    UINT32 ch = m->channels, c;
    size_t i;
    if (av_frame_make_writable(m->frame) < 0) return E_OUTOFMEMORY;
    for (c = 0; c < ch; c++) {
        float *dst = (float *)m->frame->extended_data[c];
        for (i = 0; i < FRAME_SAMPLES; i++) dst[i] = i < count ? m->pcm[i * ch + c] / 32768.0f : 0.0f;
    }
    m->frame->pts = m->frames_sent * FRAME_SAMPLES;
    m->frames_sent++;
    if (avcodec_send_frame(m->ctx, m->frame) < 0) return E_FAIL;
    collect_packets(m);
    return S_OK;
}

static void pcm_consume(aac_mft *m, size_t frames)
{
    memmove(m->pcm, m->pcm + frames * m->channels, (m->pcm_frames - frames) * m->channels * sizeof(short));
    m->pcm_frames -= frames;
}

/* flush: pad the partial frame, then drain the encoder */
static void drain(aac_mft *m)
{
    if (!m->ctx || m->drained) return;
    if (m->pcm_frames) { encode_frame(m, m->pcm_frames); m->pcm_frames = 0; }
    avcodec_send_frame(m->ctx, NULL);
    collect_packets(m);
    m->drained = TRUE;
}

void nvb_log(const char *fmt, ...);
static aac_mft *impl(IMFTransform *i) { return (aac_mft *)i; }

static HRESULT WINAPI t_QueryInterface(IMFTransform *i, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFTransform)) { *out = i; IMFTransform_AddRef(i); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI t_AddRef(IMFTransform *i) { return InterlockedIncrement(&impl(i)->ref); }
static ULONG WINAPI t_Release(IMFTransform *i)
{
    aac_mft *m = impl(i);
    LONG r = InterlockedDecrement(&m->ref);
    if (!r) {
        codec_close(m);
        queue_clear(m);
        if (m->in_type) IMFMediaType_Release(m->in_type);
        if (m->out_type) IMFMediaType_Release(m->out_type);
        free(m->pcm);
        DeleteCriticalSection(&m->cs);
        free(m);
    }
    return r;
}

static HRESULT WINAPI t_GetStreamLimits(IMFTransform *i, DWORD *imin, DWORD *imax, DWORD *omin, DWORD *omax)
{
    *imin = *imax = *omin = *omax = 1;
    return S_OK;
}
static HRESULT WINAPI t_GetStreamCount(IMFTransform *i, DWORD *in, DWORD *out) { *in = *out = 1; return S_OK; }
static HRESULT WINAPI t_GetStreamIDs(IMFTransform *i, DWORD is, DWORD *ids, DWORD os, DWORD *oids) { return E_NOTIMPL; }

static HRESULT WINAPI t_GetInputStreamInfo(IMFTransform *i, DWORD id, MFT_INPUT_STREAM_INFO *info)
{
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    memset(info, 0, sizeof(*info));
    info->dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES | MFT_INPUT_STREAM_DOES_NOT_ADDREF;
    return S_OK;
}
static HRESULT WINAPI t_GetOutputStreamInfo(IMFTransform *i, DWORD id, MFT_OUTPUT_STREAM_INFO *info)
{
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    memset(info, 0, sizeof(*info));
    info->dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES | MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;
    info->cbSize = MAX_PACKET;
    return S_OK;
}

static HRESULT WINAPI t_GetAttributes(IMFTransform *i, IMFAttributes **a) { return E_NOTIMPL; }
static HRESULT WINAPI t_GetInputStreamAttributes(IMFTransform *i, DWORD id, IMFAttributes **a) { return E_NOTIMPL; }
static HRESULT WINAPI t_GetOutputStreamAttributes(IMFTransform *i, DWORD id, IMFAttributes **a) { return E_NOTIMPL; }
static HRESULT WINAPI t_DeleteInputStream(IMFTransform *i, DWORD id) { return E_NOTIMPL; }
static HRESULT WINAPI t_AddInputStreams(IMFTransform *i, DWORD c, DWORD *ids) { return E_NOTIMPL; }

static HRESULT WINAPI t_GetInputAvailableType(IMFTransform *i, DWORD id, DWORD index, IMFMediaType **type)
{
    aac_mft *m = impl(i);
    UINT32 rate = m->out_type ? m->rate : 0, ch = m->out_type ? m->channels : 0;
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (rate) { if (index >= 1) return MF_E_NO_MORE_TYPES; return make_pcm_type(rate, ch, type); }
    if (index >= 4) return MF_E_NO_MORE_TYPES;
    return make_pcm_type(index < 2 ? 48000 : 44100, (index & 1) ? 1 : 2, type);
}

static HRESULT WINAPI t_GetOutputAvailableType(IMFTransform *i, DWORD id, DWORD index, IMFMediaType **type)
{
    aac_mft *m = impl(i);
    UINT32 rate = m->in_type ? m->rate : 48000, ch = m->in_type ? m->channels : 2;
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (index >= sizeof(bytes_per_sec) / sizeof(bytes_per_sec[0])) return MF_E_NO_MORE_TYPES;
    return make_aac_type(rate, ch, bytes_per_sec[index], type);
}

static HRESULT WINAPI t_SetInputType(IMFTransform *i, DWORD id, IMFMediaType *type, DWORD flags)
{
    aac_mft *m = impl(i);
    GUID major = {0}, sub = {0};
    UINT32 ch = 0, rate = 0, bits = 0;
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (!type) {
        EnterCriticalSection(&m->cs);
        if (m->in_type) IMFMediaType_Release(m->in_type);
        m->in_type = NULL; codec_close(m);
        LeaveCriticalSection(&m->cs);
        return S_OK;
    }
    IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
    IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &sub);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    if (!IsEqualGUID(&major, &MFMediaType_Audio) || !IsEqualGUID(&sub, &MFAudioFormat_PCM) || bits != 16
        || (ch != 1 && ch != 2) || !rate_ok(rate)) return MF_E_INVALIDMEDIATYPE;
    EnterCriticalSection(&m->cs);
    if (m->out_type && (rate != m->rate || ch != m->channels)) { LeaveCriticalSection(&m->cs); return MF_E_INVALIDMEDIATYPE; }
    if (!(flags & MFT_SET_TYPE_TEST_ONLY)) {
        if (m->in_type) IMFMediaType_Release(m->in_type);
        m->in_type = type; IMFMediaType_AddRef(type);
        m->rate = rate; m->channels = ch;
        codec_close(m);
    }
    LeaveCriticalSection(&m->cs);
    return S_OK;
}

static HRESULT WINAPI t_SetOutputType(IMFTransform *i, DWORD id, IMFMediaType *type, DWORD flags)
{
    aac_mft *m = impl(i);
    GUID major = {0}, sub = {0};
    UINT32 ch = 0, rate = 0, avg = 0, payload = 0;
    IMFMediaType *full = NULL;
    HRESULT hr;
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (!type) {
        EnterCriticalSection(&m->cs);
        if (m->out_type) IMFMediaType_Release(m->out_type);
        m->out_type = NULL; codec_close(m);
        LeaveCriticalSection(&m->cs);
        return S_OK;
    }
    IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
    IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &sub);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg);
    IMFMediaType_GetUINT32(type, &MF_MT_AAC_PAYLOAD_TYPE, &payload);
    if (!IsEqualGUID(&major, &MFMediaType_Audio) || !IsEqualGUID(&sub, &MFAudioFormat_AAC) || payload != 0) return MF_E_INVALIDMEDIATYPE;
    if (!ch) ch = m->in_type ? m->channels : 2;
    if (!rate) rate = m->in_type ? m->rate : 48000;
    if ((ch != 1 && ch != 2) || !rate_ok(rate)) return MF_E_INVALIDMEDIATYPE;
    if (!avg) avg = 16000;
    if (avg < 8000 || avg > 40000) return MF_E_INVALIDMEDIATYPE;
    EnterCriticalSection(&m->cs);
    if (m->in_type && (rate != m->rate || ch != m->channels)) { LeaveCriticalSection(&m->cs); return MF_E_INVALIDMEDIATYPE; }
    hr = S_OK;
    if (!(flags & MFT_SET_TYPE_TEST_ONLY)) {
        hr = make_aac_type(rate, ch, avg, &full);
        if (SUCCEEDED(hr)) {
            if (m->out_type) IMFMediaType_Release(m->out_type);
            m->out_type = full;
            m->rate = rate; m->channels = ch; m->bitrate = avg * 8;
            codec_close(m);
        }
    }
    LeaveCriticalSection(&m->cs);
    return hr;
}

static HRESULT WINAPI t_GetInputCurrentType(IMFTransform *i, DWORD id, IMFMediaType **type)
{
    aac_mft *m = impl(i);
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (!m->in_type) return MF_E_TRANSFORM_TYPE_NOT_SET;
    *type = m->in_type; IMFMediaType_AddRef(*type); return S_OK;
}
static HRESULT WINAPI t_GetOutputCurrentType(IMFTransform *i, DWORD id, IMFMediaType **type)
{
    aac_mft *m = impl(i);
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (!m->out_type) return MF_E_TRANSFORM_TYPE_NOT_SET;
    *type = m->out_type; IMFMediaType_AddRef(*type); return S_OK;
}

static HRESULT WINAPI t_GetInputStatus(IMFTransform *i, DWORD id, DWORD *flags)
{
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    *flags = MFT_INPUT_STATUS_ACCEPT_DATA;
    return S_OK;
}
static HRESULT WINAPI t_GetOutputStatus(IMFTransform *i, DWORD *flags)
{
    aac_mft *m = impl(i);
    *flags = m->head ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
    return S_OK;
}
static HRESULT WINAPI t_SetOutputBounds(IMFTransform *i, LONGLONG lo, LONGLONG hi) { return E_NOTIMPL; }
static HRESULT WINAPI t_ProcessEvent(IMFTransform *i, DWORD id, IMFMediaEvent *ev) { return E_NOTIMPL; }

static HRESULT WINAPI t_ProcessMessage(IMFTransform *i, MFT_MESSAGE_TYPE msg, ULONG_PTR param)
{
    aac_mft *m = impl(i);
    EnterCriticalSection(&m->cs);
    switch (msg) {
    case MFT_MESSAGE_COMMAND_FLUSH:
        codec_close(m);
        queue_clear(m);
        m->pcm_frames = 0; m->have_base = FALSE;
        break;
    case MFT_MESSAGE_COMMAND_DRAIN:
    case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
        nvb_log("aac: drain/eos message %d (in=%u out=%u)", (int)msg, m->n_in, m->n_out);
        drain(m);
        break;
    case MFT_MESSAGE_SET_D3D_MANAGER:
        LeaveCriticalSection(&m->cs);
        return E_NOTIMPL;
    default: break;
    }
    LeaveCriticalSection(&m->cs);
    return S_OK;
}

static HRESULT WINAPI t_ProcessInput(IMFTransform *i, DWORD id, IMFSample *sample, DWORD flags)
{
    aac_mft *m = impl(i);
    IMFMediaBuffer *buf = NULL;
    BYTE *p = NULL;
    DWORD len = 0;
    LONGLONG t = 0;
    HRESULT hr;
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (!sample) return E_POINTER;
    if (!m->in_type || !m->out_type) return MF_E_TRANSFORM_TYPE_NOT_SET;
    touch();
    EnterCriticalSection(&m->cs);
    hr = codec_open(m);
    if (FAILED(hr)) goto done;
    if (m->drained) { avcodec_flush_buffers(m->ctx); m->drained = FALSE; }
    hr = IMFSample_ConvertToContiguousBuffer(sample, &buf);
    if (FAILED(hr)) goto done;
    hr = IMFMediaBuffer_Lock(buf, &p, NULL, &len);
    if (SUCCEEDED(hr)) {
        size_t add = len / (m->channels * 2);
        if (!m->have_base) {
            m->have_base = SUCCEEDED(IMFSample_GetSampleTime(sample, &t));
            m->base_time = m->have_base ? t : 0;
            m->have_base = TRUE;
        }
        if (m->pcm_frames + add > m->pcm_cap) {
            size_t cap = (m->pcm_frames + add) * 2;
            short *np = realloc(m->pcm, cap * m->channels * sizeof(short));
            if (!np) hr = E_OUTOFMEMORY; else { m->pcm = np; m->pcm_cap = cap; }
        }
        if (SUCCEEDED(hr)) {
            memcpy(m->pcm + m->pcm_frames * m->channels, p, add * m->channels * 2);
            m->pcm_frames += add;
            while (SUCCEEDED(hr) && m->pcm_frames >= FRAME_SAMPLES) {
                hr = encode_frame(m, FRAME_SAMPLES);
                pcm_consume(m, FRAME_SAMPLES);
            }
        }
        IMFMediaBuffer_Unlock(buf);
    }
    IMFMediaBuffer_Release(buf);
done:
    if ((m->n_in++ % 1000) == 0) nvb_log("aac: input #%u hr=%08lx pcm_frames=%zu queued=%d", m->n_in - 1, hr, m->pcm_frames, m->head != NULL);
    LeaveCriticalSection(&m->cs);
    return hr;
}

static HRESULT WINAPI t_ProcessOutput(IMFTransform *i, DWORD flags, DWORD count, MFT_OUTPUT_DATA_BUFFER *out, DWORD *status)
{
    aac_mft *m = impl(i);
    pkt_node *n;
    IMFMediaBuffer *buf = NULL;
    BYTE *p = NULL;
    DWORD max = 0;
    HRESULT hr;
    if (count != 1 || !out) return E_INVALIDARG;
    if (!out->pSample) return E_INVALIDARG;
    if (!m->in_type || !m->out_type) return MF_E_TRANSFORM_TYPE_NOT_SET;
    EnterCriticalSection(&m->cs);
    n = m->head;
    if (!n) { LeaveCriticalSection(&m->cs); return MF_E_TRANSFORM_NEED_MORE_INPUT; }
    hr = IMFSample_GetBufferByIndex(out->pSample, 0, &buf);
    if (SUCCEEDED(hr)) hr = IMFMediaBuffer_Lock(buf, &p, &max, NULL);
    if (SUCCEEDED(hr)) {
        if (max < (DWORD)n->size) hr = MF_E_BUFFERTOOSMALL;
        else {
            memcpy(p, n->data, n->size);
            IMFMediaBuffer_Unlock(buf);
            IMFMediaBuffer_SetCurrentLength(buf, n->size);
            IMFSample_SetSampleTime(out->pSample, n->time);
            IMFSample_SetSampleDuration(out->pSample, FRAME_SAMPLES * HUNDRED_NS / m->rate);
            IMFSample_SetUINT32(out->pSample, &MFSampleExtension_CleanPoint, 1);
            m->head = n->next; if (!m->head) m->tail = NULL;
            free(n);
            touch();
            if ((m->n_out++ % 1000) == 0) nvb_log("aac: output #%u t=%lld ms", m->n_out - 1, out->pSample ? 0LL : 0LL);
            out->dwStatus = 0;
            out->pEvents = NULL;
            if (status) *status = 0;
            IMFMediaBuffer_Release(buf);
            LeaveCriticalSection(&m->cs);
            return S_OK;
        }
        IMFMediaBuffer_Unlock(buf);
    }
    if (buf) IMFMediaBuffer_Release(buf);
    LeaveCriticalSection(&m->cs);
    return hr;
}

static const IMFTransformVtbl mft_vtbl = {
    t_QueryInterface, t_AddRef, t_Release, t_GetStreamLimits, t_GetStreamCount, t_GetStreamIDs, t_GetInputStreamInfo,
    t_GetOutputStreamInfo, t_GetAttributes, t_GetInputStreamAttributes, t_GetOutputStreamAttributes, t_DeleteInputStream,
    t_AddInputStreams, t_GetInputAvailableType, t_GetOutputAvailableType, t_SetInputType, t_SetOutputType,
    t_GetInputCurrentType, t_GetOutputCurrentType, t_GetInputStatus, t_GetOutputStatus, t_SetOutputBounds,
    t_ProcessEvent, t_ProcessMessage, t_ProcessInput, t_ProcessOutput,
};

/* ---- class factory + registration ---- */
static HRESULT WINAPI f_QueryInterface(IClassFactory *i, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) { *out = i; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI f_AddRef(IClassFactory *i) { return 2; }
static ULONG WINAPI f_Release(IClassFactory *i) { return 1; }
static HRESULT WINAPI f_CreateInstance(IClassFactory *i, IUnknown *outer, REFIID riid, void **out)
{
    aac_mft *m;
    HRESULT hr;
    if (outer) return CLASS_E_NOAGGREGATION;
    m = calloc(1, sizeof(*m));
    if (!m) return E_OUTOFMEMORY;
    m->lpVtbl = &mft_vtbl;
    m->ref = 1;
    InitializeCriticalSection(&m->cs);
    hr = IMFTransform_QueryInterface((IMFTransform *)m, riid, out);
    IMFTransform_Release((IMFTransform *)m);
    return hr;
}
static HRESULT WINAPI f_LockServer(IClassFactory *i, BOOL lock) { return S_OK; }

static const IClassFactoryVtbl factory_vtbl = { f_QueryInterface, f_AddRef, f_Release, f_CreateInstance, f_LockServer };
static IClassFactory factory = { &factory_vtbl };

__declspec(dllexport) HRESULT nvb_aac_register(void)
{
    MFT_REGISTER_TYPE_INFO in = { 0 }, out = { 0 };
    in.guidMajorType = MFMediaType_Audio;  in.guidSubtype = MFAudioFormat_PCM;
    out.guidMajorType = MFMediaType_Audio; out.guidSubtype = MFAudioFormat_AAC;
    return MFTRegisterLocal(&factory, &MFT_CATEGORY_AUDIO_ENCODER, L"NVB AAC Encoder", MFT_ENUM_FLAG_SYNCMFT, 1, &in, 1, &out);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { return TRUE; }
