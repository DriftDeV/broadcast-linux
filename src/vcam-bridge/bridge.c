/* nvb-vcam-bridge.exe -- runs inside the Wine prefix.
 *
 * Connects to the DirectShow virtual camera "Camera (NVIDIA Broadcast)" (the filter that shows the processed
 * video) as a normal consumer, and copies every frame into /dev/shm/nvb-vcam so a Linux program can feed it to
 * v4l2loopback.  The graph only runs while the Linux side sets the `active` flag (a reader has the loopback
 * device open): with no reader the consumer disconnects and Broadcast stops the physical webcam.
 *
 * Shared file layout: see vcam_shm.h (kept in sync by hand with the Linux feeder).
 */
#define COBJMACROS
#include <windows.h>
#include <dshow.h>
#include <qedit.h>
#include <initguid.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "vcam_shm.h"

DEFINE_GUID(CLSID_NullRenderer, 0xC1F400A4, 0x3F08, 0x11D3, 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37);

#define SHM_PATH "Z:\\dev\\shm\\nvb-vcam"
#define LOG_PATH "Z:\\tmp\\nvb-vcam-bridge.log"
#define DEVICE_MATCH L"NVIDIA Broadcast"   /* friendly name of the virtual camera */

static void logf_(const char *fmt, ...)
{
    char b[400]; va_list ap; DWORD w; HANDLE h; SYSTEMTIME t; int n;
    GetLocalTime(&t);
    n = snprintf(b, sizeof(b) - 4, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt); vsnprintf(b + n, sizeof(b) - 4 - n, fmt, ap); va_end(ap);
    strcat(b, "\r\n");
    h = CreateFileA(LOG_PATH, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, b, (DWORD)strlen(b), &w, NULL); CloseHandle(h); }
}

/* ---- shared memory file ---- */
static HANDLE g_shm = INVALID_HANDLE_VALUE;
static struct vcam_shm_header g_hdr;
static unsigned long long g_seq;

