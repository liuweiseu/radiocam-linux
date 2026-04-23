// SPDX-License-Identifier: GPL-2.0
/*
 * radiocam driver
 *
 * Copyright (C) 2026 Radio Astronomy Lab, UCB.
 *
 * V0.0.1 implemented i2c control.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <media/v4l2-device.h>
#include "radiocam.h"

#define RADIOCAM_NAME "radiocam"
#define DRIVER_VERSION KERNEL_VERSION(0, 0x00, 0x03)

#define RADIOCAM_LINK_FREQ_160MHZ 160000000
/* actual pixel rate provided by hardware: 75 MHz */
//#define RADIOCAM_PIXEL_RATE 40000000ULL
#define RADIOCAM_PIXEL_RATE   125000000ULL

#define RADIOCAM_LANES 4
#define RADIOCAM_VBLANK 4

    static const s64 link_freq_menu_items[] = {
        RADIOCAM_LINK_FREQ_160MHZ,
};
#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id radiocam_of_match[] = {
    {.compatible = "ucb-ral,radiocam"},
    {},
};
MODULE_DEVICE_TABLE(of, radiocam_of_match);
#endif

struct radiocam_mode
{
    u32 width;
    u32 height;
    struct v4l2_fract max_fps;
    u32 hts_def;
    u32 vts_def;
    u32 exp_def;
    u32 link_freq_idx;
    u32 bpp;
};
struct radiocam
{
    struct i2c_client *client;
    struct v4l2_subdev subdev;
    struct media_pad pad;
    struct v4l2_ctrl_handler ctrl_handler;
    struct v4l2_ctrl *exposure;
    struct v4l2_ctrl *vblank;
    struct mutex mutex;
    bool streaming;
    bool power_on;
    const struct radiocam_mode *cur_mode;
    u32 module_index;
    struct v4l2_device v4l2_dev;
    struct video_device video_dev;
    struct media_device media_dev;
};

#define to_radiocam(sd) container_of(sd, struct radiocam, subdev)

static const struct radiocam_mode supported_modes[] = {
    {
        .width = 2048,
        .height = 1080,
        .max_fps = {
            .numerator = 176963,
            .denominator = 10000000,
        },
        .exp_def = 0x0440,
        //.hts_def = 4800,
        .hts_def = 2612, /* HS(168) + HBP(242) + HDISP(2048) + HFP(274) */
        .vts_def = 1086, /* VS(2) + VBP(1) + VDISP(1080) + VFP(1)*/
        .link_freq_idx = 0,
        .bpp = 8,
    },
};

/****************************************************************************************/
/***************************** low-level register read/write ****************************/
/****************************************************************************************/
/* Read register*/
static int radiocam_read_reg(struct i2c_client *client, u8 dev_id, u32 addr, u32 *val)
{
    struct i2c_msg msgs[1];
    int ret;
    u8 tx_buf[10];
    u8 *rx_buf = (u8 *)val;
    // take a look at the protocol here:
    // https://github.com/liuweiseu/hercules-i2c-demo/wiki/com-protocol
    tx_buf[0] = dev_id;
    tx_buf[1] = 0;
    tx_buf[2] = (addr >> 24) & 0xff;
    tx_buf[3] = (addr >> 16) & 0xff;
    tx_buf[4] = (addr >> 8) & 0xff;
    tx_buf[5] = (addr >> 0) & 0xff;

    /* Write register address */
    msgs[0].addr = client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 10;
    msgs[0].buf = tx_buf;
    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs))
        return -EIO;
    /* wait for 2ms */
    usleep_range(2000, 2500);
    /* Read data from register */
    msgs[0].addr = client->addr;
    msgs[0].flags = I2C_M_RD;
    msgs[0].len = 4;
    msgs[0].buf = rx_buf;
    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs))
        return -EIO;
    dev_dbg(&client->dev, "(dbg)Read reg(0x%x, 0x%x): 0x%x\n", dev_id, addr, *val);
    return 0;
}

