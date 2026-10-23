/* Decodes the audio stream of a file through Wine's source reader (PCM out) and prints how much it delivers.
   Usage: aacdecode.exe <path>   log: Z:\tmp\aacdecode.out */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
int main(int argc,char **argv){
  WCHAR url[512]; IMFSourceReader *r=NULL; IMFMediaType *mt=NULL; HRESULT hr; unsigned long long bytes=0; unsigned n=0;
  freopen("Z:\\tmp\\aacdecode.out","w",stdout); setvbuf(stdout,NULL,_IONBF,0);
  MultiByteToWideChar(CP_UTF8,0,argv[1],-1,url,512);
  CoInitializeEx(NULL,COINIT_MULTITHREADED); MFStartup(MF_VERSION,0);
  hr=MFCreateSourceReaderFromURL(url,NULL,&r); printf("open hr=%08lx\n",hr);
  MFCreateMediaType(&mt); IMFMediaType_SetGUID(mt,&MF_MT_MAJOR_TYPE,&MFMediaType_Audio); IMFMediaType_SetGUID(mt,&MF_MT_SUBTYPE,&MFAudioFormat_PCM);
  hr=IMFSourceReader_SetCurrentMediaType(r,0,NULL,mt); printf("SetCurrentMediaType(PCM) hr=%08lx\n",hr);
  for(;;){ DWORD si,fl; LONGLONG ts; IMFSample *s=NULL;
    hr=IMFSourceReader_ReadSample(r,0,0,&si,&fl,&ts,&s); if(FAILED(hr)){ printf("ReadSample hr=%08lx\n",hr); break; }
    if(s){ IMFMediaBuffer *b; DWORD len=0; IMFSample_ConvertToContiguousBuffer(s,&b); IMFMediaBuffer_GetCurrentLength(b,&len); bytes+=len; n++; IMFMediaBuffer_Release(b); IMFSample_Release(s); }
    if(fl&MF_SOURCE_READERF_ENDOFSTREAM){ printf("EOS flags=%lx\n",fl); break; } }
  { IMFMediaType *cur=NULL; UINT32 ch=2,rate=48000,bits=16; IMFSourceReader_GetCurrentMediaType(r,0,&cur);
    if(cur){ IMFMediaType_GetUINT32(cur,&MF_MT_AUDIO_NUM_CHANNELS,&ch); IMFMediaType_GetUINT32(cur,&MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate); IMFMediaType_GetUINT32(cur,&MF_MT_AUDIO_BITS_PER_SAMPLE,&bits); }
    printf("samples=%u bytes=%llu -> %.3f s (ch=%u rate=%u bits=%u)\n",n,bytes,(double)bytes/(ch*rate*bits/8),ch,rate,bits); }
  return 0; }
