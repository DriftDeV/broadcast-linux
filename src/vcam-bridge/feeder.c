/* nvb-vcam-feeder -- Linux side of the NVIDIA Broadcast virtual camera.
 *
 * Reads the frames the Wine bridge publishes in /dev/shm/nvb-vcam (see vcam_shm.h), converts them to YUYV and
 * writes them to a v4l2loopback device.  It tells the bridge (header.active) whether any other process has the
 * loopback device open, so the physical webcam is only used while an application actually watches the camera.
 *
 *   nvb-vcam-feeder [--device /dev/video10] [--width 1280] [--height 720] [--fps 30] [--shm /dev/shm/nvb-vcam]
 *                   [--always-active] [--dump out.yuyv]   (--dump: write one frame to a file and exit; for tests)
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "vcam_shm.h"

static volatile sig_atomic_t g_stop;
static void on_signal(int s) { (void)s; g_stop = 1; }

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* true when a process other than us holds `device` open (a camera application is watching) */
static int has_reader(const char *device)
{
    DIR *proc = opendir("/proc");
    struct dirent *e;
    pid_t self = getpid();
    int found = 0;
    if (!proc) return 1;                       /* cannot tell: stay active */
    while (!found && (e = readdir(proc))) {
        char path[96], link[256];
        DIR *fds;
        struct dirent *f;
        if (e->d_name[0] < '0' || e->d_name[0] > '9' || atoi(e->d_name) == self) continue;
        snprintf(path, sizeof(path), "/proc/%s/fd", e->d_name);
        fds = opendir(path);
        if (!fds) continue;
        while ((f = readdir(fds))) {
            char fp[160];
            ssize_t n;
            if (f->d_name[0] == '.') continue;
            snprintf(fp, sizeof(fp), "%s/%s", path, f->d_name);
            n = readlink(fp, link, sizeof(link) - 1);
            if (n > 0) { link[n] = 0; if (!strcmp(link, device)) { found = 1; break; } }
        }
        closedir(fds);
    }
    closedir(proc);
    return found;
}