/* Write registers up to 4 at a time */
static int radiocam_write_reg(struct i2c_client *client, u8 dev_id, u32 addr, u32 val)
{
    dev_dbg(&client->dev, "(dbg)write reg(0x%x, 0x%x): 0x%x\n", dev_id, addr, val);
    struct i2c_msg msgs[1];
    u8 tx_buf[10];
    u8 rx_buf[4];
    int ret;
    tx_buf[0] = dev_id;
    tx_buf[1] = 1;
    tx_buf[2] = (addr >> 24) & 0xff;
    tx_buf[3] = (addr >> 16) & 0xff;
    tx_buf[4] = (addr >> 8) & 0xff;
    tx_buf[5] = (addr >> 0) & 0xff;
    tx_buf[6] = (val >> 24) & 0xff;
    tx_buf[7] = (val >> 16) & 0xff;
    tx_buf[8] = (val >> 8) & 0xff;
    tx_buf[9] = (val >> 0) & 0xff;

    /* Write register address */
    msgs[0].addr = client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 10;
    msgs[0].buf = tx_buf;
    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs))
        return -EIO;
    /* wait for 2ms */
    usleep_range(2000, 2500);
    /* Read data from register */
    msgs[0].addr = client->addr;
    msgs[0].flags = I2C_M_RD;
    msgs[0].len = 4;
    msgs[0].buf = rx_buf;
    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs))
        return -EIO;
    dev_dbg(&client->dev, "(dbg)Write reg - return Val: 0x%x\n", *(u32 *)rx_buf);
    if (rx_buf[0] == dev_id)
    {
        dev_dbg(&client->dev, "(dbg)Write reg successfully");
        return 0;
    }
    else
    {
        dev_err(&client->dev, "Write reg(0x%x, 0x%x): 0x%x failed", dev_id, addr, val);
        // should we return -EIO here??
        return -EIO;
    }
}

/**********************************************************************/
/***************************** driver code ****************************/
/**********************************************************************/
/*
 * function: radiocam_runtime_suspend
 * brief: power management
 */
static int __maybe_unused radiocam_runtime_suspend(struct device *dev)
{
    // TODO: implement this function
    return 0;
}

/*
 * function: radiocam_runtime_resume
 * brief: power management
 */
static int __maybe_unused radiocam_runtime_resume(struct device *dev)
{
    // TODO: implement this function
    return 0;
}

/*
 * core ops
 */
static int radiocam_s_power(struct v4l2_subdev *sd, int on)
{
    struct radiocam *radiocam = to_radiocam(sd);
    struct i2c_client *client = radiocam->client;
    int ret = 0;

    mutex_lock(&radiocam->mutex);
    // TODO: TO be implemented

unlock_and_return:
    mutex_unlock(&radiocam->mutex);

    return ret;
}

static long radiocam_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
    struct radiocam *radiocam = to_radiocam(sd);
    long ret = 0;
    u32 stream = 0;

    switch (cmd)
    {
    case RADIOCAM_GET_STATUS:
        dev_dbg(&radiocam->client->dev, "RADIOCAM_GET_STATUS");
        break;
    case RADIOCAM_SET_MODE:
        dev_dbg(&radiocam->client->dev, "RADIOCAM_SET_MODE");
        break;
    default:
        ret = -ENOIOCTLCMD;
        break;
    }

    return ret;
}

static int __radiocam_start_stream(struct radiocam *radiocam)
{
    // TODO: use i2c read/write reg here
    return radiocam_write_reg(radiocam->client,
                              0x02,
                              0x00,
                              0x01);
}

static int __radiocam_stop_stream(struct radiocam *radiocam)
{
    // TODO: use i2c read/write reg here
    return radiocam_write_reg(radiocam->client,
                              0x02,
                              0x00,
                              0x00);
}

