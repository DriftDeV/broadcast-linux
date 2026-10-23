/* Shared memory file /dev/shm/nvb-vcam between the Wine bridge (writer of frames) and the Linux feeder
   (writer of the `active` flag).  Plain file, little endian.  Keep both programs in sync with this header. */
#ifndef VCAM_SHM_H
#define VCAM_SHM_H

#include <stddef.h>

#define VCAM_MAGIC        0x5642564eu   /* "NVBV" */
#define VCAM_VERSION      1u
#define VCAM_HEADER_SIZE  4096
#define VCAM_MAX_FRAME    (3840u * 2160u * 4u)
#define VCAM_SHM_SIZE     (VCAM_HEADER_SIZE + VCAM_MAX_FRAME)

#define VCAM_PIX_BGRA     1u            /* B,G,R,A bytes, `stride` bytes per row */

struct vcam_shm_header {
    unsigned magic;
    unsigned version;
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned pixfmt;
    unsigned bottom_up;                 /* 1: first row in memory is the bottom row (Windows DIB) */
    unsigned frame_bytes;
    unsigned long long timestamp_ms;    /* writer clock at the last frame */
    unsigned heartbeat_ms;              /* bumped by the bridge while its graph runs */
    unsigned active;                    /* written by the Linux feeder: 1 = someone reads the loopback device */
    unsigned long long seq;             /* seqlock: odd while the frame is being written */
};

#endif
