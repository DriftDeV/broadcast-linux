#define COBJMACROS
#include <windows.h>
#include <dshow.h>
#include <stdio.h>
#include <initguid.h>
/* minimal ISampleGrabber (qedit.h is not always available) */
DEFINE_GUID(CLSID_SampleGrabber_,0xC1F400A0,0x3F08,0x11D3,0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37);
DEFINE_GUID(CLSID_NullRenderer_,0xC1F400A4,0x3F08,0x11D3,0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37);
DEFINE_GUID(IID_ISampleGrabber_,0x6B652FFF,0x11FE,0x4FCE,0x92,0xAD,0x02,0x66,0xB5,0xD7,0xC7,0x8F);
DEFINE_GUID(IID_ISampleGrabberCB_,0x0579154A,0x2B53,0x4994,0xB0,0xD0,0xE7,0x73,0x14,0x8E,0xFF,0x85);
typedef struct ISampleGrabberCB ISampleGrabberCB;
typedef struct ISampleGrabberCBVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(ISampleGrabberCB*,REFIID,void**); ULONG (STDMETHODCALLTYPE *AddRef)(ISampleGrabberCB*); ULONG (STDMETHODCALLTYPE *Release)(ISampleGrabberCB*);
  HRESULT (STDMETHODCALLTYPE *SampleCB)(ISampleGrabberCB*,double,IMediaSample*); HRESULT (STDMETHODCALLTYPE *BufferCB)(ISampleGrabberCB*,double,BYTE*,long); } ISampleGrabberCBVtbl;
struct ISampleGrabberCB { const ISampleGrabberCBVtbl *lpVtbl; };
typedef struct ISampleGrabber ISampleGrabber;
typedef struct ISampleGrabberVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(ISampleGrabber*,REFIID,void**); ULONG (STDMETHODCALLTYPE *AddRef)(ISampleGrabber*); ULONG (STDMETHODCALLTYPE *Release)(ISampleGrabber*);
  HRESULT (STDMETHODCALLTYPE *SetOneShot)(ISampleGrabber*,BOOL); HRESULT (STDMETHODCALLTYPE *SetMediaType)(ISampleGrabber*,const AM_MEDIA_TYPE*); HRESULT (STDMETHODCALLTYPE *GetConnectedMediaType)(ISampleGrabber*,AM_MEDIA_TYPE*);
  HRESULT (STDMETHODCALLTYPE *SetBufferSamples)(ISampleGrabber*,BOOL); HRESULT (STDMETHODCALLTYPE *GetCurrentBuffer)(ISampleGrabber*,long*,long*); HRESULT (STDMETHODCALLTYPE *GetCurrentSample)(ISampleGrabber*,IMediaSample**);
  HRESULT (STDMETHODCALLTYPE *SetCallback)(ISampleGrabber*,ISampleGrabberCB*,long); } ISampleGrabberVtbl;
struct ISampleGrabber { const ISampleGrabberVtbl *lpVtbl; };
static volatile LONG frames=0; static volatile LONG saved=0; static long gw=0,gh=0,gbpp=24; static LONGLONG firstT=0,lastT=0; static HRESULT lastHr=0;
static HRESULT STDMETHODCALLTYPE cb_qi(ISampleGrabberCB*t,REFIID r,void**o){ *o=t; return S_OK; }
static ULONG STDMETHODCALLTYPE cb_ar(ISampleGrabberCB*t){ return 2; } static ULONG STDMETHODCALLTYPE cb_rel(ISampleGrabberCB*t){ return 1; }
static HRESULT STDMETHODCALLTYPE cb_buf(ISampleGrabberCB*t,double d,BYTE*b,long l);
static HRESULT STDMETHODCALLTYPE cb_sample(ISampleGrabberCB*t,double d,IMediaSample*s){ LONGLONG a=0,b=0; HRESULT hr=IMediaSample_GetTime(s,&a,&b); { BYTE*p=NULL; long l=IMediaSample_GetActualDataLength(s); IMediaSample_GetPointer(s,&p); if(p) { if(frames==60) cb_buf(t,d,p,l);} } lastHr=hr; if(!frames) firstT=a; lastT=a; InterlockedIncrement(&frames); return S_OK; }
static HRESULT STDMETHODCALLTYPE cb_buf(ISampleGrabberCB*t,double d,BYTE*b,long l){
  if(frames==60 && !saved){ long i; unsigned long long sum=0; unsigned mn=255,mx=0; saved=1;
    for(i=0;i<l;i++){ sum+=b[i]; if(b[i]<mn)mn=b[i]; if(b[i]>mx)mx=b[i]; }
    printf("FRAME len=%ld mean=%.2f min=%u max=%u (%ldx%ld %ldbpp)\n",l,(double)sum/l,mn,mx,gw,gh,gbpp);
    { FILE*f=fopen("Z:\\tmp\\vcam_frame.raw","wb"); if(f){ fwrite(b,1,l,f); fclose(f);} }
  } return S_OK; }
