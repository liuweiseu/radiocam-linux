# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Linux kernel driver + device tree overlay for RadioCAM, a custom MIPI CSI-2 camera/sensor
module built around an on-board MCU, targeting an Orange Pi 5 (Rockchip RK3588) running the
`orangepi-build` kernel (`orange-pi-6.1-rk35xx`). Everything here is cross-compiled on a dev
machine and deployed to the SBC over SSH — there is no on-target build step.

## Build commands

All builds require a pre-built target kernel source tree; cross-compile env vars live in
`env.sh` (`source env.sh` sets `ARCH=arm64` and `CROSS_COMPILE=aarch64-linux-gnu-`).

**Device tree overlay** (`dts/`):
```
cd dts
make            # produces radiocam.dtbo from radiocam.dts (uses `dtc`)
make clean
```

**Kernel driver** (`driver/`):
```
cd driver
make                 # normal build
make DEBUG=1         # build with -DDEBUG (enables dev_dbg logging)
make clean
```
`driver/Makefile` hardcodes `KDIR` to a local path of the compiled orangepi-build kernel tree
(`.../orangepi-build/kernel/orange-pi-6.1-rk35xx`) — update it to point at your own compiled
kernel source before building.

There is no userspace `app/` yet; the root README lists it as TODO.

## Deploying / testing on the SBC

Artifacts (`radiocam.dtbo`, `radiocam.ko`) are `scp`'d to the board; there's no remote build.

Load/unload the overlay at runtime (as root on the board):
```
mkdir /sys/kernel/config/device-tree/overlays/radiocam
cat radiocam.dtbo > /sys/kernel/config/device-tree/overlays/radiocam/dtbo
# ...
rmdir /sys/kernel/config/device-tree/overlays/radiocam
```
`dts/scripts/load_dtbo.sh` / `unload_dtbo.sh` wrap this. To persist across boot, copy the dtbo
to `/boot/dtb/rockchip/overlay/rk3588-radiocam.dtbo` and enable it via `orangepi-config` (see
`dts/README.md`).

Load/unload the driver at runtime:
```
sudo insmod radiocam.ko
sudo rmmod radiocam
```
To load at boot instead, copy `radiocam.ko` into `/lib/modules/$(uname -r)/kernel/drivers/sdr/`,
run `depmod -a`, and add `radiocam` to `/etc/modules-load.d/radiocam.conf` (see
`driver/README.md`) — needed if you want the V4L2 framework to pick it up automatically.

`driver/gp.sh` and `dts/gp.sh` are personal helper scripts that copy build artifacts into a
separate `~/Projects/opi-driver` repo; not part of the normal build/test flow.

## Architecture

**`dts/radiocam.dts`** — a device tree overlay with several fragments that: (1) instantiate the
`ucb-ral,radiocam` I2C device at `i2c7@0x28`, whose MIPI `port` endpoint links to the RK3588's
CSI2 dphy0 (`csi2_dphy0`) via a matching `mipi_in_ucam2` endpoint (`data-lanes = <1 2 3 4>`,
`link-frequencies = 156250000`), and (2) enable the downstream `csi2_dphy0_hw`, `mipi2_csi2`,
`rkcif_mipi_lvds2`, `rkcif`, and `rkcif_mmu` nodes that the Rockchip camera pipeline needs to
actually capture from that CSI-2 lane.

**`driver/radiocam.c`** — a single `i2c_driver` (`compatible = "ucb-ral,radiocam"`) that binds to
the I2C address defined in the DTS and does two independent things with that same `i2c_client`:

1. **Registers a V4L2 subdev** (`v4l2_async_register_subdev_sensor`) implementing the standard
   core/video/pad ops (`s_stream`, `enum_mbus_code`, `enum_frame_size`, `enum_frame_interval`,
   `get_fmt`/`set_fmt`, `g_mbus_config`) so the Rockchip `rkcif` capture pipeline can drive it as
   a camera sensor. Supported modes live in the `supported_modes[]` table (currently one fixed
   mode: 2048x2556, `MEDIA_BUS_FMT_SBGGR8_1X8`). Power management (`s_power`,
   runtime suspend/resume) and most of the custom ioctl (`RADIOCAM_GET_STATUS`/`SET_MODE`) are
   still stubs — treat them as unimplemented, not as reference behavior.

2. **Registers a second, synthetic `i2c_adapter`** (`radiocam-i2c`, via `i2c_add_adapter`) purely
   so userspace can issue raw I2C transfers to the MCU. This exists because the kernel's
   `i2c-dev` layer refuses `I2C_RDWR` ioctls to an address that already has a kernel driver
   bound (`-EBUSY`) — see commit `c19cd89`. `radiocam_i2c_master_xfer` validates the target
   address matches the bound client, then forwards the transfer to the real
   `radiocam->client->adapter` under `radiocam->mutex`. Its `functionality` op must keep
   returning `I2C_FUNC_I2C`; `python-periphery` (and other host-side I2C libs) probe this during
   init and treat anything else as a hard failure (see commit `f32a552`).

**Low-level MCU protocol** (`radiocam_read_reg`/`radiocam_write_reg` in `radiocam.c`): the MCU
exposes a register-style interface addressed by a `dev_id` byte plus a 32-bit register address.
A transaction is a 10-byte write (`dev_id`, r/w flag, 4-byte addr, 4-byte value) followed by an
~8ms delay and a 4-byte read of the result/echo. `dev_id` values and per-device register offsets
(`RADIOCAM_DEV_SYSMON`, `RADIOCAM_DEV_MIPI`, ...) are defined in `radiocam.h` and **must stay in
sync with `choosecmd.h`/`firmware_version.h` in the separate `radiocam-firmware` repo** — don't
renumber them independently. Full wire protocol:
https://github.com/liuweiseu/hercules-i2c-demo/wiki/com-protocol.

**`driver/radiocam.h`** — shared constants: custom V4L2 subdev ioctl codes, MCU device/register
IDs (must match firmware), and the custom V4L2 control ID `V4L2_CID_RADIOCAM_SETTING`. The
`V4L2_CTRL_CLASS_USER`/`V4L2_CID_BASE`/`V4L2_CID_USER_BASE` macros are guarded with `#ifndef`
because they generally already exist in kernel headers, which must take precedence.

**`test/`** — a bare `helloworld.c` kernel module used as a minimal build/load sanity check
(not a test of RadioCAM functionality); its Makefile's `KDIR` points at `../linux`, which is
independent of `driver/Makefile`'s `KDIR`.
