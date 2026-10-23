/* MP4 sink writer for file post-processing, on FFmpeg's mp4 muxer.
   Wine's MP4 media sink is a stub (BeginWriting fails with E_NOTIMPL, "media_sink_SetPresentationClock stub!"), so the
   mp4 export of the core cannot work through mfreadwrite. The core hands the sink writer already encoded samples
   (H.264/HEVC in Annex B from NVENC, raw AAC from nvb_aac), which only need muxing. mfhooks.c in the nvcuda shim routes
   ".mp4" URLs here (nvb_mp4_create). */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>

void nvb_log(const char *fmt, ...);

#define MAX_STREAMS 4

typedef struct {
    IMFMediaType *target, *input;
    AVStream *st;
    BOOL video;
    enum AVCodecID codec;
} mp4_stream;

typedef struct {
    const IMFSinkWriterVtbl *lpVtbl;
    LONG ref;
    CRITICAL_SECTION cs;
    char path[1024];
    AVFormatContext *fc;
    mp4_stream s[MAX_STREAMS];
    int count;
    BOOL began, done;
    unsigned written[MAX_STREAMS];
} mp4_writer;

static mp4_writer *impl(IMFSinkWriter *i) { return (mp4_writer *)i; }

static void free_streams(mp4_writer *w)
{
    int i;
    for (i = 0; i < w->count; i++) {
        if (w->s[i].target) IMFMediaType_Release(w->s[i].target);
        if (w->s[i].input) IMFMediaType_Release(w->s[i].input);
    }
    w->count = 0;
}

static HRESULT finish(mp4_writer *w)
{
    int r = 0;
    if (w->began && !w->done) {
        r = av_write_trailer(w->fc);
        avio_closep(&w->fc->pb);
        nvb_log("mp4sink: trailer written (%d)", r);
    }
    w->done = TRUE;
    return r < 0 ? E_FAIL : S_OK;
}

static HRESULT WINAPI w_QueryInterface(IMFSinkWriter *i, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFSinkWriter)) { *out = i; IMFSinkWriter_AddRef(i); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI w_AddRef(IMFSinkWriter *i) { return InterlockedIncrement(&impl(i)->ref); }
static ULONG WINAPI w_Release(IMFSinkWriter *i)
{
    mp4_writer *w = impl(i);
    LONG r = InterlockedDecrement(&w->ref);
    if (!r) {
        finish(w);
        if (w->fc) avformat_free_context(w->fc);
        free_streams(w);
        DeleteCriticalSection(&w->cs);
        free(w);
    }
    return r;
}

static BOOL guid_is(IMFMediaType *t, const GUID *key, const GUID *want)
{
    GUID g = {0};
    return SUCCEEDED(IMFMediaType_GetGUID(t, key, &g)) && IsEqualGUID(&g, want);
}

static HRESULT WINAPI w_AddStream(IMFSinkWriter *i, IMFMediaType *target, DWORD *index)
{
    mp4_writer *w = impl(i);
    mp4_stream *s;
    HRESULT hr = S_OK;
    GUID sub = {0};
    if (!target || !index) return E_POINTER;
    EnterCriticalSection(&w->cs);
    if (w->began || w->count >= MAX_STREAMS) { LeaveCriticalSection(&w->cs); return MF_E_INVALIDREQUEST; }
    s = &w->s[w->count];
    memset(s, 0, sizeof(*s));
    IMFMediaType_GetGUID(target, &MF_MT_SUBTYPE, &sub);
    if (guid_is(target, &MF_MT_MAJOR_TYPE, &MFMediaType_Video)) {
        s->video = TRUE;
        if (IsEqualGUID(&sub, &MFVideoFormat_H264)) s->codec = AV_CODEC_ID_H264;
        else if (IsEqualGUID(&sub, &MFVideoFormat_HEVC)) s->codec = AV_CODEC_ID_HEVC;
    } else if (guid_is(target, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio)) {
        if (IsEqualGUID(&sub, &MFAudioFormat_AAC)) s->codec = AV_CODEC_ID_AAC;
    }
    if (s->codec == AV_CODEC_ID_NONE) {
        nvb_log("mp4sink: AddStream rejected subtype %08lx", sub.Data1);
        hr = MF_E_INVALIDMEDIATYPE;
    } else {
        s->st = avformat_new_stream(w->fc, NULL);
        if (!s->st) hr = E_OUTOFMEMORY;
        else {
            s->target = target; IMFMediaType_AddRef(target);
            *index = w->count++;
            nvb_log("mp4sink: AddStream %s -> index %lu", s->video ? "video" : "audio", *index);
        }
    }
    LeaveCriticalSection(&w->cs);
    return hr;
}

static HRESULT WINAPI w_SetInputMediaType(IMFSinkWriter *i, DWORD index, IMFMediaType *type, IMFAttributes *params)
{
    mp4_writer *w = impl(i);
    GUID a = {0}, b = {0};
    if (!type) return E_POINTER;
    if (index >= (DWORD)w->count) return MF_E_INVALIDSTREAMNUMBER;
    IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &a);
    IMFMediaType_GetGUID(w->s[index].target, &MF_MT_SUBTYPE, &b);
    if (!IsEqualGUID(&a, &b)) { nvb_log("mp4sink: input subtype %08lx differs from target %08lx (no transcoding)", a.Data1, b.Data1); return MF_E_INVALIDMEDIATYPE; }
    EnterCriticalSection(&w->cs);
    if (w->s[index].input) IMFMediaType_Release(w->s[index].input);
    w->s[index].input = type; IMFMediaType_AddRef(type);
    LeaveCriticalSection(&w->cs);
    return S_OK;
}

