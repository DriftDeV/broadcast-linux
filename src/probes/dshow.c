#define COBJMACROS
#include <windows.h>
#include <dshow.h>
#include <stdio.h>
#include <initguid.h>
static void guid(const GUID *g, char *o){ sprintf(o,"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g->Data1,g->Data2,g->Data3,g->Data4[0],g->Data4[1],g->Data4[2],g->Data4[3],g->Data4[4],g->Data4[5],g->Data4[6],g->Data4[7]); }
int main(void){
  ICreateDevEnum *de; IEnumMoniker *em; IMoniker *m; HRESULT hr; int n=0;
  CoInitializeEx(NULL,COINIT_MULTITHREADED);
  hr=CoCreateInstance(&CLSID_SystemDeviceEnum,NULL,CLSCTX_INPROC_SERVER,&IID_ICreateDevEnum,(void**)&de); printf("SystemDeviceEnum hr=%08lx\n",hr); if(FAILED(hr)) return 1;
  hr=ICreateDevEnum_CreateClassEnumerator(de,&CLSID_VideoInputDeviceCategory,&em,0); printf("CreateClassEnumerator hr=%08lx\n",hr); if(hr!=S_OK){puts("no video devices");return 0;}
  while(IEnumMoniker_Next(em,1,&m,NULL)==S_OK){
    IPropertyBag *pb; VARIANT v; IBaseFilter *f; IEnumPins *ep; IPin *pin; n++;
    VariantInit(&v);
    if(SUCCEEDED(IMoniker_BindToStorage(m,0,0,&IID_IPropertyBag,(void**)&pb))){ IPropertyBag_Read(pb,L"FriendlyName",&v,0); printf("DEVICE %d: %ls\n",n,v.bstrVal); }
    hr=IMoniker_BindToObject(m,0,0,&IID_IBaseFilter,(void**)&f); printf("  BindToObject hr=%08lx\n",hr); if(FAILED(hr)) continue;
    IBaseFilter_EnumPins(f,&ep);
    while(IEnumPins_Next(ep,1,&pin,NULL)==S_OK){
      PIN_INFO pi; IAMStreamConfig *sc; IPin_QueryPinInfo(pin,&pi); printf("  PIN %ls dir=%d\n",pi.achName,pi.dir);
      if(SUCCEEDED(IPin_QueryInterface(pin,&IID_IAMStreamConfig,(void**)&sc))){
        int cnt=0,sz=0,i; IAMStreamConfig_GetNumberOfCapabilities(sc,&cnt,&sz); printf("    IAMStreamConfig caps=%d size=%d\n",cnt,sz);
        for(i=0;i<cnt;i++){ AM_MEDIA_TYPE *mt; BYTE *buf=calloc(1,sz?sz:128); if(SUCCEEDED(IAMStreamConfig_GetStreamCaps(sc,i,&mt,buf))){ char a[64],b[64]; guid(&mt->majortype,a); guid(&mt->subtype,b); 
            if(mt->formattype.Data1==0x05589f80){ VIDEOINFOHEADER *vi=(VIDEOINFOHEADER*)mt->pbFormat; { VIDEO_STREAM_CONFIG_CAPS *c=(VIDEO_STREAM_CONFIG_CAPS*)buf; printf("    [%d] sub=%s %ldx%ld bits=%d comp=%lu avgTPF=%lld sz=%lu | caps: min=%ldx%ld max=%ldx%ld minTPF=%lld maxTPF=%lld\n",i,b,vi->bmiHeader.biWidth,vi->bmiHeader.biHeight,vi->bmiHeader.biBitCount,(unsigned long)vi->bmiHeader.biCompression,(long long)vi->AvgTimePerFrame,(unsigned long)mt->lSampleSize,c->MinOutputSize.cx,c->MinOutputSize.cy,c->MaxOutputSize.cx,c->MaxOutputSize.cy,(long long)c->MinFrameInterval,(long long)c->MaxFrameInterval);} } else printf("    [%d] sub=%s fmt=%lx\n",i,b,mt->formattype.Data1);} }
      } else printf("    (no IAMStreamConfig)\n");
    }
  }
  printf("devices: %d\n",n); return 0; }
