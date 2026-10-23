/* Media Foundation fixes for file post-processing, applied by redirecting NvBroadcastCore.dll's imports from
   mfreadwrite.dll (the core is signature-checked, so it cannot be patched on disk).

   1. WAV sink writer.
   Wine's mfreadwrite creates the writer for a ".wav" URL through MFCreateWAVEMediaSink(bytestream, NULL), which Wine's
   mfsrcsnk.dll rejects with E_POINTER (0x80004003), so the core's MFCreateSinkWriterFromURL fails and the whole job
   dies with errorStatus -9111. ".wav" URLs get the minimal IMFSinkWriter below, everything else goes to the real
   function. The writer stores the samples exactly as they arrive (the header is taken from the input media type). */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void logf_(const char *fmt, ...);

typedef HRESULT (WINAPI *create_from_url_t)(LPCWSTR, IMFByteStream *, IMFAttributes *, IMFSinkWriter **);
static create_from_url_t real_create_from_url;
typedef HRESULT (*mp4_create_t)(LPCWSTR, IMFSinkWriter **);
static mp4_create_t mp4_create;     /* from nvb_media.dll, see load_media_dll() */

typedef struct {
    const IMFSinkWriterVtbl *lpVtbl;
    LONG ref;
    CRITICAL_SECTION cs;
    HANDLE file;
    WCHAR path[MAX_PATH];
    BOOL have_target, have_input, writing;
    UINT32 tag, channels, rate, bits, align, bytes_per_sec;     /* header fields, from the input type */
    UINT64 data_bytes;
} wav_writer;

static void put16(BYTE *p, unsigned v) { p[0] = v & 255; p[1] = v >> 8; }
static void put32(BYTE *p, unsigned v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

static BOOL write_header(wav_writer *w)
{
    BYTE h[44];
    DWORD n;
    memcpy(h, "RIFF", 4);          put32(h + 4, (unsigned)(w->data_bytes + 36));
    memcpy(h + 8, "WAVEfmt ", 8);  put32(h + 16, 16);
    put16(h + 20, w->tag);         put16(h + 22, w->channels);
    put32(h + 24, w->rate);        put32(h + 28, w->bytes_per_sec);
    put16(h + 32, w->align);       put16(h + 34, w->bits);
    memcpy(h + 36, "data", 4);    put32(h + 40, (unsigned)w->data_bytes);
    SetFilePointer(w->file, 0, NULL, FILE_BEGIN);
    return WriteFile(w->file, h, sizeof(h), &n, NULL) && n == sizeof(h);
}

static const char *type_desc(IMFMediaType *t, char *b, size_t len)
{
    GUID sub = {0};
    UINT32 ch = 0, rate = 0, bits = 0;
    IMFMediaType_GetGUID(t, &MF_MT_SUBTYPE, &sub);
    IMFMediaType_GetUINT32(t, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
    IMFMediaType_GetUINT32(t, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    IMFMediaType_GetUINT32(t, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    snprintf(b, len, "subtype=%08lx ch=%u rate=%u bits=%u", sub.Data1, ch, rate, bits);
    return b;
}

static HRESULT WINAPI w_QueryInterface(IMFSinkWriter *i, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFSinkWriter)) {
        *out = i; IMFSinkWriter_AddRef(i); return S_OK;
    }
    logf_("wavwriter: QueryInterface %08lx-%04x refused", riid->Data1, riid->Data2);
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI w_AddRef(IMFSinkWriter *i) { return InterlockedIncrement(&((wav_writer *)i)->ref); }
static ULONG WINAPI w_Release(IMFSinkWriter *i)
{
    wav_writer *w = (wav_writer *)i;
    LONG r = InterlockedDecrement(&w->ref);
    if (!r) {
        if (w->file != INVALID_HANDLE_VALUE) { write_header(w); CloseHandle(w->file); }
        DeleteCriticalSection(&w->cs);
        free(w);
    }
    return r;
}

static HRESULT WINAPI w_AddStream(IMFSinkWriter *i, IMFMediaType *target, DWORD *index)
{
    wav_writer *w = (wav_writer *)i;
    char d[128];
    if (!target || !index) return E_POINTER;
    if (w->have_target) return MF_E_INVALIDREQUEST;     /* WAV holds one stream */
    w->have_target = TRUE;
    *index = 0;
    logf_("wavwriter: AddStream %s", type_desc(target, d, sizeof(d)));
    return S_OK;
}

static HRESULT WINAPI w_SetInputMediaType(IMFSinkWriter *i, DWORD index, IMFMediaType *type, IMFAttributes *params)
{
    wav_writer *w = (wav_writer *)i;
    GUID sub = {0};
    UINT32 ch = 0, rate = 0, bits = 0, align = 0, avg = 0;
    char d[128];
    if (!type) return E_POINTER;
    if (index != 0 || !w->have_target) return MF_E_INVALIDSTREAMNUMBER;
    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &sub))) return MF_E_INVALIDMEDIATYPE;
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &ch);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, &align);
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg);
    logf_("wavwriter: SetInputMediaType %s", type_desc(type, d, sizeof(d)));
    if (IsEqualGUID(&sub, &MFAudioFormat_PCM)) w->tag = 1;
    else if (IsEqualGUID(&sub, &MFAudioFormat_Float)) w->tag = 3;
    else return MF_E_INVALIDMEDIATYPE;
    if (!ch || !rate || !bits) return MF_E_INVALIDMEDIATYPE;
    if (!align) align = ch * bits / 8;
    if (!avg) avg = align * rate;
    w->channels = ch; w->rate = rate; w->bits = bits; w->align = align; w->bytes_per_sec = avg;
    w->have_input = TRUE;
    return S_OK;
}

