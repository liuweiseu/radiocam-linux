# Notes: unloading / reloading the driver at runtime

Reference kernel: `orangepi-build/kernel/orange-pi-6.1-rk35xx`.

Once `radiocam.ko` is loaded against an applied `radiocam.dtbo`, a plain
`rmmod radiocam` does **not** work, and a careless attempt can wedge the machine
until the next reboot. There are two *separate* failure modes with two different
causes. Both are explained below, followed by the procedures that actually work.

---

## Failure mode 1: `rmmod: ERROR: Module radiocam is in use`

### Cause

The V4L2 core deliberately takes a reference on a sub-device's module while that
sub-device is registered to a media device. From
`drivers/media/v4l2-core/v4l2-device.c`, `v4l2_device_register_subdev()`:

```c
	/*
	 * The reason to acquire the module here is to avoid unloading
	 * a module of sub-device which is registered to a media
	 * device. ...
	 */
	sd->owner_v4l2_dev = v4l2_dev->dev && v4l2_dev->dev->driver &&
		sd->owner == v4l2_dev->dev->driver->owner;

	if (!sd->owner_v4l2_dev && !try_module_get(sd->owner))
		return -ENODEV;
```

The chain for this driver is:

1. `radiocam_probe()` calls `v4l2_i2c_subdev_init()`, which sets
   `sd->owner = THIS_MODULE` (i.e. `radiocam.ko`).
2. `radiocam_probe()` calls `v4l2_async_register_subdev_sensor()`.
3. The Rockchip `rkcif` bridge (the `mipi2_csi2` / `rkcif_mipi_lvds2` nodes the
   overlay enables) matches its async notifier and calls
   `v4l2_device_register_subdev()`, which does `try_module_get(radiocam.ko)`.
4. `radiocam.ko` refcount becomes 1, so `rmmod` refuses.

The `sd->owner_v4l2_dev` exemption does not apply here, because `radiocam.ko` and
`rkcif` are different modules.

This happens entirely inside the kernel as soon as the driver probes — no
userspace program has to touch anything.

### Why it is hard to diagnose

The reference is a runtime `try_module_get()`, not a symbol dependency, so:

```
cat /sys/module/radiocam/refcnt     # -> 1
ls   /sys/module/radiocam/holders/  # -> empty (rkcif does NOT show up here)
```

The refcount is non-zero but nothing appears to be holding it.

### Fix

Make `radiocam_remove()` run, which calls `v4l2_async_unregister_subdev()` ->
`v4l2_async_cleanup()` -> `v4l2_device_unregister_subdev()` -> `module_put()`.
See the procedures below.

---

## Failure mode 2: unbind / overlay removal hangs forever (reboot required)

### Cause

`radiocam_remove()` starts with `i2c_del_adapter(&radiocam->adapter)` (the
synthetic `radiocam-i2c` adapter that exposes raw I2C to userspace). From
`drivers/i2c/i2c-core-base.c`, `i2c_del_adapter()` ends with:

```c
	/* wait until all references to the device are gone ... */
	init_completion(&adap->dev_released);
	device_unregister(&adap->dev);
	wait_for_completion(&adap->dev_released);
```

`wait_for_completion()` is **TASK_UNINTERRUPTIBLE** — the task cannot be killed,
not even with `SIGKILL`.

The adapter reference that blocks it comes from the i2c-dev char device
(`CONFIG_I2C_CHARDEV=y` in this kernel, so `/dev/i2c-N` always exists for the
synthetic adapter). From `drivers/i2c/i2c-dev.c`:

* `i2cdev_open()`  -> `i2c_get_adapter()` -> `try_module_get(adap->owner)` **and**
  `get_device(&adap->dev)`
* `i2cdev_release()` -> `i2c_put_adapter()` — only on `close()`

So **any process holding `/dev/i2c-N` open** (a python-periphery script, an
interactive Python session, a crashed-but-not-reaped process, ...) will make
`radiocam_remove()` block forever in D state.

Worse: the overlay-removal path holds hotplug/OF locks while this happens, so
every later operation touching the DT overlay or that i2c bus blocks too. At that
point **only a reboot recovers the machine.**

Note this path is *not* gated by the module refcount — `rmdir` on the overlay and
sysfs `unbind` both call `.remove()` regardless of failure mode 1.

### Fix

Close every `/dev/i2c-*` fd **before** unbinding or removing the overlay.

---

## Procedure A: reload the driver without touching the overlay (preferred)

Use this when iterating on `radiocam.ko`, or to re-run `probe` after changing MCU
side configuration.

```bash
# 0. MANDATORY: nothing may hold the i2c-dev / video nodes open
fuser -v /dev/i2c-* /dev/video* /dev/media*   # must print nothing

# 1. find the i2c device name (i2c7 + 0x28 -> usually 7-0028)
ls /sys/bus/i2c/drivers/radiocam/

# 2. unbind -> runs radiocam_remove() -> module_put()
echo 7-0028 > /sys/bus/i2c/drivers/radiocam/unbind

# 3. verify the reference was released
cat /sys/module/radiocam/refcnt               # must be 0

# 4. now the module can be unloaded / reloaded
sudo rmmod radiocam
sudo insmod radiocam.ko
```

To just re-run `probe` without unloading the module at all, skip steps 3-4 and
re-bind:

```bash
echo 7-0028 > /sys/bus/i2c/drivers/radiocam/bind
```

## Procedure B: full unload (driver + overlay)

Use this when the `.dts` itself changed and a new `.dtbo` must be applied.

```bash
# 0. MANDATORY: nothing may hold the i2c-dev / video nodes open
fuser -v /dev/i2c-* /dev/video* /dev/media*   # must print nothing

# 1. remove the overlay first -- NOT gated by the module refcount,
#    and it triggers radiocam_remove()
sudo rmdir /sys/kernel/config/device-tree/overlays/radiocam

# 2. then the module unloads cleanly
sudo rmmod radiocam

# 3. reload in the reverse order: overlay, then driver
```

Order matters: `rmmod` before removing the overlay always fails with
`Module is in use` (failure mode 1).

---

## Diagnostics

```bash
lsmod | grep radiocam                 # "Used by" column
cat /sys/module/radiocam/refcnt       # 1 while rkcif holds the subdev
ls  /sys/module/radiocam/holders/     # empty by design, see failure mode 1
fuser -v /dev/i2c-*                   # who blocks i2c_del_adapter()
ls /sys/bus/i2c/drivers/radiocam/     # bound device name for bind/unbind
dmesg | grep -i radiocam
```

If an unbind / `rmdir` is already stuck in D state, dump the blocked tasks from
another terminal:

```bash
echo w > /proc/sysrq-trigger
dmesg | tail -50                      # look for i2c_del_adapter / wait_for_completion
```

Nothing recovers the machine from that state except a reboot.

**Do not use `rmmod -f`.** This kernel does set `CONFIG_MODULE_FORCE_UNLOAD=y`, so
the command exists, but force-unloading only skips the refcount check while the
stale references remain live — the result is a use-after-free and a kernel panic,
not a recovered system.