static int shm_open_file(void)
{
    LARGE_INTEGER sz;
    if (g_shm != INVALID_HANDLE_VALUE) return 1;
    g_shm = CreateFileA(SHM_PATH, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (g_shm == INVALID_HANDLE_VALUE) { logf_("cannot open %s: %lu", SHM_PATH, GetLastError()); return 0; }
    sz.QuadPart = VCAM_SHM_SIZE;
    SetFilePointerEx(g_shm, sz, NULL, FILE_BEGIN);
    SetEndOfFile(g_shm);
    memset(&g_hdr, 0, sizeof(g_hdr));
    g_hdr.magic = VCAM_MAGIC;
    g_hdr.version = VCAM_VERSION;
    return 1;
}

static void shm_write_at(LONGLONG off, const void *p, DWORD n)
{
    LARGE_INTEGER o; DWORD w;
    o.QuadPart = off;
    SetFilePointerEx(g_shm, o, NULL, FILE_BEGIN);
    WriteFile(g_shm, p, n, &w, NULL);
}

static unsigned read_active(void)
{
    LARGE_INTEGER o; DWORD r = 0; unsigned v = 0;
    if (g_shm == INVALID_HANDLE_VALUE) return 0;
    o.QuadPart = offsetof(struct vcam_shm_header, active);
    SetFilePointerEx(g_shm, o, NULL, FILE_BEGIN);
    if (!ReadFile(g_shm, &v, sizeof(v), &r, NULL) || r != sizeof(v)) return 0;
    return v;
}

static void publish_frame(const BYTE *data, long len, long w, long h, int bottom_up)
{
    DWORD ms = GetTickCount();
    if ((unsigned)len > VCAM_MAX_FRAME) return;
    g_hdr.width = (unsigned)w; g_hdr.height = (unsigned)h; g_hdr.stride = (unsigned)(w * 4);
    g_hdr.pixfmt = VCAM_PIX_BGRA; g_hdr.bottom_up = bottom_up; g_hdr.frame_bytes = (unsigned)len;
    g_hdr.timestamp_ms = ms;
    g_hdr.heartbeat_ms = ms;
    g_seq++;                                  /* odd = write in progress */
    shm_write_at(offsetof(struct vcam_shm_header, seq), &g_seq, sizeof(g_seq));
    shm_write_at(0, &g_hdr, offsetof(struct vcam_shm_header, active));
    shm_write_at(VCAM_HEADER_SIZE, data, (DWORD)len);
    g_seq++;                                  /* even = complete */
    shm_write_at(offsetof(struct vcam_shm_header, seq), &g_seq, sizeof(g_seq));
}

/* ---- DirectShow graph ---- */
static IGraphBuilder *g_gb;
static IMediaControl *g_mc;
static ISampleGrabber *g_sg;
static long g_w, g_h, g_bpp;
static int g_bottom_up = 1;
static volatile LONG g_frames;

static HRESULT STDMETHODCALLTYPE cb_qi(ISampleGrabberCB *t, REFIID r, void **o) { *o = t; return S_OK; }
static ULONG STDMETHODCALLTYPE cb_ar(ISampleGrabberCB *t) { return 2; }
static ULONG STDMETHODCALLTYPE cb_rel(ISampleGrabberCB *t) { return 1; }
static HRESULT STDMETHODCALLTYPE cb_buf(ISampleGrabberCB *t, double d, BYTE *b, long l) { return S_OK; }
static HRESULT STDMETHODCALLTYPE cb_sample(ISampleGrabberCB *t, double d, IMediaSample *s)
{
    BYTE *p = NULL;
    long l = IMediaSample_GetActualDataLength(s);
    if (SUCCEEDED(IMediaSample_GetPointer(s, &p)) && p && g_bpp == 32) {
        publish_frame(p, l, g_w, g_h, g_bottom_up);
        InterlockedIncrement(&g_frames);
    }
    return S_OK;
}
static const ISampleGrabberCBVtbl cb_vtbl = { cb_qi, cb_ar, cb_rel, cb_sample, cb_buf };
static ISampleGrabberCB g_cb = { &cb_vtbl };

static IBaseFilter *find_vcam(void)
{
    ICreateDevEnum *de = NULL; IEnumMoniker *em = NULL; IMoniker *m; IBaseFilter *f = NULL;
    if (FAILED(CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, &IID_ICreateDevEnum, (void **)&de))) return NULL;
    if (ICreateDevEnum_CreateClassEnumerator(de, &CLSID_VideoInputDeviceCategory, &em, 0) == S_OK) {
        while (!f && IEnumMoniker_Next(em, 1, &m, NULL) == S_OK) {
            IPropertyBag *pb = NULL; VARIANT v; VariantInit(&v);
            if (SUCCEEDED(IMoniker_BindToStorage(m, 0, 0, &IID_IPropertyBag, (void **)&pb))) {
                if (SUCCEEDED(IPropertyBag_Read(pb, L"FriendlyName", &v, 0)) && v.bstrVal &&
                    wcsstr(v.bstrVal, L"Camera") && wcsstr(v.bstrVal, DEVICE_MATCH))
                    IMoniker_BindToObject(m, 0, 0, &IID_IBaseFilter, (void **)&f);
                VariantClear(&v);
                IPropertyBag_Release(pb);
            }
            IMoniker_Release(m);
        }
        IEnumMoniker_Release(em);
    }
    ICreateDevEnum_Release(de);
    return f;
}

/* first pin of the given direction (we connect pins directly: RenderStream() would insert a Smart Tee that
   deadlocks on the single-buffer allocator of the virtual camera) */
static IPin *first_pin(IBaseFilter *f, PIN_DIRECTION want)
{
    IEnumPins *ep = NULL; IPin *pin = NULL;
    if (FAILED(IBaseFilter_EnumPins(f, &ep))) return NULL;
    while (IEnumPins_Next(ep, 1, &pin, NULL) == S_OK) {
        PIN_DIRECTION d;
        if (SUCCEEDED(IPin_QueryDirection(pin, &d)) && d == want) break;
        IPin_Release(pin); pin = NULL;
    }
    IEnumPins_Release(ep);
    return pin;
}

