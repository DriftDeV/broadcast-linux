#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef void *(*qi_t)(unsigned); typedef int (*init_t)(void); typedef int (*enum_t)(void **, unsigned *);
typedef int (*board_t)(void *, void *);
int main(void){
  HMODULE na=LoadLibraryA("nvapi64.dll"); qi_t qi=(qi_t)GetProcAddress(na,"nvapi_QueryInterface");
  init_t ini=(init_t)qi(0x0150E828); enum_t en=(enum_t)qi(0xE5AC921F); board_t gbi=(board_t)qi(0x22D54523);
  void *g[64]; unsigned n=0; unsigned char buf[256]; int r;
  ini(); en(g,&n); printf("gpus=%u GetBoardInfo fn=%p\n",n,(void*)gbi);
  if(gbi){ memset(buf,0,sizeof buf); *(unsigned*)buf = (unsigned)(sizeof(buf)) | (1<<16); r=gbi(g[0],buf); printf("NvAPI_GPU_GetBoardInfo=%d\n",r);} 
  { HMODULE nv=LoadLibraryA("nvml.dll"); int (*init2)(void)=(int(*)(void))GetProcAddress(nv,"nvmlInit_v2"); int (*hbi)(unsigned,void**)=(int(*)(unsigned,void**))GetProcAddress(nv,"nvmlDeviceGetHandleByIndex_v2");
    int (*ser)(void*,char*,unsigned)=(int(*)(void*,char*,unsigned))GetProcAddress(nv,"nvmlDeviceGetSerial"); int (*uu)(void*,char*,unsigned)=(int(*)(void*,char*,unsigned))GetProcAddress(nv,"nvmlDeviceGetUUID");
    void *h=0; char s[128]={0}; printf("nvml=%p init=%p ser=%p\n",(void*)nv,(void*)init2,(void*)ser);
    if(init2){ r=init2(); printf("nvmlInit=%d\n",r); r=hbi(0,&h); printf("handle=%d\n",r); if(ser){ r=ser(h,s,128); printf("nvmlDeviceGetSerial=%d [%s]\n",r,s);} r=uu(h,s,128); printf("UUID=%d [%s]\n",r,s);} }
  return 0; }
