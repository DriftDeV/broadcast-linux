/* Exercises nvb_media.dll the way the core does: MFTEnumEx -> ActivateObject -> types -> PCM in, raw AAC out.
   Writes ADTS to Z:\tmp\aacprobe.aac, the same audio muxed by nvb_mp4_create to Z:\tmp\aacprobe.m4a (check with ffprobe), and a log
   to Z:\tmp\aacprobe.out. */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <math.h>
static IMFSinkWriter *g_w;
static void adts(FILE *f, const BYTE *d, int n, int ridx, int ch){
  BYTE h[7]; int len=n+7;
  h[0]=0xFF; h[1]=0xF1; h[2]=(1<<6)|(ridx<<2)|(ch>>2); h[3]=((ch&3)<<6)|(len>>11); h[4]=(len>>3)&0xFF; h[5]=((len&7)<<5)|0x1F; h[6]=0xFC;
  fwrite(h,1,7,f); fwrite(d,1,n,f); }
int main(int argc,char **argv){
  HRESULT hr; IMFActivate **act=NULL; UINT32 n=0; IMFTransform *t=NULL; IMFMediaType *it=NULL,*ot=NULL;
  MFT_REGISTER_TYPE_INFO out={0}; HMODULE m; HRESULT (*reg)(void); FILE *aac; int packets=0,bytes=0; LONGLONG s;
  freopen("Z:\\tmp\\aacprobe.out","w",stdout); setvbuf(stdout,NULL,_IONBF,0);
  CoInitializeEx(NULL,COINIT_MULTITHREADED); MFStartup(MF_VERSION,0);
  m=LoadLibraryA("Z:\\home\\drift\\git\\broadcast-linux\\build\\nvb_media.dll"); printf("LoadLibrary=%p err=%lu\n",(void*)m,GetLastError());
  reg=(void*)GetProcAddress(m,"nvb_aac_register"); hr=reg(); printf("register hr=%08lx\n",hr);
  out.guidMajorType=MFMediaType_Audio; out.guidSubtype=MFAudioFormat_AAC;
  hr=MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER,MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_LOCALMFT|MFT_ENUM_FLAG_SORTANDFILTER,NULL,&out,&act,&n); printf("MFTEnumEx hr=%08lx count=%u\n",hr,n);
  if(FAILED(hr)||!n) return 1;
  hr=IMFActivate_ActivateObject(act[0],&IID_IMFTransform,(void**)&t); printf("ActivateObject hr=%08lx\n",hr); if(FAILED(hr)) return 1;
  hr=IMFTransform_GetOutputAvailableType(t,0,0,&ot); printf("GetOutputAvailableType(0) hr=%08lx\n",hr);
  hr=IMFTransform_SetOutputType(t,0,ot,0); printf("SetOutputType hr=%08lx\n",hr);
  hr=IMFTransform_GetInputAvailableType(t,0,0,&it); printf("GetInputAvailableType(0) hr=%08lx\n",hr);
  hr=IMFTransform_SetInputType(t,0,it,0); printf("SetInputType hr=%08lx\n",hr);
  { IMFMediaType *cur=NULL; UINT32 sz=0; BYTE ud[32]; hr=IMFTransform_GetOutputCurrentType(t,0,&cur); printf("GetOutputCurrentType hr=%08lx\n",hr);
    if(cur){ hr=IMFMediaType_GetBlob(cur,&MF_MT_USER_DATA,ud,sizeof(ud),&sz); printf("user data hr=%08lx size=%u asc=%02x%02x\n",hr,sz,ud[12],ud[13]); } }
  IMFTransform_ProcessMessage(t,MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0); IMFTransform_ProcessMessage(t,MFT_MESSAGE_NOTIFY_START_OF_STREAM,0);
  { HRESULT (*mk)(LPCWSTR,IMFSinkWriter**)=(void*)GetProcAddress(m,"nvb_mp4_create"); IMFMediaType *cur=NULL; DWORD si=0;
    void (*sl)(void*)=(void*)GetProcAddress(m,"nvb_media_set_logger"); (void)sl;
    hr=mk(L"Z:\\tmp\\aacprobe.m4a",&g_w); printf("nvb_mp4_create hr=%08lx\n",hr);
    IMFTransform_GetOutputCurrentType(t,0,&cur);
    hr=IMFSinkWriter_AddStream(g_w,cur,&si); printf("AddStream hr=%08lx\n",hr);
    hr=IMFSinkWriter_SetInputMediaType(g_w,si,cur,NULL); printf("SetInputMediaType hr=%08lx\n",hr);
    hr=IMFSinkWriter_BeginWriting(g_w); printf("BeginWriting hr=%08lx\n",hr); }
  aac=fopen("Z:\\tmp\\aacprobe.aac","wb");
  for(s=0;s<200;s++){                      /* 200 x 10 ms = 2 s of 440 Hz, 48 kHz stereo */
    IMFSample *smp; IMFMediaBuffer *b; BYTE *p; short *w; int i; MFT_OUTPUT_DATA_BUFFER ob={0}; DWORD st;
    MFCreateMemoryBuffer(480*4,&b); IMFMediaBuffer_Lock(b,&p,NULL,NULL); w=(short*)p;
    for(i=0;i<480;i++){ short v=(short)(8000*sin(2*3.14159265*440*((s*480+i)/48000.0))); w[2*i]=v; w[2*i+1]=v; }
    IMFMediaBuffer_Unlock(b); IMFMediaBuffer_SetCurrentLength(b,480*4);
    MFCreateSample(&smp); IMFSample_AddBuffer(smp,b); IMFSample_SetSampleTime(smp,s*100000LL); IMFSample_SetSampleDuration(smp,100000);
    hr=IMFTransform_ProcessInput(t,0,smp,0); if(FAILED(hr)){ printf("ProcessInput %lld hr=%08lx\n",s,hr); return 1; }
    IMFSample_Release(smp); IMFMediaBuffer_Release(b);
    for(;;){ IMFSample *os; IMFMediaBuffer *obuf; BYTE *q; DWORD len,mx; MFCreateSample(&os); MFCreateMemoryBuffer(8192,&obuf); IMFSample_AddBuffer(os,obuf); ob.pSample=os;
      hr=IMFTransform_ProcessOutput(t,0,1,&ob,&st);
      if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT){ IMFSample_Release(os); IMFMediaBuffer_Release(obuf); break; }
      if(FAILED(hr)){ printf("ProcessOutput hr=%08lx\n",hr); return 1; }
      IMFMediaBuffer_Lock(obuf,&q,&mx,&len); adts(aac,q,len,3,2); packets++; bytes+=len; IMFMediaBuffer_Unlock(obuf); hr=IMFSinkWriter_WriteSample(g_w,0,os); if(FAILED(hr)) printf("WriteSample hr=%08lx\n",hr); IMFSample_Release(os); IMFMediaBuffer_Release(obuf); }
  }
  IMFTransform_ProcessMessage(t,MFT_MESSAGE_COMMAND_DRAIN,0);
  for(;;){ IMFSample *os; IMFMediaBuffer *obuf; BYTE *q; DWORD len,mx; MFT_OUTPUT_DATA_BUFFER ob={0}; DWORD st; MFCreateSample(&os); MFCreateMemoryBuffer(8192,&obuf); IMFSample_AddBuffer(os,obuf); ob.pSample=os;
    hr=IMFTransform_ProcessOutput(t,0,1,&ob,&st); if(FAILED(hr)) break;
    IMFMediaBuffer_Lock(obuf,&q,&mx,&len); adts(aac,q,len,3,2); packets++; bytes+=len; IMFMediaBuffer_Unlock(obuf); IMFSinkWriter_WriteSample(g_w,0,os); }
  hr=IMFSinkWriter_Finalize(g_w); printf("Finalize hr=%08lx\n",hr);
  fclose(aac); printf("packets=%d bytes=%d (drain end hr=%08lx)\n",packets,bytes,hr); return 0; }