static inline uint8_t clip(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

/* BGRA (any size, optionally bottom-up) -> YUYV dw x dh, nearest-neighbour when the sizes differ */
static void bgra_to_yuyv(const uint8_t *src, unsigned sw, unsigned sh, unsigned sstride, int bottom_up,
                         uint8_t *dst, unsigned dw, unsigned dh)
{
    unsigned x, y;
    for (y = 0; y < dh; y++) {
        unsigned sy = (unsigned)((uint64_t)y * sh / dh);
        const uint8_t *row;
        uint8_t *out = dst + (size_t)y * dw * 2;
        if (bottom_up) sy = sh - 1 - sy;
        row = src + (size_t)sy * sstride;
        for (x = 0; x < dw; x += 2) {
            unsigned sx0 = (unsigned)((uint64_t)x * sw / dw), sx1 = (unsigned)((uint64_t)(x + 1) * sw / dw);
            const uint8_t *p0 = row + (size_t)sx0 * 4, *p1 = row + (size_t)sx1 * 4;
            int b0 = p0[0], g0 = p0[1], r0 = p0[2], b1 = p1[0], g1 = p1[1], r1 = p1[2];
            int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
            int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
            int r = (r0 + r1) >> 1, g = (g0 + g1) >> 1, b = (b0 + b1) >> 1;
            int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            out[x * 2 + 0] = clip(y0); out[x * 2 + 1] = clip(u);
            out[x * 2 + 2] = clip(y1); out[x * 2 + 3] = clip(v);
        }
    }
}

static void fill_black(uint8_t *yuyv, size_t n)
{
    size_t i;
    for (i = 0; i + 3 < n; i += 4) { yuyv[i] = 16; yuyv[i + 1] = 128; yuyv[i + 2] = 16; yuyv[i + 3] = 128; }
}

int main(int argc, char **argv)
{
    const char *device = "/dev/video10", *shm_path = "/dev/shm/nvb-vcam", *dump = NULL;
    unsigned width = 1280, height = 720, fps = 30;
    int always_active = 0, i, vfd = -1, sfd;
    volatile struct vcam_shm_header *hdr;
    uint8_t *shm, *frame_copy, *yuyv;
    size_t yuyv_size;
    uint64_t next_scan = 0, tick_ms;
    unsigned long long last_seq = 0;
    uint64_t last_frame_ms = 0, wrote = 0;
    int active_now = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--device") && i + 1 < argc) device = argv[++i];
        else if (!strcmp(argv[i], "--shm") && i + 1 < argc) shm_path = argv[++i];
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) width = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) height = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--always-active")) always_active = 1;
        else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    width &= ~1u;
    yuyv_size = (size_t)width * height * 2;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);

    sfd = open(shm_path, O_RDWR | O_CREAT, 0666);
    if (sfd < 0) { perror(shm_path); return 1; }
    if (ftruncate(sfd, VCAM_SHM_SIZE) < 0) { perror("ftruncate"); return 1; }
    shm = mmap(NULL, VCAM_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); return 1; }
    hdr = (volatile struct vcam_shm_header *)shm;
    frame_copy = malloc(VCAM_MAX_FRAME);
    yuyv = malloc(yuyv_size);
    if (!frame_copy || !yuyv) return 1;
    fill_black(yuyv, yuyv_size);

    if (!dump) {
        struct v4l2_format f;
        vfd = open(device, O_WRONLY);
        if (vfd < 0) { fprintf(stderr, "cannot open %s: %s (is v4l2loopback loaded?)\n", device, strerror(errno)); return 1; }
        memset(&f, 0, sizeof(f));
        f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        f.fmt.pix.width = width; f.fmt.pix.height = height;
        f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        f.fmt.pix.field = V4L2_FIELD_NONE;
        f.fmt.pix.bytesperline = width * 2;
        f.fmt.pix.sizeimage = (unsigned)yuyv_size;
        f.fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
        if (ioctl(vfd, VIDIOC_S_FMT, &f) < 0) { perror("VIDIOC_S_FMT"); return 1; }
        fprintf(stderr, "feeding %s with %ux%u YUYV @ %u fps from %s\n", device, width, height, fps, shm_path);
    }

    tick_ms = 1000 / (fps ? fps : 30);
    while (!g_stop) {
        uint64_t t0 = now_ms();

        if (t0 >= next_scan) {
            int want = always_active || dump || has_reader(device);
            if (want != active_now) fprintf(stderr, "%s\n", want ? "reader present: bridge active" : "no readers: bridge idle");
            active_now = want;
            hdr->active = (unsigned)want;
            next_scan = t0 + 1000;
        }

        /* take the newest complete frame (seqlock) */
        {
            unsigned long long s1 = hdr->seq;
            if (hdr->magic == VCAM_MAGIC && !(s1 & 1) && s1 != last_seq && hdr->frame_bytes && hdr->frame_bytes <= VCAM_MAX_FRAME &&
                hdr->pixfmt == VCAM_PIX_BGRA) {
                unsigned sw = hdr->width, sh = hdr->height, ss = hdr->stride, bu = hdr->bottom_up, fb = hdr->frame_bytes;
                memcpy(frame_copy, shm + VCAM_HEADER_SIZE, fb);
                if (hdr->seq == s1 && sw && sh && (size_t)ss * sh <= fb) {
                    bgra_to_yuyv(frame_copy, sw, sh, ss, (int)bu, yuyv, width, height);
                    last_seq = s1;
                    last_frame_ms = t0;
                }
            }
        }
        /* no fresh frame for a while: show black instead of a frozen image */
        if (last_frame_ms && t0 - last_frame_ms > 1500) { fill_black(yuyv, yuyv_size); last_frame_ms = 0; }

        if (dump) {
            if (last_frame_ms || t0 > 8000 + 0) {
                FILE *o = fopen(dump, "wb");
                if (o) { fwrite(yuyv, 1, yuyv_size, o); fclose(o); }
                fprintf(stderr, "dumped %zu bytes (%s)\n", yuyv_size, last_frame_ms ? "real frame" : "black");
                return last_frame_ms ? 0 : 3;
            }
        } else if (write(vfd, yuyv, yuyv_size) < 0 && errno != EAGAIN) {
            perror("write"); break;
        } else wrote++;

        {
            uint64_t el = now_ms() - t0;
            if (el < tick_ms) { struct timespec ts = { 0, (long)((tick_ms - el) * 1000000) }; nanosleep(&ts, NULL); }
        }
    }
    hdr->active = 0;
    fprintf(stderr, "stopped after %llu frames\n", (unsigned long long)wrote);
    return 0;
}
