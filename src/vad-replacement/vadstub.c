#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* NvAudCap API replacement for NVIDIA Broadcast on Wine.
   The original library talks to a kernel driver that creates a virtual microphone.
   Here the mic frames the core pushes (S16 stereo 48 kHz, 0x780 bytes = 10 ms) are written to a
   Wine render endpoint whose name contains "Mic Input"; PipeWire loops that sink back as a source. */

static void logf_(const char *fmt, ...)
{
    char b[300]; va_list ap; DWORD w; HANDLE h; SYSTEMTIME t; int n;
    GetLocalTime(&t);
    n = snprintf(b, sizeof(b) - 4, "%02d:%02d:%02d.%03d p%lu t%lu ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId());
    va_start(ap, fmt); vsnprintf(b + n, sizeof(b) - 4 - n, fmt, ap); va_end(ap);
    strcat(b, "\r\n");
    h = CreateFileA("Z:\\tmp\\nvvad_stub.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, b, (DWORD)strlen(b), &w, NULL); CloseHandle(h); }
}

#define MIC_SINK_MATCH L"NvB Mic Input"
#define FRAME_BYTES 4          /* S16 stereo */
#define RATE 48000

typedef struct { int type; } session_t;

static IAudioClient *g_client;
static IAudioRenderClient *g_render;
static UINT32 g_buffer_frames;
static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static unsigned long long g_pushed_frames, g_dropped_frames;

static BOOL CALLBACK init_lock(PINIT_ONCE o, PVOID p, PVOID *c) { InitializeCriticalSection(&g_lock); return TRUE; }

static IMMDevice *find_mic_sink(void)
{
    IMMDeviceEnumerator *e = NULL;
    IMMDeviceCollection *c = NULL;
    IMMDevice *found = NULL;
    UINT n = 0, i;
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&e))) return NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(e, eRender, DEVICE_STATE_ACTIVE, &c))) {
        IMMDeviceCollection_GetCount(c, &n);
        for (i = 0; i < n && !found; i++) {
            IMMDevice *d = NULL;
            IPropertyStore *ps = NULL;
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(IMMDeviceCollection_Item(c, i, &d)) && SUCCEEDED(IMMDevice_OpenPropertyStore(d, STGM_READ, &ps))) {
                if (SUCCEEDED(IPropertyStore_GetValue(ps, &PKEY_Device_FriendlyName, &v)) && v.pwszVal && wcsstr(v.pwszVal, MIC_SINK_MATCH)) {
                    logf_("mic sink endpoint: %ls", v.pwszVal);
                    found = d; d = NULL;
                }
                PropVariantClear(&v);
                IPropertyStore_Release(ps);
            }
            if (d) IMMDevice_Release(d);
        }
        IMMDeviceCollection_Release(c);
    }
    IMMDeviceEnumerator_Release(e);
    return found;
}

static int start_render(void)
{
    IMMDevice *dev;
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, RATE, RATE * FRAME_BYTES, FRAME_BYTES, 16, 0 };
    HRESULT hr;
    if (g_client) return 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    dev = find_mic_sink();
    if (!dev) { logf_("start_render: no endpoint matching '%ls'", MIC_SINK_MATCH); return 1; }
    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&g_client);
    IMMDevice_Release(dev);
    if (FAILED(hr)) { logf_("Activate hr=%08lx", hr); g_client = NULL; return 1; }
    hr = IAudioClient_Initialize(g_client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 2000000 /* 200 ms */, 0, &wf, NULL);
    if (FAILED(hr)) { logf_("Initialize hr=%08lx", hr); IAudioClient_Release(g_client); g_client = NULL; return 1; }
    IAudioClient_GetBufferSize(g_client, &g_buffer_frames);
    hr = IAudioClient_GetService(g_client, &IID_IAudioRenderClient, (void **)&g_render);
    if (FAILED(hr)) { logf_("GetService hr=%08lx", hr); IAudioClient_Release(g_client); g_client = NULL; return 1; }
    hr = IAudioClient_Start(g_client);
    logf_("render started: buffer=%u frames, Start hr=%08lx", g_buffer_frames, hr);
    return 0;
}

static void stop_render(void)
{
    if (!g_client) return;
    IAudioClient_Stop(g_client);
    if (g_render) { IAudioRenderClient_Release(g_render); g_render = NULL; }
    IAudioClient_Release(g_client);
    g_client = NULL;
    logf_("render stopped: pushed=%llu dropped=%llu frames", g_pushed_frames, g_dropped_frames);
}

