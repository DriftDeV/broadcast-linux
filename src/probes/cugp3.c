#include <windows.h>
#include <stdio.h>
typedef int (*fGP2)(const char*, void**, int, unsigned long long, int*);
int main(void){
  HMODULE h=LoadLibraryA("nvcuda.dll"); fGP2 gp2=(fGP2)GetProcAddress(h,"cuGetProcAddress_v2");
  struct {const char*n; int v;} t[]={{"cuInit",2000},{"cuInit",2010},{"cuInit",1000},{"cuCtxCreate",2000},{"cuCtxCreate",3020},{"cuCtxCreate",11040},{"cuCtxCreate",12050},{"cuCtxCreate",12060},{"cuCtxCreate",13000},{"cuCtxGetCurrent",4000},{"cuCtxGetCurrent",12000}};
  int i; for(i=0;i<(int)(sizeof t/sizeof t[0]);i++){ void*p=0; int st=-9; int r=gp2(t[i].n,&p,t[i].v,0,&st); printf("%s@%d r=%d p=%p st=%d\n",t[i].n,t[i].v,r,p,st);} return 0; }
