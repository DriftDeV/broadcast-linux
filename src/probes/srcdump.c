/* Usage: srcdump.exe <path> [order]  (order e.g. 01 = read stream 0 fully then stream 1, no ANY_STREAM)
   Reads a file through Wine's source reader (native types, no decoding) with ReadSample(ANY_STREAM) until the first
   end of stream, then reads each stream explicitly, and prints per-stream sample counts. Log: Z:\tmp\srcdump.out */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
int main(int argc,char **argv){
  WCHAR url[512]; IMFSourceReader *r=NULL; HRESULT hr; unsigned cnt[4]={0}; LONGLONG last[4]={0}; int phase; DWORD s;
  freopen("Z:\\tmp\\srcdump.out","w",stdout); setvbuf(stdout,NULL,_IONBF,0);
  MultiByteToWideChar(CP_UTF8,0,argv[1],-1,url,512);
  CoInitializeEx(NULL,COINIT_MULTITHREADED); MFStartup(MF_VERSION,0);
  hr=MFCreateSourceReaderFromURL(url,NULL,&r); printf("open hr=%08lx\n",hr);
  if(argc>2 && argv[2][0]=='t'){ /* mode t: explicit reads, always the stream whose last timestamp is lowest */
    unsigned n[2]={0,0}; LONGLONG l[2]={0,0}; int ended[2]={0,0};
    while(!(ended[0]&&ended[1])){ DWORD st=(!ended[0]&&(ended[1]||l[0]<=l[1]))?0:1; DWORD si,fl; LONGLONG ts; IMFSample *smp=NULL;
      hr=IMFSourceReader_ReadSample(r,st,0,&si,&fl,&ts,&smp); if(FAILED(hr)){ printf("stream %lu hr=%08lx\n",st,hr); break; }
      if(smp){ n[st]++; l[st]=ts; IMFSample_Release(smp); } if(fl&MF_SOURCE_READERF_ENDOFSTREAM) ended[st]=1; }
    printf("interleaved explicit: s0=%u@%lldms s1=%u@%lldms\n",n[0],l[0]/10000,n[1],l[1]/10000); return 0; }
  if(argc>2){ /* mode 2: explicit streams only, audio first then video (or the order given) */
    int order[2]={argv[2][0]-'0',argv[2][1]-'0'}; int i; for(i=0;i<2;i++){ DWORD st=order[i]; unsigned n=0; LONGLONG l=0; for(;;){ DWORD si,fl; LONGLONG ts; IMFSample *smp=NULL;
        hr=IMFSourceReader_ReadSample(r,st,0,&si,&fl,&ts,&smp); if(FAILED(hr)){ printf("stream %lu hr=%08lx\n",st,hr); break; } if(smp){ n++; l=ts; IMFSample_Release(smp); } if(fl&MF_SOURCE_READERF_ENDOFSTREAM) break; }
      printf("explicit-only stream %lu: %u samples, last %lldms\n",st,n,l/10000); }
    return 0; }
  for(;;){ DWORD si,fl; LONGLONG ts; IMFSample *smp=NULL;
    hr=IMFSourceReader_ReadSample(r,MF_SOURCE_READER_ANY_STREAM,0,&si,&fl,&ts,&smp); if(FAILED(hr)){ printf("ReadSample hr=%08lx\n",hr); break; }
    if(smp){ if(si<4){ cnt[si]++; last[si]=ts; } IMFSample_Release(smp); }
    if(fl&MF_SOURCE_READERF_ENDOFSTREAM){ printf("ANY: EOS actual=%lx after s0=%u@%lldms s1=%u@%lldms\n",si,cnt[0],last[0]/10000,cnt[1],last[1]/10000); break; } }
  for(phase=0;phase<2;phase++){ s=phase; { unsigned extra=0; for(;;){ DWORD si,fl; LONGLONG ts; IMFSample *smp=NULL;
      hr=IMFSourceReader_ReadSample(r,s,0,&si,&fl,&ts,&smp); if(FAILED(hr)){ printf("stream %lu ReadSample hr=%08lx\n",s,hr); break; }
      if(smp){ extra++; cnt[s]++; last[s]=ts; IMFSample_Release(smp); }
      if(fl&MF_SOURCE_READERF_ENDOFSTREAM){ break; } }
    printf("explicit stream %lu: +%u samples -> total s%lu=%u@%lldms\n",s,extra,s,cnt[s],last[s]/10000); } }
  return 0; }
