# RadioCAM App: V4L2 Capture Test Program

## Overview
Userspace test application for the RadioCAM V4L2 driver. Opens `/dev/video0`,
enables streaming, captures one frame, and saves it as a raw binary file.

## Capture Flow (`capture.c`)

1. `open("/dev/video0", O_RDWR)` — open device node
2. `VIDIOC_QUERYCAP` — verify `V4L2_CAP_VIDEO_CAPTURE` and `V4L2_CAP_STREAMING`
3. `VIDIOC_S_FMT` — set format: 1920×1080, `V4L2_PIX_FMT_SBGGR8` (RAW8 Bayer)
4. `VIDIOC_REQBUFS` — request 1 MMAP buffer
5. `VIDIOC_QUERYBUF` + `mmap()` — map buffer into userspace
6. `VIDIOC_QBUF` — enqueue buffer
7. `VIDIOC_STREAMON` — start streaming
8. `select()` — wait for frame ready (5s timeout)
9. `VIDIOC_DQBUF` — dequeue filled buffer
10. Write `bytesused` bytes to `raw_dat_<unix_timestamp>.dat`
11. `VIDIOC_STREAMOFF` + `munmap` + `close` — cleanup

## Output
- File: `raw_dat_<unix_timestamp>.dat` in current directory
- Expected size: 1920 × 1080 × 1 byte = **2,073,600 bytes** (RAW8)

## Build

```bash
# Cross-compile (from host)
make CC=aarch64-linux-gnu-gcc

# Native compile (on board)
make
```

## Test on Board

```bash
./capture
ls -lh raw_dat_*.dat
```
