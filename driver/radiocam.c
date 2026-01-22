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

#define RADIOCAM_NAME			"radiocam"

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id radiocam_of_match[] = {
	{ .compatible = "ucb-ral,radiocam" },
	{},
};
MODULE_DEVICE_TABLE(of, radiocam_of_match);
#endif

/*
* function: radiocam_runtime_suspend
* brief: power management
*/
static int __maybe_unused radiocam_runtime_suspend(struct device *dev)
{
	// struct i2c_client *client = to_i2c_client(dev);
	// struct v4l2_subdev *sd = i2c_get_clientdata(client);
	// struct ov13850 *ov13850 = to_ov13850(sd);

	// __ov13850_power_off(ov13850);
    // TODO: implement this function
	return 0;
}

/*
* function: radiocam_runtime_resume
* brief: power management
*/
static int __maybe_unused radiocam_runtime_resume(struct device *dev)
{
	// struct i2c_client *client = to_i2c_client(dev);
	// struct v4l2_subdev *sd = i2c_get_clientdata(client);
	// struct ov13850 *ov13850 = to_ov13850(sd);

	// return __ov13850_power_on(ov13850);
    // TODO: implement this function
    return 0;
}

static const struct i2c_device_id radiocam_match_id[] = {
	{ "ucb-ral,radiocam", 0 },
	{ },
};

static const struct dev_pm_ops radiocam_pm_ops = {
	SET_RUNTIME_PM_OPS(radiocam_runtime_suspend,
			   radiocam_runtime_resume, NULL)
};

static int radiocam_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
    printk("Hello World!\n");
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
	.probe		= &radiocam_probe,
	.remove		= &radiocam_remove,
	.id_table	= radiocam_match_id,
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