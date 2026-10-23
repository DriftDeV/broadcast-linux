#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* nvcuda.dll shim: forwards the CUDA driver API to the Wine wrapper (nvcuda_real.dll)
   and gives cuGetProcAddress the real driver semantics (best version <= requested). */
typedef int (*gp2_t)(const char *, void **, int, unsigned long long, int *);

static const int known_versions[] = {
    13050, 13040, 13030, 13020, 13010, 13000, 12090, 12080, 12060, 12050, 12040, 12030, 12020, 12010, 12000,
    11070, 11060, 11040, 11030, 11020, 11010, 11000, 10020, 10010, 10000, 9020, 9000, 8000, 7000, 6050, 6000,
    5050, 5000, 4020, 4010, 4000, 3020, 3010, 3000, 2030, 2020, 2010, 2000, 0 };

static gp2_t real_gp2;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

static void logf_(const char *fmt, ...)
{
    char b[400]; va_list ap; DWORD w; HANDLE h;
    SYSTEMTIME t; int n;
    GetLocalTime(&t);
    n = snprintf(b, sizeof(b) - 2, "%02d:%02d:%02d.%03d p%lu t%lu ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId());
    va_start(ap, fmt); vsnprintf(b + n, sizeof(b) - 2 - n, fmt, ap); va_end(ap);
    strcat(b, "\r\n");
    h = CreateFileA("Z:\\tmp\\nvcuda_shim.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, b, (DWORD)strlen(b), &w, NULL); CloseHandle(h); }
}

__declspec(dllexport) int my_cuGetProcAddress_v2(const char *, void **, int, unsigned long long, int *);
__declspec(dllexport) int my_cuGetProcAddress(const char *, void **, int, unsigned long long);

/* tracing wrappers for the calls the core makes first */
static int (*real_cuInit)(unsigned);
static int traced_cuInit(unsigned f) { int r = real_cuInit(f); logf_("cuInit(%u) = %d", f, r); return r; }

/* DXVK-NVAPI reports "...4060M" while CUDA says "...4060 Laptop GPU"; the core matches the two by name */
static int (*real_cuDeviceGetName)(char *, int, int);
static int traced_cuDeviceGetName(char *name, int len, int dev)
{
    int r = real_cuDeviceGetName(name, len, dev);
    if (r == 0 && name) {
        char *q = strstr(name, " Laptop GPU");
        if (q) strcpy(q, "M");
        logf_("cuDeviceGetName -> [%s]", name);
    }
    return r;
}


/* ---- Undocumented CUDA export table "C4G" (UUID d2688bf2-8371-e24f-9469-8177c8ccec27) ----
   One function (create a context for a GPU). Wine's nvcuda wrapper does not know it, so we call the
   native libcuda function directly through a Windows->SysV calling-convention thunk. */
#define C4G_OFFSET 0x268800ull   /* offset in libcuda.so.1 of driver 615.78.08 */
static const unsigned char C4G_UUID[16] = { 0xd2, 0x68, 0x8b, 0xf2, 0x83, 0x71, 0xe2, 0x4f, 0x94, 0x69, 0x81, 0x77, 0xc8, 0xcc, 0xec, 0x27 };
static const unsigned char C4G_PROLOGUE[11] = { 0x55, 0x31, 0xc0, 0xb9, 0x1f, 0x00, 0x00, 0x00, 0x48, 0x89, 0xe5 };
void *c4g_native_fn;
static struct { unsigned long long size; void *fn; } c4g_table;

void c4g_thunk(void);
__asm__(
    ".text\n.globl c4g_thunk\nc4g_thunk:\n"
    "  push %rbp\n  mov %rsp,%rbp\n  push %rsi\n  push %rdi\n  sub $0xA0,%rsp\n"
    "  movdqu %xmm6,0x00(%rsp)\n  movdqu %xmm7,0x10(%rsp)\n  movdqu %xmm8,0x20(%rsp)\n  movdqu %xmm9,0x30(%rsp)\n"
    "  movdqu %xmm10,0x40(%rsp)\n  movdqu %xmm11,0x50(%rsp)\n  movdqu %xmm12,0x60(%rsp)\n  movdqu %xmm13,0x70(%rsp)\n"
    "  movdqu %xmm14,0x80(%rsp)\n  movdqu %xmm15,0x90(%rsp)\n"
    "  mov %rcx,%rdi\n  mov %rdx,%rsi\n  mov %r8d,%edx\n"
    "  mov c4g_native_fn(%rip),%rax\n  call *%rax\n"
    "  movdqu 0x00(%rsp),%xmm6\n  movdqu 0x10(%rsp),%xmm7\n  movdqu 0x20(%rsp),%xmm8\n  movdqu 0x30(%rsp),%xmm9\n"
    "  movdqu 0x40(%rsp),%xmm10\n  movdqu 0x50(%rsp),%xmm11\n  movdqu 0x60(%rsp),%xmm12\n  movdqu 0x70(%rsp),%xmm13\n"
    "  movdqu 0x80(%rsp),%xmm14\n  movdqu 0x90(%rsp),%xmm15\n"
    "  add $0xA0,%rsp\n  pop %rdi\n  pop %rsi\n  pop %rbp\n  ret\n");

/* libcuda load base of one process, from its /proc/<pid>/maps (streamed: the file is large) */
static unsigned long long libcuda_base_of(const char *pid)
{
    char path[64];
    HANDLE h;
    static char buf[1 << 16];
    DWORD n = 0;
    size_t keep = 0;
    unsigned long long start = 0;
    snprintf(path, sizeof(path), "Z:\\proc\\%s\\maps", pid);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    for (;;) {
        char *line, *nl;
        if (!ReadFile(h, buf + keep, (DWORD)(sizeof(buf) - 1 - keep), &n, NULL) || !n) break;
        buf[keep + n] = 0;
        line = buf;
        while ((nl = strchr(line, '\n'))) {
            *nl = 0;
            if (strstr(line, "libcuda.so")) { start = strtoull(line, NULL, 16); goto done; }
            line = nl + 1;
        }
        keep = strlen(line);
        memmove(buf, line, keep);
    }
done:
    CloseHandle(h);
    return start;
}

/* the C4G function lives at a fixed offset of libcuda; accept a candidate only if our own memory matches */
static void *find_native_c4g(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA("Z:\\proc\\*", &fd);
    void *found = NULL;
    int tried = 0;
    if (f == INVALID_HANDLE_VALUE) { logf_("proc: cannot list"); return NULL; }
    do {
        unsigned long long base;
        unsigned char got[sizeof(C4G_PROLOGUE)];
        SIZE_T rd = 0;
        if (fd.cFileName[0] < '0' || fd.cFileName[0] > '9') continue;
        base = libcuda_base_of(fd.cFileName);
        if (!base) continue;
        tried++;
        if (ReadProcessMemory(GetCurrentProcess(), (void *)(base + C4G_OFFSET), got, sizeof(got), &rd) &&
            rd == sizeof(got) && !memcmp(got, C4G_PROLOGUE, sizeof(got))) {
            found = (void *)(base + C4G_OFFSET);
            logf_("C4G: pid %s base=%llx verified", fd.cFileName, base);
            break;
        }
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    if (!found) logf_("C4G: no verified libcuda base (%d candidates)", tried);
    return found;
}

static int (*real_cuGetExportTable)(const void **, const unsigned char *);
__declspec(dllexport) int my_cuGetExportTable(const void **tbl, const unsigned char *uuid)
{
    if (uuid && !memcmp(uuid, C4G_UUID, 16)) {
        if (!c4g_native_fn) {
            void *fn = find_native_c4g();
            if (fn && !memcmp(fn, C4G_PROLOGUE, sizeof(C4G_PROLOGUE))) c4g_native_fn = fn;
            logf_("C4G native fn=%p ok=%d", fn, c4g_native_fn != NULL);
        }
        if (c4g_native_fn) {
            c4g_table.size = 16; c4g_table.fn = (void *)c4g_thunk;
            *tbl = &c4g_table;
            return 0;
        }
        return 999;
    }
    if (!real_cuGetExportTable) {
        HMODULE h = LoadLibraryA("nvcuda_real.dll");
        if (h) real_cuGetExportTable = (int (*)(const void **, const unsigned char *))GetProcAddress(h, "cuGetExportTable");
    }
    return real_cuGetExportTable ? real_cuGetExportTable(tbl, uuid) : 3;
}


/* ---- development hook (only with NVB_DEV_EXEC=1): run commands written to /tmp/nvb_exec.cmd inside this
   Wine session, output to /tmp/nvb_exec.out. A second umu-run hangs while the app is open. ---- */
static DWORD WINAPI dev_exec_thread(LPVOID arg)
{
    for (;;) {
        HANDLE f;
        Sleep(500);
        f = CreateFileA("Z:\\tmp\\nvb_exec.cmd", GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            char cmd[1024] = {0};
            DWORD n = 0;
            ReadFile(f, cmd, sizeof(cmd) - 1, &n, NULL);
            CloseHandle(f);
            DeleteFileA("Z:\\tmp\\nvb_exec.cmd");
            while (n && (cmd[n - 1] == '\n' || cmd[n - 1] == '\r' || cmd[n - 1] == ' ')) cmd[--n] = 0;
            if (n) {
                SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
                HANDLE out = CreateFileA("Z:\\tmp\\nvb_exec.out", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL);
                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi;
                si.dwFlags = STARTF_USESTDHANDLES;
                si.hStdOutput = si.hStdError = out;
                si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                logf_("dev exec: %s", cmd);
                if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                    /* do not wait: long-running tools (the vcam bridge) must not block the queue */
                    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                } else logf_("dev exec failed %lu", GetLastError());
                /* the child inherited the output handle; keep it open for its lifetime */
            }
        }
    }
    return 0;
}


/* ---- virtual camera bridge launcher (NVB_VCAM_BRIDGE=1): keeps C:\nvb-vcam-bridge.exe running ---- */
static DWORD WINAPI bridge_thread(LPVOID arg)
{
    Sleep(10000);   /* let the core come up first */
    for (;;) {
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        char cmd[] = "C:\\nvb-vcam-bridge.exe";
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            logf_("vcam bridge started pid %lu", pi.dwProcessId);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            logf_("vcam bridge exited, restarting");
        } else logf_("vcam bridge start failed %lu", GetLastError());
        Sleep(3000);
    }
    return 0;
}

static INIT_ONCE dev_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK start_dev_hook(PINIT_ONCE o, PVOID p, PVOID *c)
{
    char v[8] = {0};
    if (GetEnvironmentVariableA("NVB_VCAM_BRIDGE", v, sizeof(v)) && v[0] == '1') {
        HANDLE t = CreateThread(NULL, 0, bridge_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
        v[0] = 0;
    }
    if (GetEnvironmentVariableA("NVB_DEV_EXEC", v, sizeof(v)) && v[0] == '1') {
        HANDLE t = CreateThread(NULL, 0, dev_exec_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
        logf_("dev exec hook started");
    }
    return TRUE;
}

static BOOL CALLBACK init_real(PINIT_ONCE o, PVOID p, PVOID *c)
{
    HMODULE h = LoadLibraryA("nvcuda_real.dll");
    if (h) real_gp2 = (gp2_t)GetProcAddress(h, "cuGetProcAddress_v2");
    return TRUE;
}

__declspec(dllexport) int my_cuGetProcAddress_v2(const char *name, void **pfn, int version,
                                                 unsigned long long flags, int *status)
{
    int i, r;
    InitOnceExecuteOnce(&once, init_real, NULL, NULL);
    InitOnceExecuteOnce(&dev_once, start_dev_hook, NULL, NULL);
    logf_("V2 name=[%s] ver=%d flags=%llu status=%p", name ? name : "(null)", version, flags, (void *)status);
    if (!real_gp2 || !name || !pfn) return 1; /* CUDA_ERROR_INVALID_VALUE */
    /* hand out our own fixed lookup functions instead of the wrapper's exact-match ones */
    if (!strcmp(name, "cuGetProcAddress_v2") || (!strcmp(name, "cuGetProcAddress") && version >= 12000)) {
        *pfn = (void *)my_cuGetProcAddress_v2; if (status) *status = 0; logf_("  -> mine v2"); return 0;
    }
    if (!strcmp(name, "cuGetProcAddress")) {
        *pfn = (void *)my_cuGetProcAddress; if (status) *status = 0; logf_("  -> mine v1"); return 0;
    }
    for (i = 0; known_versions[i]; i++) {
        unsigned long long f;
        int st;
        if (known_versions[i] > version) continue;
        for (f = flags; ; f = 0) {
            void *p = NULL;
            r = real_gp2(name, &p, known_versions[i], f, &st);
            if (r == 0 && p) {
                if (!strcmp(name, "cuGetExportTable")) { p = (void *)my_cuGetExportTable; }
                if (!strcmp(name, "cuDeviceGetName")) { real_cuDeviceGetName = (int (*)(char *, int, int))p; p = (void *)traced_cuDeviceGetName; }
                if (!strcmp(name, "cuInit")) { real_cuInit = (int (*)(unsigned))p; p = (void *)traced_cuInit; }
                *pfn = p;
                if (status) *status = 0;
                logf_("  -> found@%d flags=%llu", known_versions[i], f);
                return 0;
            }
            if (f == 0) break;
        }
    }
    *pfn = NULL;
    if (status) *status = 1; /* CU_GET_PROC_ADDRESS_SYMBOL_NOT_FOUND */
    logf_("  -> NOT FOUND");
    return 0;
}

__declspec(dllexport) int my_cuGetProcAddress(const char *name, void **pfn, int version, unsigned long long flags)
{
    int st;
    logf_("V1 call");
    return my_cuGetProcAddress_v2(name, pfn, version, flags, &st);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { return TRUE; }