static int radiocam_s_stream(struct v4l2_subdev *sd, int on)
{
    struct radiocam *radiocam = to_radiocam(sd);
    struct i2c_client *client = radiocam->client;
    int ret = 0;

    mutex_lock(&radiocam->mutex);
    on = !!on;
    if (on == radiocam->streaming)
        goto unlock_and_return;
    if (on)
    {
        ret = __radiocam_start_stream(radiocam);
        if (ret)
        {
            v4l2_err(sd, "start stream failed while write regs\n");
            pm_runtime_put(&client->dev);
            goto unlock_and_return;
        }
    }
    else
    {
        __radiocam_stop_stream(radiocam);
    }
    radiocam->streaming = on;
unlock_and_return:
    mutex_unlock(&radiocam->mutex);

    return ret;
}

static int radiocam_g_frame_interval(struct v4l2_subdev *sd,
                                     struct v4l2_subdev_frame_interval *fi)
{
    struct radiocam *radiocam = to_radiocam(sd);

    mutex_lock(&radiocam->mutex);
    fi->interval = radiocam->cur_mode->max_fps;
    mutex_unlock(&radiocam->mutex);

    return 0;
}

static int radiocam_enum_mbus_code(struct v4l2_subdev *sd,
                                   struct v4l2_subdev_state *sd_state,
                                   struct v4l2_subdev_mbus_code_enum *code)
{
    if (code->index != 0)
        return -EINVAL;

    //code->code = MEDIA_BUS_FMT_SBGGR16_1X16;
    code->code = MEDIA_BUS_FMT_SBGGR8_1X8;
    return 0;
}

static int radiocam_enum_frame_sizes(struct v4l2_subdev *sd,
                                     struct v4l2_subdev_state *sd_state,
                                     struct v4l2_subdev_frame_size_enum *fse)
{
    if (fse->index >= ARRAY_SIZE(supported_modes))
        return -EINVAL;

    //if (fse->code != MEDIA_BUS_FMT_SBGGR16_1X16)
    if (fse->code != MEDIA_BUS_FMT_SBGGR8_1X8)
        return -EINVAL;

    fse->min_width = supported_modes[fse->index].width;
    fse->max_width = supported_modes[fse->index].width;
    fse->min_height = supported_modes[fse->index].height;
    fse->max_height = supported_modes[fse->index].height;

    return 0;
}

static int radiocam_enum_frame_interval(struct v4l2_subdev *sd,
                                        struct v4l2_subdev_state *sd_state,
                                        struct v4l2_subdev_frame_interval_enum *fie)
{
    const struct radiocam_mode *mode = NULL;
    unsigned int i;

    //if (fie->code != 0 && fie->code != MEDIA_BUS_FMT_SBGGR16_1X16)
    if (fie->code != 0 && fie->code != MEDIA_BUS_FMT_SBGGR8_1X8)
        return -EINVAL;

    /* rkcif calls with code=0 and width=0/height=0 to enumerate all modes;
     * in that case enumerate by index across all supported modes */
    if (fie->code == 0 || (fie->width == 0 && fie->height == 0))
    {
        if (fie->index >= ARRAY_SIZE(supported_modes))
            return -EINVAL;
        mode = &supported_modes[fie->index];
        //fie->code = MEDIA_BUS_FMT_SBGGR16_1X16;
        fie->code = MEDIA_BUS_FMT_SBGGR8_1X8;
        fie->width = mode->width;
        fie->height = mode->height;
        fie->interval = mode->max_fps;
        return 0;
    }

    /* find the mode matching the requested resolution */
    for (i = 0; i < ARRAY_SIZE(supported_modes); i++)
    {
        if (supported_modes[i].width == fie->width &&
            supported_modes[i].height == fie->height)
        {
            mode = &supported_modes[i];
            break;
        }
    }
    if (!mode)
        return -EINVAL;

    /* each mode has exactly one fixed frame rate, so only index 0 is valid */
    if (fie->index != 0)
        return -EINVAL;

    fie->interval = mode->max_fps;
    return 0;
}

static int radiocam_get_fmt(struct v4l2_subdev *sd,
                            struct v4l2_subdev_state *sd_state,
                            struct v4l2_subdev_format *fmt)
{
    struct radiocam *radiocam = to_radiocam(sd);
    const struct radiocam_mode *mode = radiocam->cur_mode;