static HRESULT WINAPI w_BeginWriting(IMFSinkWriter *i)
{
    wav_writer *w = (wav_writer *)i;
    HRESULT hr = S_OK;
    EnterCriticalSection(&w->cs);
    if (!w->have_input) hr = MF_E_INVALIDREQUEST;
    else if (!w->writing) {
        w->file = CreateFileW(w->path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (w->file == INVALID_HANDLE_VALUE) hr = HRESULT_FROM_WIN32(GetLastError());
        else if (!write_header(w)) hr = HRESULT_FROM_WIN32(GetLastError());
        else w->writing = TRUE;
    }
    LeaveCriticalSection(&w->cs);
    logf_("wavwriter: BeginWriting hr=%08lx", hr);
    return hr;
}

static HRESULT WINAPI w_WriteSample(IMFSinkWriter *i, DWORD index, IMFSample *sample)
{
    wav_writer *w = (wav_writer *)i;
    IMFMediaBuffer *buf = NULL;
    BYTE *p = NULL;
    DWORD len = 0, n = 0;
    HRESULT hr;
    if (!sample) return E_POINTER;
    if (index != 0 || !w->writing) return MF_E_INVALIDREQUEST;
    hr = IMFSample_ConvertToContiguousBuffer(sample, &buf);
    if (FAILED(hr)) return hr;
    hr = IMFMediaBuffer_Lock(buf, &p, NULL, &len);
    if (SUCCEEDED(hr)) {
        EnterCriticalSection(&w->cs);
        if (!WriteFile(w->file, p, len, &n, NULL) || n != len) hr = HRESULT_FROM_WIN32(GetLastError());
        else w->data_bytes += len;
        LeaveCriticalSection(&w->cs);
        IMFMediaBuffer_Unlock(buf);
    }
    IMFMediaBuffer_Release(buf);
    if (FAILED(hr)) logf_("wavwriter: WriteSample hr=%08lx", hr);
    return hr;
}

static HRESULT WINAPI w_SendStreamTick(IMFSinkWriter *i, DWORD index, LONGLONG ts) { return S_OK; }
static HRESULT WINAPI w_PlaceMarker(IMFSinkWriter *i, DWORD index, void *ctx) { return S_OK; }
static HRESULT WINAPI w_NotifyEndOfSegment(IMFSinkWriter *i, DWORD index) { return S_OK; }
static HRESULT WINAPI w_Flush(IMFSinkWriter *i, DWORD index) { return S_OK; }

static HRESULT WINAPI w_Finalize(IMFSinkWriter *i)
{
    wav_writer *w = (wav_writer *)i;
    HRESULT hr = S_OK;
    EnterCriticalSection(&w->cs);
    if (!w->writing) hr = MF_E_INVALIDREQUEST;
    else {
        if (!write_header(w)) hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(w->file);
        w->file = INVALID_HANDLE_VALUE;
        w->writing = FALSE;
    }
    LeaveCriticalSection(&w->cs);
    logf_("wavwriter: Finalize hr=%08lx data=%llu bytes", hr, (unsigned long long)w->data_bytes);
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

static const IMFSinkWriterVtbl wav_vtbl = {
    w_QueryInterface, w_AddRef, w_Release, w_AddStream, w_SetInputMediaType, w_BeginWriting, w_WriteSample,
    w_SendStreamTick, w_PlaceMarker, w_NotifyEndOfSegment, w_Flush, w_Finalize, w_GetServiceForStream, w_GetStatistics,
};

static BOOL url_has_ext(LPCWSTR url, LPCWSTR ext)
{
    size_t n = wcslen(url), e = wcslen(ext);
    return n > e && !_wcsicmp(url + n - e, ext);
}

static BOOL is_wav_url(LPCWSTR url)
{
    size_t n = wcslen(url);
    return n > 4 && !_wcsicmp(url + n - 4, L".wav");
}

/* log every attribute the core passes to the real sink writer factory (diagnostics for container failures) */
static void dump_attributes(IMFAttributes *a)
{
    UINT32 n = 0, i;
    if (FAILED(IMFAttributes_GetCount(a, &n))) return;
    for (i = 0; i < n; i++) {
        GUID key = {0};
        PROPVARIANT v;
        PropVariantInit(&v);
        if (FAILED(IMFAttributes_GetItemByIndex(a, i, &key, &v))) continue;
        if (v.vt == VT_UI4) logf_("  attr %08lx-%04x-%04x = %lu", key.Data1, key.Data2, key.Data3, v.ulVal);
        else if (v.vt == VT_CLSID && v.puuid) logf_("  attr %08lx-%04x-%04x = guid %08lx-%04x-%04x", key.Data1, key.Data2, key.Data3, v.puuid->Data1, v.puuid->Data2, v.puuid->Data3);
        else logf_("  attr %08lx-%04x-%04x vt=%u", key.Data1, key.Data2, key.Data3, v.vt);
        PropVariantClear(&v);
    }
}

static HRESULT WINAPI hook_create_from_url(LPCWSTR url, IMFByteStream *bs, IMFAttributes *attr, IMFSinkWriter **out)
{
    if (url && !bs && out && is_wav_url(url) && wcslen(url) < MAX_PATH) {
        wav_writer *w = calloc(1, sizeof(*w));
        if (!w) return E_OUTOFMEMORY;
        w->lpVtbl = &wav_vtbl;
        w->ref = 1;
        w->file = INVALID_HANDLE_VALUE;
        wcscpy(w->path, url);
        InitializeCriticalSection(&w->cs);
        *out = (IMFSinkWriter *)w;
        logf_("wavwriter: created for %ls", url);
        return S_OK;
    }
    if (url && !bs && out && mp4_create && (url_has_ext(url, L".mp4") || url_has_ext(url, L".m4a") || url_has_ext(url, L".mov"))) {
        HRESULT hr = mp4_create(url, out);
        logf_("mfhooks: mp4 sink writer for %ls = %08lx", url, hr);
        return hr;
    }
    {
        HRESULT hr = real_create_from_url(url, bs, attr, out);
        logf_("mfhooks: MFCreateSinkWriterFromURL(%ls, bytestream=%p, attributes=%p) = %08lx", url ? url : L"(null)", (void *)bs, (void *)attr, hr);
        if (attr) dump_attributes(attr);
        return hr;
    }
}

/* 2. Source reader native media types.
   Wine lists the decoded format (NV12) before the compressed one (H264) for a video stream; Windows lists the
   compressed one first and the core takes type 0 as the file's codec, so every mp4 is rejected ("Source video codec is
   not supported"). The hook reorders the native types of video streams: compressed first, raw formats after. */
typedef HRESULT (WINAPI *get_native_t)(IMFSourceReader *, DWORD, DWORD, IMFMediaType **);
static get_native_t real_get_native;
typedef HRESULT (WINAPI *create_reader_url_t)(LPCWSTR, IMFAttributes *, IMFSourceReader **);
static create_reader_url_t real_create_reader_url;

static BOOL is_raw_video(const GUID *sub)
{
    static const BYTE base[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    if (sub->Data2 != 0 || sub->Data3 != 0x10 || memcmp(sub->Data4, base, 8)) return FALSE;
    switch (sub->Data1) {
    case 20: case 21: case 22: case 23:                 /* RGB24, ARGB32, RGB32, RGB565 */
    case 0x3231564e: case 0x32595559: case 0x56595559:  /* NV12, YUY2, YUYV */
    case 0x43425949: case 0x30323449: case 0x32315659:  /* IYUV, I420, YV12 */
    case 0x59565955: return TRUE;                       /* UYVY */
    }
    return FALSE;
}

static BOOL type_is(IMFMediaType *t, BOOL want_raw)
{
    GUID sub = {0};
    IMFMediaType_GetGUID(t, &MF_MT_SUBTYPE, &sub);
    return is_raw_video(&sub) == want_raw;
}

static HRESULT WINAPI hook_get_native(IMFSourceReader *r, DWORD stream, DWORD index, IMFMediaType **out)
{
    IMFMediaType *t0 = NULL;
    GUID major = {0};
    HRESULT hr;
    DWORD pass, k, seen = 0;
    if (stream >= 0xfffffff0 || FAILED(real_get_native(r, stream, 0, &t0))) return real_get_native(r, stream, index, out);
    IMFMediaType_GetGUID(t0, &MF_MT_MAJOR_TYPE, &major);
    IMFMediaType_Release(t0);
    if (!IsEqualGUID(&major, &MFMediaType_Video)) return real_get_native(r, stream, index, out);
    for (pass = 0; pass < 2; pass++) {      /* pass 0: compressed types, pass 1: raw ones */
        for (k = 0; ; k++) {
            IMFMediaType *t = NULL;
            hr = real_get_native(r, stream, k, &t);
            if (FAILED(hr)) break;
            if (type_is(t, pass == 1)) {
                if (seen == index) {
                    if (index == 0) { GUID sub = {0}; IMFMediaType_GetGUID(t, &MF_MT_SUBTYPE, &sub); logf_("srcreader: stream %lu native type 0 -> %08lx (was type %lu)", stream, sub.Data1, k); }
                    *out = t; return S_OK;
                }
                seen++;
            }
            IMFMediaType_Release(t);
        }
    }
    return MF_E_NO_MORE_TYPES;
}

/* the reader is Wine's own object: patch GetNativeMediaType in its (shared) vtable, once */
static void patch_reader(IMFSourceReader *r)
{
    IMFSourceReaderVtbl *vt = (IMFSourceReaderVtbl *)r->lpVtbl;
    DWORD old;
    if (real_get_native || vt->GetNativeMediaType == hook_get_native) return;
    if (!VirtualProtect(&vt->GetNativeMediaType, sizeof(void *), PAGE_READWRITE, &old)) { logf_("srcreader: VirtualProtect failed %lu", GetLastError()); return; }
    real_get_native = vt->GetNativeMediaType;
    vt->GetNativeMediaType = hook_get_native;
    VirtualProtect(&vt->GetNativeMediaType, sizeof(void *), old, &old);
    logf_("srcreader: GetNativeMediaType hooked");
}

static HRESULT WINAPI hook_create_reader_url(LPCWSTR url, IMFAttributes *attr, IMFSourceReader **out)
{
    HRESULT hr = real_create_reader_url(url, attr, out);
    if (SUCCEEDED(hr) && out && *out) patch_reader(*out);
    return hr;
}

/* point one of the core's imports from `dll` at a replacement, return the original */
static void *hook_import(BYTE *base, const char *dll, const char *func, void *hook)
{
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!dir.VirtualAddress) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *funcs;
        if (_stricmp((char *)base + imp->Name, dll)) continue;
        names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        funcs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, funcs++) {
            DWORD old;
            void *orig;
            IMAGE_IMPORT_BY_NAME *n;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            n = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)n->Name, func)) continue;
            if (!VirtualProtect(&funcs->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) { logf_("mfhooks: VirtualProtect failed %lu", GetLastError()); return NULL; }
            orig = (void *)funcs->u1.Function;
            funcs->u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&funcs->u1.Function, sizeof(void *), old, &old);
            logf_("mfhooks: %s import hooked", func);
            return orig;
        }
    }
    logf_("mfhooks: import %s not found", func);
    return NULL;
}

