#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <stdio.h>
int main(void){
  IMFAttributes *a=NULL; IMFActivate **d=NULL; UINT32 n=0; HRESULT hr;
  CoInitializeEx(NULL,COINIT_MULTITHREADED); hr=MFStartup(MF_VERSION,0); printf("MFStartup=%08lx\n",hr);
  hr=MFCreateAttributes(&a,1); IMFAttributes_SetGUID(a,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  hr=MFEnumDeviceSources(a,&d,&n); printf("MFEnumDeviceSources hr=%08lx count=%u\n",hr,n);
  { UINT32 i; for(i=0;i<n;i++){ WCHAR *nm=NULL; UINT32 l=0; IMFActivate_GetAllocatedString(d[i],&MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&nm,&l); printf(" dev %u: %ls\n",i,nm?nm:L"?"); } }
  return 0; }
