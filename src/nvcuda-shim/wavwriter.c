/* WAV sink writer for file post-processing.
   Wine's mfreadwrite creates the writer for a ".wav" URL through MFCreateWAVEMediaSink(bytestream, NULL), which Wine's
   mfsrcsnk.dll rejects with E_POINTER (0x80004003), so the core's MFCreateSinkWriterFromURL fails and the whole job
   dies with errorStatus -9111. NvBroadcastCore.dll is signature-checked, so instead of patching it we redirect its
   import of MFCreateSinkWriterFromURL: ".wav" URLs get the minimal IMFSinkWriter below, everything else goes to the
   real function. The writer stores the samples exactly as they arrive (the header is taken from the input media type). */
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

static BOOL is_wav_url(LPCWSTR url)
{
    size_t n = wcslen(url);
    return n > 4 && !_wcsicmp(url + n - 4, L".wav");
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
    return real_create_from_url(url, bs, attr, out);
}

/* point the core's import of MFCreateSinkWriterFromURL at hook_create_from_url */
void wavwriter_install(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA("NvBroadcastCore.dll");
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY dir;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!base) return;
    nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *funcs;
        if (_stricmp((char *)base + imp->Name, "mfreadwrite.dll")) continue;
        names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        funcs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, funcs++) {
            DWORD old;
            IMAGE_IMPORT_BY_NAME *n;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            n = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)n->Name, "MFCreateSinkWriterFromURL")) continue;
            if (!VirtualProtect(&funcs->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) { logf_("wavwriter: VirtualProtect failed %lu", GetLastError()); return; }
            real_create_from_url = (create_from_url_t)funcs->u1.Function;
            funcs->u1.Function = (ULONG_PTR)hook_create_from_url;
            VirtualProtect(&funcs->u1.Function, sizeof(void *), old, &old);
            logf_("wavwriter: MFCreateSinkWriterFromURL import hooked");
            return;
        }
    }
    logf_("wavwriter: import not found");
}
