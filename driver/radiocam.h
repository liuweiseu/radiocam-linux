#ifndef _RADIOCAM_H
#define _RADIOCAM_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct radiocam_status
{
    __u32 status;
} __attribute__((packed));

/* customized ioctl codes */
#define RADIOCAM_DEVICE 'M'
#define RADIOCAM_GET_STATUS _IOR('M', 0, struct radiocam_status)
#define RADIOCAM_SET_MODE _IOW('M', 1, int)

/* customized v4l2 controk IDs */
#define V4L2_CTRL_CLASS_USER 0x00980000 /* Old-style 'user' controls */
#define V4L2_CID_BASE (V4L2_CTRL_CLASS_USER | 0x900)
#define V4L2_CID_USER_BASE V4L2_CID_BASE
#define V4L2_CID_RADIOCAM_SETTING (V4L2_CID_USER_BASE + 0x1000)
// #define V4L2_CID_MY_SETTING (V4L2_CID_MY_CUSTOM_OFFSET + 1)

#endif