/* Local MFTs are only returned when the caller passes MFT_ENUM_FLAG_LOCALMFT, which the core (written for Windows,
   where its encoders are system MFTs) does not. Add the flag for audio encoder enumerations. */
typedef HRESULT (WINAPI *mft_enum_ex_t)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO *, const MFT_REGISTER_TYPE_INFO *, IMFActivate ***, UINT32 *);
static mft_enum_ex_t real_mft_enum_ex;

static HRESULT WINAPI hook_mft_enum_ex(GUID category, UINT32 flags, const MFT_REGISTER_TYPE_INFO *in, const MFT_REGISTER_TYPE_INFO *out, IMFActivate ***acts, UINT32 *count)
{
    HRESULT hr;
    if (IsEqualGUID(&category, &MFT_CATEGORY_AUDIO_ENCODER)) flags |= MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SYNCMFT;
    hr = real_mft_enum_ex(category, flags, in, out, acts, count);
    if (IsEqualGUID(&category, &MFT_CATEGORY_AUDIO_ENCODER))
        logf_("mfhooks: MFTEnumEx(audio encoder, flags=%x, out=%08lx) = %08lx count=%u", flags, out ? out->guidSubtype.Data1 : 0, hr, count ? *count : 0);
    return hr;
}

/* 3. nvb_media.dll (FFmpeg based): AAC encoder, which Wine lacks and the core needs for mp4 output, and an mp4 sink
   writer, because Wine's mp4 media sink is a stub. */
