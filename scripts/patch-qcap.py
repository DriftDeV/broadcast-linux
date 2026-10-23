#!/usr/bin/env python3
"""Patch Wine's qcap (webcam capture) inside a GE-Proton runner so NVIDIA Broadcast accepts the webcam.

Wine 11 qcap has two gaps that the Broadcast core does not tolerate:
  1. VIDEOINFOHEADER.AvgTimePerFrame is never set (0): the core derives the frame rate from it.
     -> qcap.so  : in the (inlined) fill_caps, also store AvgTimePerFrame = MinFrameInterval.
  2. Captured samples carry no time stamp: IMediaSample::GetTime fails and the core drops every frame.
     -> qcap.dll : call IMediaSample::SetTime(sample, &stream_time, NULL) right before IMemInputPin::Receive.
                   stream_time is relative to the first frame (absolute times make the renderer wait forever).

Both patches write small code caves into zero padding that is mapped executable, and are verified byte for byte,
so the script refuses to touch a build that differs from GE-Proton11-11.  Backups: <file>.orig.  Idempotent.
"""
import os, shutil, struct, sys

RUNNER = os.environ.get("NVB_RUNNER", os.path.expanduser("~/.local/share/Steam/compatibilitytools.d/GE-Proton11-11-x86_64"))
SO  = os.path.join(RUNNER, "files/lib/wine/x86_64-unix/qcap.so")
DLL = os.path.join(RUNNER, "files/lib/wine/x86_64-windows/qcap.dll")


def backup(path):
    if not os.path.exists(path + ".orig"):
        shutil.copy2(path, path + ".orig")


def patch_so():
    d = bytearray(open(SO, "rb").read())
    site, cave = 0x29F8, 0x2E00          # file offsets == virtual addresses in this ELF
    if d[site] == 0xE9:
        print("qcap.so already patched"); return
    assert d[site:site + 6].hex() == "898680000000", "qcap.so: unexpected bytes at fill_caps site (other GE build?)"
    assert set(d[cave:cave + 32]) == {0}, "qcap.so: code cave is not free"
    backup(SO)
    code = bytes.fromhex("898680000000")             # original: mov [rsi+0x80], eax   (dwBitRate)
    code += bytes.fromhex("44899e88000000")          # mov [rsi+0x88], r11d            (AvgTimePerFrame = min interval)
    code += b"\xe9" + struct.pack("<i", (site + 6) - (cave + len(code) + 5))
    d[cave:cave + len(code)] = code
    d[site:site + 6] = b"\xe9" + struct.pack("<i", cave - (site + 5)) + b"\x90"
    open(SO, "wb").write(d)
    print("qcap.so patched")


def patch_dll():
    d = bytearray(open(DLL, "rb").read())
    text_va, text_raw = 0x180001000, 0x1000
    off = lambda va: text_raw + (va - text_va)
    site_a, ret_a, cave_a = 0x180013CCE, 0x180013CD6, 0x180017C40
    site_b, ret_b, cave_b = 0x180013D92, 0x180013D99, 0x180017C80
    if d[off(site_b)] == 0xE9:
        print("qcap.dll already patched"); return
    assert d[off(site_a):off(site_a) + 8].hex() == "488d45e0488945d8", "qcap.dll: unexpected bytes at stream_thread start"
    assert d[off(site_b):off(site_b) + 7].hex() == "488b8b60040000", "qcap.dll: unexpected bytes before Receive"
    assert set(d[off(cave_a):off(cave_a) + 160]) == {0}, "qcap.dll: code cave is not free"
    backup(DLL)
    rel = lambda end, to: struct.pack("<i", to - end)

    a = bytes.fromhex("488d45e0") + bytes.fromhex("488945d8") + bytes.fromhex("48c745c800000000")   # original + [rbp-0x38]=0
    a += b"\xe9" + rel(cave_a + len(a) + 5, ret_a)

    b = bytes.fromhex("48b80800fe7f00000000")        # mov rax, 0x7ffe0008   (KUSER_SHARED_DATA.InterruptTime, 100 ns)
    b += bytes.fromhex("488b00")                     # mov rax, [rax]
    b += bytes.fromhex("488b4dc8")                   # mov rcx, [rbp-0x38]   (time of the first frame)
    b += bytes.fromhex("4885c9")                     # test rcx, rcx
    b += bytes.fromhex("7507")                       # jne +7
    b += bytes.fromhex("488945c8")                   # mov [rbp-0x38], rax
    b += bytes.fromhex("4889c1")                     # mov rcx, rax
    b += bytes.fromhex("4829c8")                     # sub rax, rcx          (stream time)
    b += bytes.fromhex("4889442420")                 # mov [rsp+0x20], rax
    b += bytes.fromhex("4889d1")                     # mov rcx, rdx          (IMediaSample *)
    b += bytes.fromhex("488d542420")                 # lea rdx, [rsp+0x20]
    b += bytes.fromhex("4531c0")                     # xor r8d, r8d          (no stop time)
    b += bytes.fromhex("488b01")                     # mov rax, [rcx]
    b += bytes.fromhex("ff5030")                     # call [rax+0x30]       IMediaSample::SetTime
    b += bytes.fromhex("488b55e0")                   # mov rdx, [rbp-0x20]
    b += bytes.fromhex("488b8b60040000")             # original: mov rcx, [rbx+0x460]
    b += b"\xe9" + rel(cave_b + len(b) + 5, ret_b)

    d[off(cave_a):off(cave_a) + len(a)] = a
    d[off(cave_b):off(cave_b) + len(b)] = b
    d[off(site_a):off(site_a) + 8] = b"\xe9" + rel(site_a + 5, cave_a) + b"\x90\x90\x90"
    d[off(site_b):off(site_b) + 7] = b"\xe9" + rel(site_b + 5, cave_b) + b"\x90\x90"
    open(DLL, "wb").write(d)
    print("qcap.dll patched")


if __name__ == "__main__":
    for p in (SO, DLL):
        if not os.path.exists(p):
            sys.exit("not found: " + p)
        os.chmod(p, 0o755)
    patch_so()
    patch_dll()