/* push whole frames, never block longer than ~20 ms */
static void write_frames(const unsigned char *buf, UINT32 frames)
{
    int tries;
    for (tries = 0; tries < 20 && frames; tries++) {
        UINT32 pad = 0, room, n;
        BYTE *dst = NULL;
        if (FAILED(IAudioClient_GetCurrentPadding(g_client, &pad))) return;
        room = g_buffer_frames - pad;
        if (!room) { Sleep(1); continue; }
        n = frames < room ? frames : room;
        if (FAILED(IAudioRenderClient_GetBuffer(g_render, n, &dst))) return;
        memcpy(dst, buf, (size_t)n * FRAME_BYTES);
        IAudioRenderClient_ReleaseBuffer(g_render, n, 0);
        g_pushed_frames += n;
        buf += (size_t)n * FRAME_BYTES;
        frames -= n;
    }
    g_dropped_frames += frames;
}

/* ---- table slots ---- */
typedef long long (__cdecl *fn_t)(long long, long long, long long, long long);

static long long __cdecl api_open_session(long long a, long long size, long long type, long long out)
{
    session_t *s = (session_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    s->type = (int)type;
    if (out) *(void **)out = s;
    logf_("OpenSession type=%lld -> %p", type, (void *)s);
    return 0;
}

static long long __cdecl api_check_default(long long session, long long out, long long c, long long d)
{
    if (out) *(int *)out = 0;
    return 0;
}

static long long __cdecl api_start_mic(long long session, long long a, long long b, long long c)
{
    InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL);
    EnterCriticalSection(&g_lock);
    start_render();
    LeaveCriticalSection(&g_lock);
    logf_("StartMicAudioPush");
    return 0;
}

static long long __cdecl api_stop_mic(long long session, long long a, long long b, long long c)
{
    InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL);
    EnterCriticalSection(&g_lock);
    stop_render();
    LeaveCriticalSection(&g_lock);
    return 0;
}

static long long __cdecl api_push_mic(long long session, long long buf, long long bytes, long long written)
{
    InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL);
    EnterCriticalSection(&g_lock);
    if (g_client && g_render && buf && bytes > 0) write_frames((const unsigned char *)buf, (UINT32)(bytes / FRAME_BYTES));
    LeaveCriticalSection(&g_lock);
    if (written) *(DWORD *)written = (DWORD)bytes;
    return 0;
}

#define SLOTS 44
#define STUB(i) static long long __cdecl stub##i(long long a, long long b, long long c, long long d) { logf_("slot 0x%x(%llx, %llx, %llx, %llx) [stub]", 8 + (i) * 8, a, b, c, d); return 0; }
STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7) STUB(8) STUB(9) STUB(10) STUB(11) STUB(12) STUB(13) STUB(14)
STUB(15) STUB(16) STUB(17) STUB(18) STUB(19) STUB(20) STUB(21) STUB(22) STUB(23) STUB(24) STUB(25) STUB(26) STUB(27) STUB(28)
STUB(29) STUB(30) STUB(31) STUB(32) STUB(33) STUB(34) STUB(35) STUB(36) STUB(37) STUB(38) STUB(39) STUB(40) STUB(41) STUB(42) STUB(43)

static fn_t stubs[SLOTS] = {
    stub0, stub1, stub2, stub3, stub4, stub5, stub6, stub7, stub8, stub9, stub10, stub11, stub12, stub13, stub14,
    stub15, stub16, stub17, stub18, stub19, stub20, stub21, stub22, stub23, stub24, stub25, stub26, stub27, stub28,
    stub29, stub30, stub31, stub32, stub33, stub34, stub35, stub36, stub37, stub38, stub39, stub40, stub41, stub42, stub43 };

__declspec(dllexport) int __cdecl NvAudCapAPICreateInstance(void *tbl)
{
    int i;
    unsigned tag;
    if (!tbl) return 2;
    tag = *(unsigned *)tbl;
    logf_("CreateInstance tag=0x%08x", tag);
    for (i = 0; i < SLOTS; i++) ((void **)tbl)[1 + i] = (void *)stubs[i];
    ((void **)tbl)[0x08 / 8] = (void *)api_open_session;
    ((void **)tbl)[0x18 / 8] = (void *)api_check_default;
    ((void **)tbl)[0xe8 / 8] = (void *)api_start_mic;
    ((void **)tbl)[0xf0 / 8] = (void *)api_stop_mic;
    ((void **)tbl)[0xf8 / 8] = (void *)api_push_mic;
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { return TRUE; }
