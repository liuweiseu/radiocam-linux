#include "sdr_transfer.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#define VIDEO_DEVICE   "/dev/video0"
#define CAM_WIDTH  2048 
#define CAM_HEIGHT 1080
#define BUFFER_COUNT 3

static int fd = -1;
static void *buffers[BUFFER_COUNT];
static size_t global_buf_size = 0;
static enum v4l2_buf_type global_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

int data_transfer_init(void) {
    fd = open(VIDEO_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open video device");
        return -1;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = global_type;
    fmt.fmt.pix_mp.width = CAM_WIDTH;
    fmt.fmt.pix_mp.height = CAM_HEIGHT;
    fmt.fmt.pix_mp.pixelformat = v4l2_fourcc('B','A','8','1');
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = global_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane plane;
        memset(&buf, 0, sizeof(buf));
        memset(&plane, 0, sizeof(plane));

        buf.type = global_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = &plane;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        if (i == 0) global_buf_size = plane.length;

        buffers[i] = mmap(NULL, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, plane.m.mem_offset);
        if (buffers[i] == MAP_FAILED) {
            perror("mmap");
            return -1;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    if (ioctl(fd, VIDIOC_STREAMON, &global_type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    return 0;
}

int data_transfer_close(void) {
    if (fd >= 0) {
        ioctl(fd, VIDIOC_STREAMOFF, &global_type);
        for (int i = 0; i < BUFFER_COUNT; i++) {
            if (buffers[i]) munmap(buffers[i], global_buf_size);
        }
        close(fd);
        fd = -1;
    }
    return 0;
}

int read_data(const char *filename, const int num_frames) {
    // Open the file before the loop
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open file for writing");
        return -1;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane plane;

    for (int i = 0; i < num_frames; i++) {
        // Reset the structs for each iteration
        memset(&buf, 0, sizeof(buf));
        memset(&plane, 0, sizeof(plane));

        buf.type = global_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = 1;
        buf.m.planes = &plane;

        // Wait for and De-queue a newly filled buffer from the FPGA
        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            fclose(f); // Clean up before returning
            return -1;
        }

        // Write this specific frame to the file
        fwrite(buffers[buf.index], plane.bytesused, 1, f);

        // Re-queue the empty buffer back to the kernel so the FPGA can fill it again
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            fclose(f); // Clean up before returning
            return -1;
        }
    }

    // Close the file ONCE after all frames are written
    fclose(f);
    
    return 0;
}