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
#define DRIVER_VERSION KERNEL_VERSION(0, 0x00, 0x01)

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id radiocam_of_match[] = {
    {.compatible = "ucb-ral,radiocam"},
    {},
};
MODULE_DEVICE_TABLE(of, radiocam_of_match);
#endif

struct radiocam
{
    struct i2c_client *client;
    struct clk *xvclk;
    struct gpio_desc *power_gpio;
    struct gpio_desc *reset_gpio;
    struct gpio_desc *pwdn_gpio;
    // struct  regulator_bulk_data supplies[OV13850_NUM_SUPPLIES];

    struct pinctrl *pinctrl;
    struct pinctrl_state *pins_default;
    struct pinctrl_state *pins_sleep;

    struct v4l2_subdev subdev;
    struct media_pad pad;
    struct v4l2_ctrl_handler ctrl_handler;
    struct v4l2_ctrl *exposure;
    struct v4l2_ctrl *anal_gain;
    struct v4l2_ctrl *digi_gain;
    struct v4l2_ctrl *hblank;
    struct v4l2_ctrl *vblank;
    struct v4l2_ctrl *test_pattern;
    struct mutex mutex;
    bool streaming;
    bool power_on;
    // const struct ov13850_mode *cur_mode;
    u32 module_index;
    const char *module_facing;
    const char *module_name;
    const char *len_name;
    struct v4l2_device v4l2_dev;
    struct video_device video_dev;
    struct media_device media_dev;
};

#define to_radiocam(sd) container_of(sd, struct radiocam, subdev)

/* Read register*/
static int radocam_read_reg(struct i2c_client *client, u8 dev_id, u32 addr, u32 *val)
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
    tx_buf[1] = 0;
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

// #ifdef CONFIG_COMPAT
// static long radiocam_compat_ioctl32(struct v4l2_subdev *sd,
//                                     unsigned int cmd, unsigned long arg)
// {
//     void __user *up = compat_ptr(arg);
//     long ret;
//     u32 stream = 0;

//     switch (cmd)
//     {
//     case RKMODULE_GET_MODULE_INFO:
//         inf = kzalloc(sizeof(*inf), GFP_KERNEL);
//         if (!inf)
//         {
//             ret = -ENOMEM;
//             return ret;
//         }

//         ret = radiocam_ioctl(sd, cmd, inf);
//         if (!ret)
//             ret = copy_to_user(up, inf, sizeof(*inf));
//         kfree(inf);
//         break;
//     case RKMODULE_AWB_CFG:
//         cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
//         if (!cfg)
//         {
//             ret = -ENOMEM;
//             return ret;
//         }

//         ret = copy_from_user(cfg, up, sizeof(*cfg));
//         if (!ret)
//             ret = ov13850_ioctl(sd, cmd, cfg);
//         kfree(cfg);
//         break;
//     case RKMODULE_SET_QUICK_STREAM:
//         ret = copy_from_user(&stream, up, sizeof(u32));
//         if (!ret)
//             ret = ov13850_ioctl(sd, cmd, &stream);
//         break;
//     default:
//         ret = -ENOIOCTLCMD;
//         break;
//     }

//     return ret;
// }
// #endif

static int radiocam_s_stream(struct v4l2_subdev *sd, int on)
{
    struct radiocam *radiocam = to_radiocam(sd);
    struct i2c_client *client = radiocam->client;
    int ret = 0;

    mutex_lock(&radiocam->mutex);
    // TODO: to be implemented
unlock_and_return:
    mutex_unlock(&radiocam->mutex);

    return ret;
}

static int radiocam_g_frame_interval(struct v4l2_subdev *sd,
                                     struct v4l2_subdev_frame_interval *fi)
{
    struct radiocam *radiocam = to_radiocam(sd);
    // TODO: to be implemented
    return 0;
}

static int radiocam_enum_mbus_code(struct v4l2_subdev *sd,
                                   struct v4l2_subdev_state *sd_state,
                                   struct v4l2_subdev_mbus_code_enum *code)
{
    // TODO: to be implemented

    return 0;
}

static int radiocam_enum_frame_sizes(struct v4l2_subdev *sd,
                                     struct v4l2_subdev_state *sd_state,
                                     struct v4l2_subdev_frame_size_enum *fse)
{
    // TODO: to be implemented
    return 0;
}

static int radiocam_enum_frame_interval(struct v4l2_subdev *sd,
                                        struct v4l2_subdev_state *sd_state,
                                        struct v4l2_subdev_frame_interval_enum *fie)
{
    // TODO: to be implemented
    return 0;
}

static int radiocam_get_fmt(struct v4l2_subdev *sd,
                            struct v4l2_subdev_state *sd_state,
                            struct v4l2_subdev_format *fmt)
{
    struct radiocam *radiocam = to_radiocam(sd);

    mutex_lock(&radiocam->mutex);
    // TODO: to be implemented
    mutex_unlock(&radiocam->mutex);

    return 0;
}

static int radiocam_set_fmt(struct v4l2_subdev *sd,
                            struct v4l2_subdev_state *sd_state,
                            struct v4l2_subdev_format *fmt)
{
    struct radiocam *radiocam = to_radiocam(sd);
    mutex_lock(&radiocam->mutex);
    // TODO: to be implemented
    mutex_unlock(&radiocam->mutex);

    return 0;
}

static int radiocam_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
                                  struct v4l2_mbus_config *config)
{
    // TODO: to be implemented
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
    // const struct ov13850_mode *def_mode = &supported_modes[0];

    mutex_lock(&radiocam->mutex);
    /* Initialize try_fmt */
    // try_fmt->width = def_mode->width;
    // try_fmt->height = def_mode->height;
    try_fmt->width = 0;
    try_fmt->height = 0;
    try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
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
    mutex_init(&radiocam->mutex);

    sd = &radiocam->subdev;
    v4l2_i2c_subdev_init(sd, client, &radiocam_subdev_ops);
    strscpy(sd->name, "radiocam-subdev", sizeof(sd->name));
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

    ret = v4l2_device_register(dev, &radiocam->v4l2_dev);
    if (ret)
        goto err_v4l2;

    ret = v4l2_device_register_subdev(&radiocam->v4l2_dev, sd);
    if (ret)
        goto err_subdev;
    ret = v4l2_device_register_subdev_nodes(&radiocam->v4l2_dev);
    if (ret)
    {
        dev_err(dev, "Failed to register subdev nodes: %d\n", ret);
    }

    dev_info(dev, "radiocam subdev registered with devnode\n");
    if (sd->devnode)
        dev_info(dev, "subdev devnode name: %s\n", sd->devnode->name);
    else
        dev_warn(dev, "subdev devnode not created\n");
    return 0;

err_subdev:
    v4l2_device_unregister_subdev(sd);
err_v4l2:
    v4l2_device_unregister(&radiocam->v4l2_dev);
err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
    return ret;
}

static void radiocam_remove(struct i2c_client *client)
{
    struct v4l2_subdev *sd = i2c_get_clientdata(client);
    struct radiocam *radiocam = to_radiocam(sd);

    v4l2_device_unregister_subdev(sd);
    v4l2_device_unregister(&radiocam->v4l2_dev);
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
    mutex_destroy(&radiocam->mutex);

    dev_info(&client->dev, "radiocam subdev removed\n");
}

static struct i2c_driver radiocam_i2c_driver = {
    .driver = {
        .name = RADIOCAM_NAME,
        .pm = &radiocam_pm_ops,
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