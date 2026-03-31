#include "sdr.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <string.h>

#define VIDEO_DEVICE   "/dev/video0"
#define SENSOR_DEVICE  "/dev/v4l-subdev2"

#define CAM_WIDTH  4224 //might need updating depending on data size, maybe should be dynamic?
#define CAM_HEIGHT 3136

#define BUFFER_COUNT 3

static int sensor_fd;
static int fd;
static void *buffers[BUFFER_COUNT];
static size_t buf_size;
static enum v4l2_buf_type type;


int data_transfer_init() {

    int sensor_fd = open(SENSOR_DEVICE, O_RDWR);
    int fd = open(VIDEO_DEVICE, O_RDWR);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = CAM_WIDTH;
    fmt.fmt.pix_mp.height = CAM_HEIGHT;
    fmt.fmt.pix_mp.pixelformat = v4l2_fourcc('R','G','1','0');
    fmt.fmt.pix_mp.num_planes = 1;

    ioctl(fd, VIDIOC_S_FMT, &fmt);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));

    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    ioctl(fd, VIDIOC_REQBUFS, &req);

    void *buffers[BUFFER_COUNT];
    size_t buf_size = 0;

    for (int i = 0; i < BUFFER_COUNT; i++) {

        struct v4l2_buffer buf;
        struct v4l2_plane plane;

        memset(&buf, 0, sizeof(buf));
        memset(&plane, 0, sizeof(plane));

        buf.type = req.type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = &plane;

        ioctl(fd, VIDIOC_QUERYBUF, &buf);

        buffers[i] = mmap(NULL, plane.length,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, plane.m.mem_offset);

        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    enum v4l2_buf_type type = req.type;
    ioctl(fd, VIDIOC_STREAMON, &type);

}


int data_transfer_close() {

    close(sensor_fd);

    ioctl(fd, VIDIOC_STREAMOFF, &type);

    for (int i = 0; i < BUFFER_COUNT; i++) {
        munmap(buffers[i], buf_size);
    }

    close(fd);

}


int read_data(const char *filename) {

    struct v4l2_buffer buf;
    struct v4l2_plane plane;

    memset(&buf, 0, sizeof(buf));
    memset(&plane, 0, sizeof(plane));

    buf.type = type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = 1;
    buf.m.planes = &plane;

    ioctl(fd, VIDIOC_DQBUF, &buf);

    FILE *f = fopen(filename, "wb");
    fwrite(buffers[buf.index], buf_size, 1, f);
    fclose(f);

    ioctl(fd, VIDIOC_QBUF, &buf);

    return 0;

}


int sdr_set_control(uint32_t id, int value) {

/*
 * id accepted values: V4L2_CID_TEST_PATTERN, (tbd)
 */

    if (sensor_fd < 0)
        return -1;
    
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    
    ctrl.id = id;
    ctrl.value = value;

    if (ioctl(sensor_fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        perror("VIDIOC_S_CTRL");
        return -1;
    }

    return 0;

}


int sdr_set_adc_config(int value) {
    //not implemented
    return 0;
}


int sdr_set_mipi_config(int value) {
    //not implemented
    return 0;
}

int sdr_set_synthesizer_config(int value) {
    //not implemented
    return 0;
}


int sdr_set_test_pattern(int mode) {
/*
 * Default value: 0
 * Range: 0 - 4
 */
    return sdr_set_control(V4L2_CID_TEST_PATTERN, mode);
}

