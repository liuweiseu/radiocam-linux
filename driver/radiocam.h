#ifndef _RADIOCAM_H
#define _RADIOCAM_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct radiocam_status
{
    __u32 status;
} __attribute__((packed));

#define RADIOCAM_DEVICE 'M'
#define RADIOCAM_GET_STATUS _IOR('M', 0, struct radiocam_status)
#define RADIOCAM_SET_MODE _IOW('M', 1, int)

#endif