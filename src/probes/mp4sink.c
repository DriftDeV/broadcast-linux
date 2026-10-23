/* Mirrors the core's mp4 mux setup: H264 video (with sequence header) + raw AAC audio -> sink writer -> BeginWriting.
   Log: Z:\tmp\mp4sink.out */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
static const BYTE seq[33]={0,0,0,1,0x67,0x64,0,0x1f,0xac,0x2b,0x20,0x0a,0,0xb7,0x60,0x22,0,0,7,0xd0,0,1,0xd4,0xc1,8,0,0,0,1,0x68,0xeb,0x8f,0};
int main(void){
  IMFSinkWriter *w=NULL; IMFMediaType *v=NULL,*a=NULL; DWORD vi=0,ai=0; HRESULT hr; BYTE ud[14]={0,0,0x29,0,0,0,0,0,0,0,0,0,0x11,0x90};
  freopen("Z:\\tmp\\mp4sink.out","w",stdout); setvbuf(stdout,NULL,_IONBF,0);
  CoInitializeEx(NULL,COINIT_MULTITHREADED); MFStartup(MF_VERSION,0);
  hr=MFCreateSinkWriterFromURL(L"X:\\Documenti\\mp4sink_probe.mp4",NULL,NULL,&w); printf("create hr=%08lx\n",hr); if(FAILED(hr)) return 1;
  MFCreateMediaType(&v); IMFMediaType_SetGUID(v,&MF_MT_MAJOR_TYPE,&MFMediaType_Video); IMFMediaType_SetGUID(v,&MF_MT_SUBTYPE,&MFVideoFormat_H264);
  IMFMediaType_SetUINT64(v,&MF_MT_FRAME_SIZE,((UINT64)1280<<32)|720); IMFMediaType_SetUINT64(v,&MF_MT_FRAME_RATE,((UINT64)30<<32)|1);
  IMFMediaType_SetUINT32(v,&MF_MT_AVG_BITRATE,2764800); IMFMediaType_SetUINT32(v,&MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
  IMFMediaType_SetUINT64(v,&MF_MT_PIXEL_ASPECT_RATIO,((UINT64)1<<32)|1);
  IMFMediaType_SetBlob(v,&MF_MT_MPEG_SEQUENCE_HEADER,seq,33);
  hr=IMFSinkWriter_AddStream(w,v,&vi); printf("AddStream(video) hr=%08lx idx=%lu\n",hr,vi);
  hr=IMFSinkWriter_SetInputMediaType(w,vi,v,NULL); printf("SetInputMediaType(video) hr=%08lx\n",hr);
  MFCreateMediaType(&a); IMFMediaType_SetGUID(a,&MF_MT_MAJOR_TYPE,&MFMediaType_Audio); IMFMediaType_SetGUID(a,&MF_MT_SUBTYPE,&MFAudioFormat_AAC);
  IMFMediaType_SetUINT32(a,&MF_MT_AUDIO_NUM_CHANNELS,2); IMFMediaType_SetUINT32(a,&MF_MT_AUDIO_SAMPLES_PER_SECOND,48000); IMFMediaType_SetUINT32(a,&MF_MT_AUDIO_BITS_PER_SAMPLE,16);
  IMFMediaType_SetUINT32(a,&MF_MT_AUDIO_BLOCK_ALIGNMENT,1); IMFMediaType_SetUINT32(a,&MF_MT_AUDIO_AVG_BYTES_PER_SECOND,24000); IMFMediaType_SetUINT32(a,&MF_MT_AAC_PAYLOAD_TYPE,0);
  IMFMediaType_SetUINT32(a,&MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION,0x29); IMFMediaType_SetBlob(a,&MF_MT_USER_DATA,ud,14);
  hr=IMFSinkWriter_AddStream(w,a,&ai); printf("AddStream(audio) hr=%08lx idx=%lu\n",hr,ai);
  hr=IMFSinkWriter_SetInputMediaType(w,ai,a,NULL); printf("SetInputMediaType(audio) hr=%08lx\n",hr);
  hr=IMFSinkWriter_BeginWriting(w); printf("BeginWriting hr=%08lx\n",hr);
  return 0; }