    mutex_lock(&radiocam->mutex);
    if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
    {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
        fmt->format = *v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
#else
        mutex_unlock(&radiocam->mutex);
        return -ENOTTY;
#endif
    }
    else
    {
        fmt->format.width = mode->width;
        fmt->format.height = mode->height;
        // fmt->format.code = MEDIA_BUS_FMT_SBGGR16_1X16;
        fmt->format.code = MEDIA_BUS_FMT_SBGGR8_1X8;
        fmt->format.field = V4L2_FIELD_NONE;
    }
    mutex_unlock(&radiocam->mutex);

    return 0;
}

static int radiocam_get_reso_dist(const struct radiocam_mode *mode,
                                  struct v4l2_mbus_framefmt *framefmt)
{
    return abs(mode->width - framefmt->width) +
           abs(mode->height - framefmt->height);
}

static const struct radiocam_mode *
radiocam_find_best_fit(struct v4l2_subdev_format *fmt)
{
    struct v4l2_mbus_framefmt *framefmt = &fmt->format;
    int dist;
    int cur_best_fit = 0;
    int cur_best_fit_dist = -1;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(supported_modes); i++)
    {
        dist = radiocam_get_reso_dist(&supported_modes[i], framefmt);
        if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist)
        {
            cur_best_fit_dist = dist;
            cur_best_fit = i;
        }
    }

    return &supported_modes[cur_best_fit];
}

static int radiocam_set_fmt(struct v4l2_subdev *sd,
                            struct v4l2_subdev_state *sd_state,
                            struct v4l2_subdev_format *fmt)
{
    struct radiocam *radiocam = to_radiocam(sd);
    const struct radiocam_mode *mode;
    // s64 h_blank, vblank_def;
    // u64 pixel_rate = 0;
    u32 lane_num = RADIOCAM_LANES;
    mutex_lock(&radiocam->mutex);
    mode = radiocam_find_best_fit(fmt);
    // fmt->format.code = MEDIA_BUS_FMT_SBGGR16_1X16;
    fmt->format.code = MEDIA_BUS_FMT_SBGGR8_1X8;
    fmt->format.width = mode->width;
    fmt->format.height = mode->height;
    fmt->format.field = V4L2_FIELD_NONE;
    if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
    {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
        *v4l2_subdev_get_try_format(sd, sd_state, fmt->pad) = fmt->format;
#else
        mutex_unlock(&radiocam->mutex);
        return -ENOTTY;
#endif
    }
    else
    {
        dev_dbg(sd->dev, "Do nothing in set fmt.");
    }
    dev_info(&radiocam->client->dev, "%s: mode->link_freq_idx(%d)",
             __func__, mode->link_freq_idx);
    mutex_unlock(&radiocam->mutex);

    return 0;
}

static int radiocam_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
                                  struct v4l2_mbus_config *config)
{
    config->type = V4L2_MBUS_CSI2_DPHY;
    config->bus.mipi_csi2.num_data_lanes = 4;
    return 0;
}

static const struct v4l2_subdev_core_ops radiocam_core_ops = {
    .s_power = radiocam_s_power,
    .ioctl = radiocam_ioctl,
    // #ifdef CONFIG_COMPAT
    //     .compat_ioctl32 = radiocam_compat_ioctl32,
    // #endif
};

