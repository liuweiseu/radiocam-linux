#ifndef _RADIOCAM_H
#define _RADIOCAM_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct radiocam_status
{
    __u32 status;
} __attribute__((packed));

/* customized ioctl codes for V4L2 subdev (magic byte 'M') */
#define RADIOCAM_DEVICE 'M'
#define RADIOCAM_GET_STATUS _IOR('M', 0, struct radiocam_status)
#define RADIOCAM_SET_MODE _IOW('M', 1, int)

/* ioctl codes for the /dev/radiocam-i2c miscdevice (magic byte 'R') */
struct radiocam_version {
    __u32 major;       /* kernel driver version (from DRIVER_VERSION) */
    __u32 minor;
    __u32 patch;
    __u32 fw_major;    /* MCU firmware version (read from DEV_ID_SYSMON) */
    __u32 fw_minor;
    __u32 fw_patch;
};
#define RADIOCAM_GET_VERSION _IOR('R', 0, struct radiocam_version)

/* MCU device ID and register constants.
 * Must match choosecmd.h / firmware_version.h in radiocam-firmware. */
#define RADIOCAM_DEV_SYSMON          0x01
#define RADIOCAM_SYSMON_DEBUG_REG    0x00  /* R/W debug output toggle */
#define RADIOCAM_SYSMON_VERSION_REG  0x01  /* R/O FW_VERSION_WORD */
#define RADIOCAM_DEV_MIPI            0x02
#define RADIOCAM_MIPI_STREAM_REG     0x00
#define RADIOCAM_MIPI_STREAM_ON      0x01
#define RADIOCAM_MIPI_STREAM_OFF     0x00

/* customized v4l2 controk IDs */
#define V4L2_CTRL_CLASS_USER 0x00980000 /* Old-style 'user' controls */
#define V4L2_CID_BASE (V4L2_CTRL_CLASS_USER | 0x900)
#define V4L2_CID_USER_BASE V4L2_CID_BASE
#define V4L2_CID_RADIOCAM_SETTING (V4L2_CID_USER_BASE + 0x1000)
// #define V4L2_CID_MY_SETTING (V4L2_CID_MY_CUSTOM_OFFSET + 1)

#endif