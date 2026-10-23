/* Reproduces the file post-processing failure: MFCreateSinkWriterFromURL for each container the app can write. */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
int main(int argc, char **argv){
  static const WCHAR *urls[] = { L"Z:\\tmp\\nvb_probe.wav", L"Z:\\tmp\\nvb_probe.mp4", L"Z:\\tmp\\nvb_probe.mp3", L"Z:\\tmp\\nvb_probe.avi" };
  HRESULT hr; unsigned i;
  CoInitializeEx(NULL,COINIT_MULTITHREADED); hr=MFStartup(MF_VERSION,0); printf("MFStartup=%08lx\n",hr);
  for(i=0;i<4;i++){
    IMFSinkWriter *w=NULL;
    hr=MFCreateSinkWriterFromURL(urls[i],NULL,NULL,&w);
    printf("%ls -> hr=%08lx\n",urls[i],hr);
    if(w) IMFSinkWriter_Release(w);
  }
  { IMFByteStream *bs=NULL; IMFMediaSink *s=NULL;
    hr=MFCreateFile(MF_ACCESSMODE_WRITE,MF_OPENMODE_DELETE_IF_EXIST,MF_FILEFLAGS_NONE,L"Z:\\tmp\\nvb_probe2.wav",&bs);
    printf("MFCreateFile hr=%08lx\n",hr);
    if(bs){ hr=MFCreateSinkWriterFromURL(NULL,bs,NULL,(IMFSinkWriter**)&s); printf("FromURL(NULL,bytestream) hr=%08lx\n",hr); }
  }
  return 0; }