static const struct v4l2_subdev_video_ops radiocam_video_ops = {
    .s_stream = radiocam_s_stream,
    .g_frame_interval = radiocam_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops radiocam_pad_ops = {
    .enum_mbus_code = radiocam_enum_mbus_code,
    .enum_frame_size = radiocam_enum_frame_sizes,
    .enum_frame_interval = radiocam_enum_frame_interval,
    .get_fmt = radiocam_get_fmt,
    .set_fmt = radiocam_set_fmt,
    .get_mbus_config = radiocam_g_mbus_config,
};

static const struct v4l2_subdev_ops radiocam_subdev_ops = {
    .core = &radiocam_core_ops,
    .video = &radiocam_video_ops,
    .pad = &radiocam_pad_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int radiocam_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
    struct radiocam *radiocam = to_radiocam(sd);
    struct v4l2_mbus_framefmt *try_fmt =
        v4l2_subdev_get_try_format(sd, fh->state, 0);
    const struct radiocam_mode *def_mode = &supported_modes[0];

    mutex_lock(&radiocam->mutex);
    /* Initialize try_fmt */
    try_fmt->width = def_mode->width;
    try_fmt->height = def_mode->height;
    // try_fmt->width = 0;
    // try_fmt->height = 0;
    try_fmt->code = MEDIA_BUS_FMT_SBGGR16_1X16;
    try_fmt->code = MEDIA_BUS_FMT_SBGGR8_1X8;
    try_fmt->field = V4L2_FIELD_NONE;

    mutex_unlock(&radiocam->mutex);
    /* No crop or compose */

    return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops radiocam_internal_ops = {
    .open = radiocam_open,
};
#endif

static int sensor_get_controls(struct v4l2_ctrl *ctrl)
{
    struct radiocam *radiocam = container_of(ctrl->handler, struct radiocam, ctrl_handler);

    switch (ctrl->id)
    {
    case V4L2_CID_VBLANK:
        ctrl->val = RADIOCAM_VBLANK;
        break;
    default:
        dev_dbg(&radiocam->client->dev, "sensor_get_controls: unknown ctrl id 0x%x\n", ctrl->id);
        return -EINVAL;
    }
    return 0;
}

static int radiocam_s_ctrl(struct v4l2_ctrl *ctrl)
{
    struct radiocam *radiocam = container_of(ctrl->handler, struct radiocam, ctrl_handler);

    switch (ctrl->id)
    {
    case V4L2_CID_RADIOCAM_SETTING:
        dev_dbg(&radiocam->client->dev, "V4L2_CID_RADIOCAM_SETTING");
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static const struct v4l2_ctrl_ops radiocam_ctrl_ops = {
    .g_volatile_ctrl = sensor_get_controls,
    .s_ctrl = radiocam_s_ctrl,
};

static const struct v4l2_ctrl_config radiocam_setting_ctrl_config = {
    .ops = &radiocam_ctrl_ops,
    .id = V4L2_CID_RADIOCAM_SETTING,
    .name = "RadioCam Custom Setting",
    .type = V4L2_CTRL_TYPE_INTEGER,
    .min = 0,
    .max = 100,
    .step = 1,
    .def = 50,
};

static int radiocam_initialize_controls(struct radiocam *radiocam)
{
    struct v4l2_ctrl_handler *handler;
    struct v4l2_ctrl *ctrl;
    int ret;

    handler = &radiocam->ctrl_handler;
    ret = v4l2_ctrl_handler_init(handler, 4);
    if (ret)
        return ret;

    /* vblank — volatile, fixed at RADIOCAM_VBLANK lines */
    radiocam->vblank = v4l2_ctrl_new_std(handler, &radiocam_ctrl_ops,
                                         V4L2_CID_VBLANK,
                                         RADIOCAM_VBLANK, RADIOCAM_VBLANK, 1, RADIOCAM_VBLANK);
    if (radiocam->vblank)
        radiocam->vblank->flags |= V4L2_CTRL_FLAG_VOLATILE;

    /* pixel rate — read-only, 75 MHz as provided by hardware */
    v4l2_ctrl_new_std(handler, NULL,
                      V4L2_CID_PIXEL_RATE,
                      0, RADIOCAM_PIXEL_RATE, 1, RADIOCAM_PIXEL_RATE);

    /* link frequency — read-only, fixed at 320MHz to match hardware */
    ctrl = v4l2_ctrl_new_int_menu(handler, NULL,
                                  V4L2_CID_LINK_FREQ,
                                  ARRAY_SIZE(link_freq_menu_items) - 1,
                                  0,
                                  link_freq_menu_items);
    if (ctrl)
        ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

    /* customized control */
    v4l2_ctrl_new_custom(handler, &radiocam_setting_ctrl_config, NULL);

    if (handler->error)
    {
        ret = handler->error;
        dev_err(&radiocam->client->dev, "Failed to init controls(%d)\n", ret);
        goto err_free_handler;
    }
    radiocam->subdev.ctrl_handler = handler;
    return 0;

err_free_handler:
    v4l2_ctrl_handler_free(handler);
    return ret;
}
/*
*********************** probe and remove functions *************************
*/

static const struct i2c_device_id radiocam_match_id[] = {
    {"ucb-ral,radiocam", 0},
    {},
};

static const struct dev_pm_ops radiocam_pm_ops = {
    SET_RUNTIME_PM_OPS(radiocam_runtime_suspend,
                       radiocam_runtime_resume, NULL)};

static int radiocam_probe(struct i2c_client *client,
                          const struct i2c_device_id *id)
{
    struct device *dev = &client->dev;
    struct device_node *node = dev->of_node;
    struct radiocam *radiocam;
    struct v4l2_subdev *sd;
    int ret;

    dev_info(dev, "driver version: %02x.%02x.%02x",
             DRIVER_VERSION >> 16,
             (DRIVER_VERSION & 0xff00) >> 8,
             DRIVER_VERSION & 0x00ff);

    radiocam = devm_kzalloc(dev, sizeof(*radiocam), GFP_KERNEL);
    if (!radiocam)
        return -ENOMEM;

    // save i2c client to radiocam struct
    radiocam->client = client;

    radiocam->cur_mode = &supported_modes[0];

    mutex_init(&radiocam->mutex);

    sd = &radiocam->subdev;
    v4l2_i2c_subdev_init(sd, client, &radiocam_subdev_ops);
    strscpy(sd->name, "radiocam-subdev", sizeof(sd->name));
    ret = radiocam_initialize_controls(radiocam);
    if (ret)
        goto err_destroy_mutex;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
    sd->internal_ops = &radiocam_internal_ops;
    sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
    radiocam->pad.flags = MEDIA_PAD_FL_SOURCE;
    sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
    ret = media_entity_pads_init(&sd->entity, 1, &radiocam->pad);
    if (ret < 0)
    {
        dev_err(dev, "media entity pads init failed\n");
        goto err_clean_entity;
    }
#endif
    ret = v4l2_async_register_subdev_sensor(sd);
    if (ret)
    {
        dev_err(dev, "v4l2 async register subdev failed\n");
        goto err_clean_entity;
    }
    dev_info(dev, "radiocam subdev registered with devnode\n");
    return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
err_free_handler:
    v4l2_ctrl_handler_free(&radiocam->ctrl_handler);
err_destroy_mutex:
    mutex_destroy(&radiocam->mutex);
    return ret;
}

static void radiocam_remove(struct i2c_client *client)
{
    struct v4l2_subdev *sd = i2c_get_clientdata(client);
    struct radiocam *radiocam = to_radiocam(sd);

    v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
    v4l2_ctrl_handler_free(&radiocam->ctrl_handler);
    mutex_destroy(&radiocam->mutex);

    dev_info(&client->dev, "radiocam subdev removed\n");
}

static struct i2c_driver radiocam_i2c_driver = {
    .driver = {
        .name = RADIOCAM_NAME,
        //.pm = &radiocam_pm_ops,
        .of_match_table = of_match_ptr(radiocam_of_match),
    },
    .probe = &radiocam_probe,
    .remove = &radiocam_remove,
    .id_table = radiocam_match_id,
};

static int __init sensor_mod_init(void)
{
    printk("RadioCam Driver Loaded.\n");
    return i2c_add_driver(&radiocam_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
    printk("RadioCam Driver Unloaded.\n");
    i2c_del_driver(&radiocam_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("UCB-RAL radiocam driver");
MODULE_LICENSE("GPL v2");