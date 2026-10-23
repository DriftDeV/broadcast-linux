#include <windows.h>
#include <stdio.h>
typedef void *(*qi_t)(unsigned);
typedef int (*init_t)(void); typedef int (*enum_t)(void **, unsigned *);
typedef int (*bus_t)(void *, unsigned *); typedef int (*pci_t)(void *, unsigned *, unsigned *, unsigned *, unsigned *);
typedef int (*name_t)(void *, char *);
typedef int (*cuInit_t)(unsigned); typedef int (*cuAttr_t)(int *, int, int); typedef int (*cuBus_t)(char *, int, int);
typedef int (*cuUuid_t)(unsigned char *, int);
int main(void){
  HMODULE na=LoadLibraryA("nvapi64.dll"), cu=LoadLibraryA("nvcuda.dll"); qi_t qi=(qi_t)GetProcAddress(na,"nvapi_QueryInterface");
  init_t ini=(init_t)qi(0x0150E828); enum_t en=(enum_t)qi(0xE5AC921F); bus_t bid=(bus_t)qi(0x1BE0B8E5), slot=(bus_t)qi(0x2A0A350F); pci_t pi=(pci_t)qi(0x2DDFB66E); name_t nm=(name_t)qi(0xCEEE8E9F);
  void *g[64]; unsigned n=0, b=0, s=0, d1=0,d2=0,d3=0,d4=0; char name[256]={0}; int r;
  r=ini(); printf("NvAPI_Initialize=%d\n",r); r=en(g,&n); printf("EnumPhysicalGPUs=%d n=%u\n",r,n);
  if(n){ r=bid(g[0],&b); printf("BusId=%d b=%u\n",r,b); r=slot(g[0],&s); printf("BusSlotId=%d s=%u\n",r,s); r=pi(g[0],&d1,&d2,&d3,&d4); printf("PCIIdent=%d dev=%08x sub=%08x rev=%08x ext=%08x\n",r,d1,d2,d3,d4); r=nm(g[0],name); printf("FullName=%d [%s]\n",r,name);} 
  { cuInit_t ci=(cuInit_t)GetProcAddress(cu,"cuInit"); cuAttr_t ga=(cuAttr_t)GetProcAddress(cu,"cuDeviceGetAttribute"); cuBus_t gb=(cuBus_t)GetProcAddress(cu,"cuDeviceGetPCIBusId"); cuUuid_t gu=(cuUuid_t)GetProcAddress(cu,"cuDeviceGetUuid");
    int bus=-1,dev=-1,dom=-1; char pb[64]={0}; unsigned char u[16]={0}; int i; ci(0); ga(&bus,33,0); ga(&dev,34,0); ga(&dom,50,0); gb(pb,64,0); gu(u,0);
    printf("CUDA bus=%d dev=%d domain=%d pciBusId=%s\nCUDA uuid=",bus,dev,dom,pb); for(i=0;i<16;i++) printf("%02x",u[i]); puts(""); }
  return 0; }
