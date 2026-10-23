#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
int main(void){
  IMMDeviceEnumerator *e; IMMDeviceCollection *c; UINT n,i; int fl;
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator,NULL,CLSCTX_ALL,&IID_IMMDeviceEnumerator,(void**)&e))) { puts("no enumerator"); return 1; }
  for (fl=0; fl<2; fl++) {
    IMMDeviceEnumerator_EnumAudioEndpoints(e, fl?eCapture:eRender, DEVICE_STATEMASK_ALL, &c);
    IMMDeviceCollection_GetCount(c,&n); printf("%s: %u\n", fl?"CAPTURE":"RENDER", n);
    for(i=0;i<n;i++){ IMMDevice *d; IPropertyStore *p; PROPVARIANT v; LPWSTR id; DWORD st;
      IMMDeviceCollection_Item(c,i,&d); IMMDevice_GetId(d,&id); IMMDevice_GetState(d,&st);
      IMMDevice_OpenPropertyStore(d,STGM_READ,&p); PropVariantInit(&v);
      IPropertyStore_GetValue(p,&PKEY_Device_FriendlyName,&v);
      printf("  [%lu] %ls | %ls\n", st, v.pwszVal?v.pwszVal:L"?", id); }
  }
  return 0; }
