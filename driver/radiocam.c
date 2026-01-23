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

#define DEBUG

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
    const struct ov13850_mode *cur_mode;
    u32 module_index;
    const char *module_facing;
    const char *module_name;
    const char *len_name;
};

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
    // print out the driver version
    dev_info(dev, "driver version: %02x.%02x.%02x",
             DRIVER_VERSION >> 16,
             (DRIVER_VERSION & 0xff00) >> 8,
             DRIVER_VERSION & 0x00ff);
    radiocam = devm_kzalloc(dev, sizeof(*radiocam), GFP_KERNEL);
    if (!radiocam)
        return -ENOMEM;
    // save i2c client to radiocam struct
    radiocam->client = client;
    u32 val;
    radocam_read_reg(client, 0x05, 0x00, &val);
    dev_info(&client->dev, "Read reg(0x%x, 0x%x): 0x%x\n", 05, 0, val);
    radiocam_write_reg(client, 0x01, 0x01, 0);
    return 0;
}

static void radiocam_remove(struct i2c_client *client)
{
    printk("Bye!\n");
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