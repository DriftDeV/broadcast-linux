#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#define _GNU_SOURCE
#include <link.h>
typedef int (*cuInit_t)(unsigned);
typedef int (*get_t)(const void **, const unsigned char *);
int main(void){
  void *h=dlopen("libcuda.so.1",RTLD_NOW); cuInit_t ci=dlsym(h,"cuInit"); get_t g=dlsym(h,"cuGetExportTable");
  /* UUID as printed by wine: {d2688bf2-8371-e24f-9469-8177c8ccec27} -> raw bytes in memory order */
  unsigned char u[16]={0xf2,0x8b,0x68,0xd2,0x71,0x83,0x4f,0xe2,0x94,0x69,0x81,0x77,0xc8,0xcc,0xec,0x27};
  unsigned char u2[16]={0xd2,0x68,0x8b,0xf2,0x83,0x71,0xe2,0x4f,0x94,0x69,0x81,0x77,0xc8,0xcc,0xec,0x27};
  const void *t=0; int r,i,k;
  ci(0);
  for(k=0;k<2;k++){ t=0; r=g(&t,k?u2:u); printf("variant %d: r=%d table=%p\n",k,r,t);
    if(r==0&&t){ const unsigned long *p=(const unsigned long*)t; printf(" size=%lu\n",p[0]&0xffffffff); for(i=1;i<=(int)(p[0]&0xffffffff)/8-1&&i<24;i++) { Dl_info di; dladdr((void*)p[i],&di); printf("  [%d] %p  lib=%s off=0x%lx sym=%s\n",i,(void*)p[i],di.dli_fname,(unsigned long)((char*)p[i]-(char*)di.dli_fbase),di.dli_sname?di.dli_sname:"?"); } } }
  return 0; }
