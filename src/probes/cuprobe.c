#include <windows.h>
#include <stdio.h>
typedef int (*fInit)(unsigned); typedef int (*fCount)(int*); typedef int (*fGet)(int*,int);
typedef int (*fName)(char*,int,int); typedef int (*fAttr)(int*,int,int); typedef int (*fVer)(int*);
typedef int (*fCtx)(void**,unsigned,int);
#define L(n) f##n##_t
int main(void){
  HMODULE h = LoadLibraryA("nvcuda.dll"); int r, c = -1, d = -1, v = -1; char nm[128] = "";
  void *ctx = 0;
  printf("LoadLibrary nvcuda: %p err=%lu\n", h, GetLastError()); if(!h) return 1;
  fInit cuInit = (fInit)GetProcAddress(h,"cuInit"); fCount cuDeviceGetCount=(fCount)GetProcAddress(h,"cuDeviceGetCount");
  fGet cuDeviceGet=(fGet)GetProcAddress(h,"cuDeviceGet"); fName cuDeviceGetName=(fName)GetProcAddress(h,"cuDeviceGetName");
  fVer cuDriverGetVersion=(fVer)GetProcAddress(h,"cuDriverGetVersion"); fCtx cuCtxCreate=(fCtx)GetProcAddress(h,"cuCtxCreate_v2");
  fAttr cuDeviceGetAttribute=(fAttr)GetProcAddress(h,"cuDeviceGetAttribute");
  printf("syms: init=%p count=%p get=%p name=%p ver=%p ctx=%p attr=%p\n",cuInit,cuDeviceGetCount,cuDeviceGet,cuDeviceGetName,cuDriverGetVersion,cuCtxCreate,cuDeviceGetAttribute);
  if(cuInit){ r=cuInit(0); printf("cuInit=%d\n",r);} 
  if(cuDriverGetVersion){ r=cuDriverGetVersion(&v); printf("cuDriverGetVersion=%d ver=%d\n",r,v);} 
  if(cuDeviceGetCount){ r=cuDeviceGetCount(&c); printf("cuDeviceGetCount=%d count=%d\n",r,c);} 
  if(cuDeviceGet){ r=cuDeviceGet(&d,0); printf("cuDeviceGet=%d dev=%d\n",r,d);} 
  if(cuDeviceGetName){ r=cuDeviceGetName(nm,127,d); printf("cuDeviceGetName=%d [%s]\n",r,nm);} 
  if(cuDeviceGetAttribute){ int m=-1,n=-1; r=cuDeviceGetAttribute(&m,75,d); cuDeviceGetAttribute(&n,76,d); printf("cc=%d.%d r=%d\n",m,n,r);} 
  if(cuCtxCreate){ r=cuCtxCreate(&ctx,0,d); printf("cuCtxCreate=%d ctx=%p\n",r,ctx);} 
  return 0; }