static int graph_start(void)
{
    ICaptureGraphBuilder2 *cgb = NULL;
    IBaseFilter *cam = NULL, *sg = NULL, *nr = NULL;
    AM_MEDIA_TYPE c;
    HRESULT hr;

    cam = find_vcam();
    if (!cam) { logf_("virtual camera not found"); return 0; }
    if (FAILED(CoCreateInstance(&CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, &IID_ICaptureGraphBuilder2, (void **)&cgb)) ||
        FAILED(CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void **)&g_gb))) {
        logf_("graph creation failed"); return 0;
    }
    ICaptureGraphBuilder2_SetFiltergraph(cgb, g_gb);
    IGraphBuilder_AddFilter(g_gb, cam, L"vcam");
    CoCreateInstance(&CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&sg);
    IBaseFilter_QueryInterface(sg, &IID_ISampleGrabber, (void **)&g_sg);
    IGraphBuilder_AddFilter(g_gb, sg, L"grab");
    CoCreateInstance(&CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&nr);
    IGraphBuilder_AddFilter(g_gb, nr, L"null");
    ISampleGrabber_SetOneShot(g_sg, FALSE);
    ISampleGrabber_SetBufferSamples(g_sg, FALSE);
    ISampleGrabber_SetCallback(g_sg, &g_cb, 0);
    { IPin *co = first_pin(cam, PINDIR_OUTPUT), *gi = first_pin(sg, PINDIR_INPUT), *go = first_pin(sg, PINDIR_OUTPUT), *ni = first_pin(nr, PINDIR_INPUT);
      if (!co || !gi || !go || !ni) { logf_("pins missing"); return 0; }
      hr = IGraphBuilder_Connect(g_gb, co, gi);
      if (FAILED(hr)) { logf_("connect cam->grabber hr=%08lx", hr); return 0; }
      hr = IGraphBuilder_Connect(g_gb, go, ni);
      if (FAILED(hr)) { logf_("connect grabber->null hr=%08lx", hr); return 0; } }
    memset(&c, 0, sizeof(c));
    if (SUCCEEDED(ISampleGrabber_GetConnectedMediaType(g_sg, &c)) && c.pbFormat) {
        VIDEOINFOHEADER *vi = (VIDEOINFOHEADER *)c.pbFormat;
        g_w = vi->bmiHeader.biWidth; g_h = labs(vi->bmiHeader.biHeight); g_bpp = vi->bmiHeader.biBitCount;
        g_bottom_up = vi->bmiHeader.biHeight > 0;
        logf_("connected %ldx%ld %ldbpp bottom_up=%d subtype=%08lx", g_w, g_h, g_bpp, g_bottom_up, (unsigned long)c.subtype.Data1);
    }
    /* live source: run without a reference clock so samples are rendered as soon as they arrive */
    { IMediaFilter *mf = NULL;
      if (SUCCEEDED(IGraphBuilder_QueryInterface(g_gb, &IID_IMediaFilter, (void **)&mf))) {
          hr = IMediaFilter_SetSyncSource(mf, NULL);
          logf_("SetSyncSource(NULL) hr=%08lx", hr);
          IMediaFilter_Release(mf);
      } }
    IGraphBuilder_QueryInterface(g_gb, &IID_IMediaControl, (void **)&g_mc);
    hr = IMediaControl_Run(g_mc);
    logf_("graph run hr=%08lx", hr);
    g_frames = 0;
    return SUCCEEDED(hr);
}

static void graph_stop(void)
{
    if (g_mc) { IMediaControl_Stop(g_mc); IMediaControl_Release(g_mc); g_mc = NULL; }
    if (g_sg) { ISampleGrabber_SetCallback(g_sg, NULL, 0); ISampleGrabber_Release(g_sg); g_sg = NULL; }
    if (g_gb) { IGraphBuilder_Release(g_gb); g_gb = NULL; }
    logf_("graph stopped");
}

int main(void)
{
    int running = 0, fails = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    logf_("bridge started");
    for (;;) {
        unsigned active;
        if (!shm_open_file()) { Sleep(1000); continue; }
        active = read_active();
        if (active && !running) {
            if (graph_start()) { running = 1; fails = 0; }
            else { graph_stop(); if (++fails > 3) Sleep(3000); }
        } else if (!active && running) {
            graph_stop(); running = 0;
        } else if (running) {
            /* keep the heartbeat fresh even when frames are scarce */
            DWORD ms = GetTickCount();
            shm_write_at(offsetof(struct vcam_shm_header, heartbeat_ms), &ms, sizeof(ms));
        }
        { static DWORD last; DWORD n = GetTickCount(); if (n - last > 2000) { last = n; logf_("tick running=%d active=%u frames=%ld", running, active, (long)g_frames); } }
        Sleep(running ? 500 : 250);
    }
    return 0;
}
