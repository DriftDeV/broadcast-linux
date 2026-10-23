/* v4lgrab: act like a camera application. Opens a V4L2 capture device, streams for a few seconds, writes the last
   YUYV frame to a file.  Usage: v4lgrab /dev/video11 out.yuyv [seconds] */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct v4l2_format f; struct v4l2_requestbuffers rb; struct v4l2_buffer b; enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    void *map[4]; size_t len[4]; unsigned i, n; int fd, secs = argc > 3 ? atoi(argv[3]) : 5; time_t end; long frames = 0;
    if (argc < 3) { fprintf(stderr, "usage: %s device out.yuyv [seconds]\n", argv[0]); return 2; }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror(argv[1]); return 1; }
    memset(&f, 0, sizeof(f)); f.type = t;
    if (ioctl(fd, VIDIOC_G_FMT, &f) < 0) { perror("G_FMT"); return 1; }
    printf("format %ux%u %.4s\n", f.fmt.pix.width, f.fmt.pix.height, (char *)&f.fmt.pix.pixelformat);
    memset(&rb, 0, sizeof(rb)); rb.count = 4; rb.type = t; rb.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) { perror("REQBUFS"); return 1; }
    n = rb.count > 4 ? 4 : rb.count;
    for (i = 0; i < n; i++) {
        memset(&b, 0, sizeof(b)); b.type = t; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        ioctl(fd, VIDIOC_QUERYBUF, &b); len[i] = b.length;
        map[i] = mmap(NULL, b.length, PROT_READ, MAP_SHARED, fd, b.m.offset);
        ioctl(fd, VIDIOC_QBUF, &b);
    }
    if (ioctl(fd, VIDIOC_STREAMON, &t) < 0) { perror("STREAMON"); return 1; }
    end = time(NULL) + secs;
    while (time(NULL) < end) {
        memset(&b, 0, sizeof(b)); b.type = t; b.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) { perror("DQBUF"); break; }
        frames++;
        { FILE *o = fopen(argv[2], "wb"); if (o) { fwrite(map[b.index], 1, b.bytesused, o); fclose(o); } }
        ioctl(fd, VIDIOC_QBUF, &b);
    }
    printf("received %ld frames in %d s\n", frames, secs);
    return 0;
}
