#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* nvapi64.dll shim: DXVK-NVAPI (nvapi64_real.dll) plus the few functions it lacks that NVIDIA Broadcast needs. */
typedef void *(__cdecl *qi_t)(unsigned);

static qi_t real_qi;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_real(PINIT_ONCE o, PVOID p, PVOID *c)
{
    HMODULE h = LoadLibraryA("nvapi64_real.dll");
    if (h) real_qi = (qi_t)GetProcAddress(h, "nvapi_QueryInterface");
    return TRUE;
}

/* NvAPI_GPU_GetSerialNumber(NvPhysicalGpuHandle, NvAPI_ShortString serial) -> NVAPI_OK */
static int __cdecl my_GetSerialNumber(void *gpu, char *serial)
{
    if (!serial) return -5; /* NVAPI_INVALID_ARGUMENT */
    strcpy(serial, "0000000000000");
    return 0;
}

__declspec(dllexport) void *__cdecl nvapi_QueryInterface(unsigned id)
{
    void *p = NULL;
    InitOnceExecuteOnce(&once, init_real, NULL, NULL);
    if (real_qi) p = real_qi(id);
    if (!p && id == 0x14B83A5Fu) p = (void *)my_GetSerialNumber;
    return p;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { return TRUE; }