static const ISampleGrabberCBVtbl vt={cb_qi,cb_ar,cb_rel,cb_sample,cb_buf}; static ISampleGrabberCB cbo={&vt};
int main(int argc,char**argv){ setvbuf(stdout,NULL,_IONBF,0); WCHAR want[128]=L"HD Webcam"; if(argc>1) MultiByteToWideChar(CP_ACP,0,argv[1],-1,want,128);
  ICreateDevEnum *de; IEnumMoniker *em; IMoniker *m; HRESULT hr; IBaseFilter *cam=NULL;
  CoInitializeEx(NULL,COINIT_MULTITHREADED);
  CoCreateInstance(&CLSID_SystemDeviceEnum,NULL,CLSCTX_INPROC_SERVER,&IID_ICreateDevEnum,(void**)&de);
  ICreateDevEnum_CreateClassEnumerator(de,&CLSID_VideoInputDeviceCategory,&em,0);
  while(IEnumMoniker_Next(em,1,&m,NULL)==S_OK){ IPropertyBag *pb; VARIANT v; VariantInit(&v); IMoniker_BindToStorage(m,0,0,&IID_IPropertyBag,(void**)&pb); IPropertyBag_Read(pb,L"FriendlyName",&v,0);
    if(wcsstr(v.bstrVal,want)){ IMoniker_BindToObject(m,0,0,&IID_IBaseFilter,(void**)&cam); break; } }
  if(!cam){puts("no cam");return 1;}
  { ICaptureGraphBuilder2 *cgb; IGraphBuilder *gb; IBaseFilter *sg=NULL,*nr=NULL; ISampleGrabber *sgi; IMediaControl *mc; AM_MEDIA_TYPE mt; int i;
    CoCreateInstance(&CLSID_CaptureGraphBuilder2,NULL,CLSCTX_INPROC_SERVER,&IID_ICaptureGraphBuilder2,(void**)&cgb); CoCreateInstance(&CLSID_FilterGraph,NULL,CLSCTX_INPROC_SERVER,&IID_IGraphBuilder,(void**)&gb);
    ICaptureGraphBuilder2_SetFiltergraph(cgb,gb); IGraphBuilder_AddFilter(gb,cam,L"cam");
    CoCreateInstance(&CLSID_SampleGrabber_,NULL,CLSCTX_INPROC_SERVER,&IID_IBaseFilter,(void**)&sg); IBaseFilter_QueryInterface(sg,&IID_ISampleGrabber_,(void**)&sgi); IGraphBuilder_AddFilter(gb,sg,L"sg");
    CoCreateInstance(&CLSID_NullRenderer_,NULL,CLSCTX_INPROC_SERVER,&IID_IBaseFilter,(void**)&nr); IGraphBuilder_AddFilter(gb,nr,L"nr");
    memset(&mt,0,sizeof mt); mt.majortype=MEDIATYPE_Video; mt.subtype=MEDIASUBTYPE_RGB24; if(!(argc>2 && argv[2][0]=='a')) sgi->lpVtbl->SetMediaType(sgi,&mt);
    sgi->lpVtbl->SetOneShot(sgi,FALSE); sgi->lpVtbl->SetBufferSamples(sgi,TRUE); sgi->lpVtbl->SetCallback(sgi,&cbo,0);
    hr=ICaptureGraphBuilder2_RenderStream(cgb,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,(IUnknown*)cam,sg,nr); printf("RenderStream hr=%08lx\n",hr);
    { AM_MEDIA_TYPE c; memset(&c,0,sizeof c); if(SUCCEEDED(sgi->lpVtbl->GetConnectedMediaType(sgi,&c)) && c.pbFormat){ VIDEOINFOHEADER*vi=(VIDEOINFOHEADER*)c.pbFormat; gw=vi->bmiHeader.biWidth; gh=vi->bmiHeader.biHeight; gbpp=vi->bmiHeader.biBitCount; printf("connected: %ldx%ld %ldbpp subtype=%08lx\n",gw,gh,gbpp,(unsigned long)c.subtype.Data1);} }
    IGraphBuilder_QueryInterface(gb,&IID_IMediaControl,(void**)&mc); hr=IMediaControl_Run(mc); printf("Run hr=%08lx\n",hr);
    for(i=0;i<8;i++){ Sleep(1500); printf("t=%ds frames=%ld lastGetTimeHr=%08lx firstT=%lld lastT=%lld\n",i+1,frames,lastHr,firstT,lastT); }
    IMediaControl_Stop(mc); }
  return 0; }