/* copy a blob attribute of the input type, else of the target type, into the codec extradata */
static BOOL get_blob(mp4_stream *s, const GUID *key, BYTE **out, UINT32 *len)
{
    IMFMediaType *t[2] = { s->input, s->target };
    int k;
    for (k = 0; k < 2; k++) {
        UINT32 n = 0;
        if (t[k] && SUCCEEDED(IMFMediaType_GetBlobSize(t[k], key, &n)) && n) {
            *out = malloc(n);
            if (*out && SUCCEEDED(IMFMediaType_GetBlob(t[k], key, *out, n, len))) return TRUE;
            free(*out); *out = NULL;
        }
    }
    return FALSE;
}

static HRESULT fill_codecpar(mp4_stream *s)
{
    AVCodecParameters *p = s->st->codecpar;
    IMFMediaType *t = s->input ? s->input : s->target;
    BYTE *blob = NULL;
    UINT32 len = 0, v = 0;
    p->codec_id = s->codec;
    if (s->video) {
        UINT64 fs = 0, fr = 0;
        p->codec_type = AVMEDIA_TYPE_VIDEO;
        if (SUCCEEDED(IMFMediaType_GetUINT64(t, &MF_MT_FRAME_SIZE, &fs))) { p->width = (int)(fs >> 32); p->height = (int)(UINT32)fs; }
        if (SUCCEEDED(IMFMediaType_GetUINT64(t, &MF_MT_FRAME_RATE, &fr)) && (UINT32)fr) {
            s->st->avg_frame_rate.num = (int)(fr >> 32); s->st->avg_frame_rate.den = (int)(UINT32)fr;
            s->st->r_frame_rate = s->st->avg_frame_rate;
        }
        if (SUCCEEDED(IMFMediaType_GetUINT32(t, &MF_MT_AVG_BITRATE, &v))) p->bit_rate = v;
        if (get_blob(s, &MF_MT_MPEG_SEQUENCE_HEADER, &blob, &len)) {
            p->extradata = av_mallocz(len + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!p->extradata) { free(blob); return E_OUTOFMEMORY; }
            memcpy(p->extradata, blob, len); p->extradata_size = (int)len;
            free(blob);
        }
        s->st->time_base.num = 1; s->st->time_base.den = 90000;
    } else {
        UINT32 ch = 2, rate = 48000;
        p->codec_type = AVMEDIA_TYPE_AUDIO;
        IMFMediaType_GetUINT32(t, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
        IMFMediaType_GetUINT32(t, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        p->sample_rate = (int)rate;
        av_channel_layout_default(&p->ch_layout, (int)ch);
        p->frame_size = 1024;
        if (SUCCEEDED(IMFMediaType_GetUINT32(t, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &v))) p->bit_rate = (int64_t)v * 8;
        /* MF_MT_USER_DATA = HEAACWAVEINFO tail (12 bytes) + AudioSpecificConfig */
        if (get_blob(s, &MF_MT_USER_DATA, &blob, &len) && len > 12) {
            p->extradata = av_mallocz(len - 12 + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!p->extradata) { free(blob); return E_OUTOFMEMORY; }
            memcpy(p->extradata, blob + 12, len - 12); p->extradata_size = (int)(len - 12);
        }
        free(blob);
        s->st->time_base.num = 1; s->st->time_base.den = (int)rate;
    }
    return S_OK;
}

static HRESULT WINAPI w_BeginWriting(IMFSinkWriter *i)
{
    mp4_writer *w = impl(i);
    HRESULT hr = S_OK;
    int k, r;
    EnterCriticalSection(&w->cs);
    if (w->began) goto out;
    if (!w->count) { hr = MF_E_INVALIDREQUEST; goto out; }
    for (k = 0; k < w->count && SUCCEEDED(hr); k++) hr = fill_codecpar(&w->s[k]);
    if (FAILED(hr)) goto out;
    r = avio_open(&w->fc->pb, w->path, AVIO_FLAG_WRITE);
    if (r < 0) { nvb_log("mp4sink: avio_open(%s) = %d", w->path, r); hr = E_FAIL; goto out; }
    r = avformat_write_header(w->fc, NULL);
    if (r < 0) { nvb_log("mp4sink: avformat_write_header = %d", r); avio_closep(&w->fc->pb); hr = E_FAIL; goto out; }
    w->began = TRUE;
    nvb_log("mp4sink: BeginWriting ok, %d streams", w->count);
out:
    LeaveCriticalSection(&w->cs);
    return hr;
}

/* true if the Annex-B access unit contains an IDR slice */
static BOOL is_keyframe(enum AVCodecID codec, const BYTE *d, DWORD n)
{
    DWORD i;
    for (i = 0; i + 4 < n; i++) {
        if (d[i] == 0 && d[i + 1] == 0 && (d[i + 2] == 1 || (d[i + 2] == 0 && d[i + 3] == 1))) {
            const BYTE *h = d + i + (d[i + 2] == 1 ? 3 : 4);
            int t;
            if (h >= d + n) break;
            if (codec == AV_CODEC_ID_H264) { t = h[0] & 0x1f; if (t == 5) return TRUE; }
            else { t = (h[0] >> 1) & 0x3f; if (t >= 16 && t <= 21) return TRUE; }
        }
    }
    return FALSE;
}

static HRESULT WINAPI w_WriteSample(IMFSinkWriter *i, DWORD index, IMFSample *sample)
{
    mp4_writer *w = impl(i);
    IMFMediaBuffer *buf = NULL;
    BYTE *p = NULL;
    DWORD len = 0;
    LONGLONG t = 0, dur = 0, dts = 0;
    UINT32 clean = 0;
    AVPacket *pkt;
    HRESULT hr;
    mp4_stream *s;
    if (!sample) return E_POINTER;
    if (index >= (DWORD)w->count) return MF_E_INVALIDSTREAMNUMBER;
    if (!w->began) return MF_E_INVALIDREQUEST;
    s = &w->s[index];
    IMFSample_GetSampleTime(sample, &t);
    IMFSample_GetSampleDuration(sample, &dur);
    if (FAILED(IMFSample_GetUINT64(sample, &MFSampleExtension_DecodeTimestamp, (UINT64 *)&dts))) dts = t;
    IMFSample_GetUINT32(sample, &MFSampleExtension_CleanPoint, &clean);
    hr = IMFSample_ConvertToContiguousBuffer(sample, &buf);
    if (FAILED(hr)) return hr;
    hr = IMFMediaBuffer_Lock(buf, &p, NULL, &len);
    if (SUCCEEDED(hr)) {
        pkt = av_packet_alloc();
        if (!pkt || av_new_packet(pkt, (int)len) < 0) hr = E_OUTOFMEMORY;
        else {
            AVRational src = { 1, 10000000 };
            memcpy(pkt->data, p, len);
            pkt->stream_index = s->st->index;
            pkt->pts = t; pkt->dts = dts; pkt->duration = dur;
            if (!s->video || clean || is_keyframe(s->codec, p, len)) pkt->flags |= AV_PKT_FLAG_KEY;
            av_packet_rescale_ts(pkt, src, s->st->time_base);
            EnterCriticalSection(&w->cs);
            if (av_interleaved_write_frame(w->fc, pkt) < 0) hr = E_FAIL;
            if ((w->written[index]++ % 500) == 0) nvb_log("mp4sink: stream %lu sample #%u t=%lld ms size=%lu key=%d", index, w->written[index] - 1, t / 10000, len, !!(pkt->flags & AV_PKT_FLAG_KEY));
            LeaveCriticalSection(&w->cs);
        }
        av_packet_free(&pkt);
        IMFMediaBuffer_Unlock(buf);
    }
    IMFMediaBuffer_Release(buf);
    if (FAILED(hr)) nvb_log("mp4sink: WriteSample(%lu) hr=%08lx", index, hr);
    return hr;
}

static HRESULT WINAPI w_SendStreamTick(IMFSinkWriter *i, DWORD index, LONGLONG ts) { return S_OK; }
static HRESULT WINAPI w_PlaceMarker(IMFSinkWriter *i, DWORD index, void *ctx) { return S_OK; }
static HRESULT WINAPI w_NotifyEndOfSegment(IMFSinkWriter *i, DWORD index) { return S_OK; }
static HRESULT WINAPI w_Flush(IMFSinkWriter *i, DWORD index) { return S_OK; }

static HRESULT WINAPI w_Finalize(IMFSinkWriter *i)
{
    mp4_writer *w = impl(i);
    HRESULT hr;
    EnterCriticalSection(&w->cs);
    hr = w->began ? finish(w) : MF_E_INVALIDREQUEST;
    LeaveCriticalSection(&w->cs);
    return hr;
}

static HRESULT WINAPI w_GetServiceForStream(IMFSinkWriter *i, DWORD index, REFGUID svc, REFIID riid, void **out)
{
    if (out) *out = NULL;
    return MF_E_UNSUPPORTED_SERVICE;
}
static HRESULT WINAPI w_GetStatistics(IMFSinkWriter *i, DWORD index, MF_SINK_WRITER_STATISTICS *st)
{
    if (!st) return E_POINTER;
    memset((char *)st + sizeof(DWORD), 0, st->cb - sizeof(DWORD));
    return S_OK;
}

static const IMFSinkWriterVtbl mp4_vtbl = {
    w_QueryInterface, w_AddRef, w_Release, w_AddStream, w_SetInputMediaType, w_BeginWriting, w_WriteSample,
    w_SendStreamTick, w_PlaceMarker, w_NotifyEndOfSegment, w_Flush, w_Finalize, w_GetServiceForStream, w_GetStatistics,
};

__declspec(dllexport) HRESULT nvb_mp4_create(LPCWSTR url, IMFSinkWriter **out)
{
    mp4_writer *w;
    if (!url || !out) return E_POINTER;
    w = calloc(1, sizeof(*w));
    if (!w) return E_OUTOFMEMORY;
    if (!WideCharToMultiByte(CP_UTF8, 0, url, -1, w->path, sizeof(w->path), NULL, NULL)) { free(w); return E_INVALIDARG; }
    if (avformat_alloc_output_context2(&w->fc, NULL, "mp4", w->path) < 0 || !w->fc) { free(w); return E_FAIL; }
    w->lpVtbl = &mp4_vtbl;
    w->ref = 1;
    InitializeCriticalSection(&w->cs);
    *out = (IMFSinkWriter *)w;
    nvb_log("mp4sink: created for %s", w->path);
    return S_OK;
}