static void load_media_dll(void)
{
    HMODULE m = LoadLibraryA("nvb_media.dll");
    HRESULT (*reg)(void) = m ? (HRESULT (*)(void))GetProcAddress(m, "nvb_aac_register") : NULL;
    void (*set_logger)(void (*)(const char *, ...)) = m ? (void (*)(void (*)(const char *, ...)))GetProcAddress(m, "nvb_media_set_logger") : NULL;
    if (!reg) { logf_("mfhooks: nvb_media.dll not available (%lu), mp4 export will fail", GetLastError()); return; }
    if (set_logger) set_logger(logf_);
    mp4_create = (mp4_create_t)GetProcAddress(m, "nvb_mp4_create");
    logf_("mfhooks: AAC encoder MFT registered hr=%08lx, mp4 sink %s", reg(), mp4_create ? "available" : "missing");
}

void mfhooks_install(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA("NvBroadcastCore.dll");
    if (!base) return;
    load_media_dll();
    real_mft_enum_ex = (mft_enum_ex_t)hook_import(base, "mfplat.dll", "MFTEnumEx", hook_mft_enum_ex);
    real_create_from_url = (create_from_url_t)hook_import(base, "mfreadwrite.dll", "MFCreateSinkWriterFromURL", hook_create_from_url);
    real_create_reader_url = (create_reader_url_t)hook_import(base, "mfreadwrite.dll", "MFCreateSourceReaderFromURL", hook_create_reader_url);
}
