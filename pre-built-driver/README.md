# pre-built-driver

Pre-built `radiocam.ko` + `radiocam.dtbo` pairs for a sweep of MIPI CSI-2 link
rates, so a rate can be swapped on the board without cross-compiling. Each
pair here was produced from `driver/` and `dts/` at the top of this repo with
`RADIOCAM_LINK_FREQ` / `RADIOCAM_PIXEL_RATE` / `max_fps` (and, for the
four-lane set, `RADIOCAM_LANES` and the DTS `data-lanes`) edited and
rebuilt for that specific rate.

## Layout

```
pre-built-driver/
  one-lane-driver/     data-lanes = <1>            (single MIPI lane)
    <rate>M/
      config.txt        human-readable summary of this build's parameters
      radiocam.ko        pre-built kernel module
      radiocam.dtbo       pre-built device tree overlay
    MANIFEST.txt         table of every <rate>M config in this directory
    load_driver.sh        loads one <rate>M config onto the board
    driver_status.sh     read-only status/lane check (no args)
  four-lanes-driver/    data-lanes = <1 2 3 4>       (4 MIPI lanes)
    <rate>M/, MANIFEST.txt, load_driver.sh, driver_status.sh
                          (same structure as one-lane-driver/)
```

`<rate>M` in a directory name is the **per-lane** MIPI D-PHY bit rate (Mbps),
e.g. `625M` = 625 Mbps per lane. This is *not* the same as `RADIOCAM_LINK_FREQ`
in `config.txt`/the DTS, which is half that value (the RK3588 CSI-2 D-PHY
doubles `link-frequencies` internally for DDR: `bit_rate = link_freq * 2`).

Pick `one-lane-driver/` or `four-lanes-driver/` based on how many lanes the
RadioCAM hardware is actually wired for.

## Usage

Copy the whole subdirectory (e.g. `four-lanes-driver/`) to the board, then,
as root:

```bash
# list available rates for this lane count
./load_driver.sh
# Usage: ./load_driver.sh <rate>
# Available configurations:
#   312.5
#   625
#   700
#   800
#   1000

# load a specific rate (loads the overlay, then insmod's the module)
sudo ./load_driver.sh 625
```

`load_driver.sh` does **not** unload an existing overlay/module first (that
logic is present but intentionally disabled in the script — see the
`>>> DISABLED` blocks — because unloading has real footguns; see
`driver/Note.md` at the repo root before re-enabling it). Unload the current
configuration by hand first if one is already loaded:

```bash
sudo rmmod radiocam
sudo rmdir /sys/kernel/config/device-tree/overlays/radiocam
```

then run `load_driver.sh <rate>` again. After loading, `load_driver.sh`
prints overlay status, `lsmod`, bound device/video nodes, and the live MIPI
configuration read back from the device tree (lane count, per-lane rate,
total rate).

Check the current state at any time, without loading or changing anything:

```bash
sudo ./driver_status.sh
```

This reports whether the overlay is applied, whether `radiocam.ko` is
loaded, the bound i2c device / `/dev/video*` nodes, and — read live from
`/sys/firmware/devicetree/base` (falling back to `/proc/device-tree` if
that path doesn't exist on the running kernel) — the number of MIPI lanes
in use and the per-lane line rate of whichever configuration is currently
applied.

## MANIFEST.txt

Each lane-count directory's `MANIFEST.txt` is a plain-text table of every
`<rate>M` build in that directory: link frequency, per-lane/total bit rate,
pixel rate, fps, and the `max_fps` fraction written into the driver's mode
table. Use it to pick a rate without opening each `config.txt` individually.
