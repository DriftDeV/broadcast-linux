/* Dumps the native media types the Wine source reader reports for a file (what the core's codec check sees). */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <wchar.h>
int main(int argc, char **argv){
  WCHAR url[512]; IMFSourceReader *r=NULL; HRESULT hr; DWORD s;
  if(argc<2) return 1;
  MultiByteToWideChar(CP_UTF8,0,argv[1],-1,url,512);
  CoInitializeEx(NULL,COINIT_MULTITHREADED); MFStartup(MF_VERSION,0);
  hr=MFCreateSourceReaderFromURL(url,NULL,&r); printf("MFCreateSourceReaderFromURL hr=%08lx\n",hr);
  if(FAILED(hr)) return 1;
  for(s=0;;s++){
    DWORD t;
    for(t=0;;t++){
      IMFMediaType *mt=NULL; GUID maj={0},sub={0}; UINT32 w=0,h=0;
      hr=IMFSourceReader_GetNativeMediaType(r,s,t,&mt);
      if(FAILED(hr)){ if(t==0) printf("stream %lu: GetNativeMediaType(0) hr=%08lx\n",s,hr); break; }
      IMFMediaType_GetGUID(mt,&MF_MT_MAJOR_TYPE,&maj); IMFMediaType_GetGUID(mt,&MF_MT_SUBTYPE,&sub);
      { UINT64 fs=0; if(SUCCEEDED(IMFMediaType_GetUINT64(mt,&MF_MT_FRAME_SIZE,&fs))){ w=fs>>32; h=(UINT32)fs; } }
      printf("stream %lu type %lu: major=%08lx sub=%08lx-%04x-%04x %ux%u\n",s,t,maj.Data1,sub.Data1,sub.Data2,sub.Data3,w,h);
      IMFMediaType_Release(mt);
    }
    if(s>6) break;
    if(hr==MF_E_INVALIDSTREAMNUMBER) break;
  }
  return 0; }
