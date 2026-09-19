#!/bin/bash
#
# Load a specific four-lane MIPI rate configuration (dtbo + ko).
#
#   ./load_driver.sh 625      -> loads 625M/radiocam.dtbo and 625M/radiocam.ko
#
# Run as root on the target board. The <rate>M/ directories must sit next to
# this script.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OVERLAY_NAME="radiocam"
OVERLAY_PATH="/sys/kernel/config/device-tree/overlays/$OVERLAY_NAME"
MODULE="radiocam"
DRIVER_SYSFS="/sys/bus/i2c/drivers/$MODULE"

die() { echo "ERROR: $*" >&2; exit 1; }

# ---------------------------------------------------------------- arguments
if [ $# -ne 1 ]; then
    echo "Usage: $0 <rate>"
    echo
    echo "Available configurations:"
    for d in "$SCRIPT_DIR"/*M/; do
        [ -d "$d" ] || continue
        printf '  %s\n' "$(basename "$d" M)"
    done | sort -n
    exit 1
fi

RATE="${1%M}"
CFG_DIR="$SCRIPT_DIR/${RATE}M"
DTBO="$CFG_DIR/radiocam.dtbo"
KO="$CFG_DIR/radiocam.ko"

[ -d "$CFG_DIR" ] || die "no such configuration: ${RATE}M (looked in $CFG_DIR)"
[ -f "$DTBO" ]    || die "missing $DTBO"
[ -f "$KO" ]      || die "missing $KO"
[ "$(id -u)" -eq 0 ] || die "must run as root"

echo "=== Loading ${RATE} Mbps configuration ==="
[ -f "$CFG_DIR/config.txt" ] && sed 's/^/  /' "$CFG_DIR/config.txt"
echo

# >>> DISABLED: safety check: open /dev/i2c-* handles
#     Kept for later use. Reason it exists: an open i2c-dev fd makes i2c_del_adapter() hang uninterruptibly on unload.
#     Re-enable by stripping the leading '# ' from the block below.
# # ------------------------------------------------- safety: open i2c handles
# # An open /dev/i2c-N on the synthetic radiocam-i2c adapter holds a reference
# # that makes i2c_del_adapter() block forever in uninterruptible sleep during
# # radiocam_remove(). That wedges the machine until reboot, so refuse to
# # unload anything while such a handle exists.
# if command -v fuser >/dev/null 2>&1; then
#     if fuser /dev/i2c-* >/dev/null 2>&1; then
#         echo "Processes holding /dev/i2c-*:"
#         fuser -v /dev/i2c-* 2>&1 | sed 's/^/  /'
#         die "close these before loading, or unloading will hang the kernel"
#     fi
# else
#     echo "WARNING: fuser not found, cannot check for open /dev/i2c-* handles"
# fi
# <<< END DISABLED

# >>> DISABLED: unload driver
#     Kept for later use. Reason it exists: unbind is required first: rkcif holds a module ref.
#     Re-enable by stripping the leading '# ' from the block below.
# # ------------------------------------------------------------ unload driver
# if lsmod | grep -q "^$MODULE "; then
#     echo "--- unloading existing driver ---"
#     # Unbind first: rkcif holds a module reference via v4l2_device_register_subdev()
#     # for as long as the subdev is registered, so rmmod alone returns EBUSY.
#     if [ -d "$DRIVER_SYSFS" ]; then
#         for dev in "$DRIVER_SYSFS"/*-*; do
#             [ -e "$dev" ] || continue
#             name="$(basename "$dev")"
#             echo "unbinding $name"
#             echo "$name" > "$DRIVER_SYSFS/unbind" || die "unbind $name failed"
#         done
#     fi
#     rmmod "$MODULE" || die "rmmod failed (refcnt=$(cat /sys/module/$MODULE/refcnt 2>/dev/null))"
#     echo "driver unloaded"
# fi
# <<< END DISABLED

# >>> DISABLED: unload overlay
#     Kept for later use. Reason it exists: removing the overlay triggers radiocam_remove().
#     Re-enable by stripping the leading '# ' from the block below.
# # ----------------------------------------------------------- unload overlay
# if [ -d "$OVERLAY_PATH" ]; then
#     echo "--- removing existing overlay ---"
#     rmdir "$OVERLAY_PATH" || die "rmdir $OVERLAY_PATH failed"
#     echo "overlay removed"
# fi
# <<< END DISABLED

# ------------------------------------------------------------- load overlay
echo "--- loading overlay ---"
mkdir "$OVERLAY_PATH" || die "mkdir $OVERLAY_PATH failed"
cat "$DTBO" > "$OVERLAY_PATH/dtbo" || die "writing dtbo failed"

# ------------------------------------------------------------- load driver
echo "--- loading driver ---"
insmod "$KO" || die "insmod $KO failed"

# ------------------------------------------------------------ verification
echo
echo "=== 1. overlay status ($OVERLAY_PATH) ==="
ls -la "$OVERLAY_PATH"
STATUS="$(cat "$OVERLAY_PATH/status" 2>/dev/null)"
echo "status: ${STATUS:-<unreadable>}"
if [ "$STATUS" != "applied" ]; then
    die "overlay not applied (status=${STATUS:-unknown})"
fi
echo "OK: overlay applied"

echo
echo "=== 2. lsmod ==="
if lsmod | grep "^$MODULE "; then
    echo "OK: driver loaded"
else
    die "$MODULE not present in lsmod"
fi

echo
echo "=== 3. bound device / video nodes ==="
ls "$DRIVER_SYSFS" 2>/dev/null | grep -E '^[0-9]+-[0-9a-f]+$' | sed 's/^/  bound: /' \
    || echo "  (no device bound yet)"
ls /dev/video* /dev/media* 2>/dev/null | sed 's/^/  /' || echo "  (no video nodes)"

# --------------------------------------- MIPI config read back from device tree
echo
echo "=== 4. MIPI configuration (read from live device tree) ==="
# /sys/firmware/devicetree/base is the modern interface and is kept in sync
# when a configfs overlay is applied/removed at runtime. /proc/device-tree is
# a legacy alias that on some kernels only reflects the tree as booted, so it
# is tried second, as a fallback only.
DT_ROOT=""
RADIOCAM_NODE=""
for candidate in /sys/firmware/devicetree/base /proc/device-tree; do
    [ -d "$candidate" ] || continue
    found="$(find "$candidate" -maxdepth 8 -type d -name 'radiocam@28' 2>/dev/null | head -1)"
    if [ -n "$found" ]; then
        DT_ROOT="$candidate"
        RADIOCAM_NODE="$found"
        break
    fi
    [ -z "$DT_ROOT" ] && DT_ROOT="$candidate"
done

if [ -z "$DT_ROOT" ]; then
    echo "  WARNING: no live device-tree filesystem found (checked /sys/firmware/devicetree/base and /proc/device-tree)"
fi

if [ -z "${RADIOCAM_NODE:-}" ]; then
    [ -n "$DT_ROOT" ] && echo "  WARNING: radiocam@28 node not found under /sys/firmware/devicetree/base or /proc/device-tree (overlay not applied?)"
    [ -n "$DT_ROOT" ] && echo "  hint: try 'sudo find /sys/firmware/devicetree/base /proc/device-tree -iname \"*radiocam*\" 2>/dev/null' to see what is actually there"
else
    DT_EP="$RADIOCAM_NODE/port/endpoint"
    if [ ! -d "$DT_EP" ]; then
        # node layout differs from what we expect (e.g. endpoint got a unit
        # address) -- fall back to a recursive search under the sensor node
        DT_EP="$(find "$RADIOCAM_NODE" -type d -name 'endpoint*' 2>/dev/null | head -1)"
    fi
fi

if [ -z "${DT_EP:-}" ] || [ ! -d "${DT_EP:-/nonexistent}" ]; then
    if [ -n "${RADIOCAM_NODE:-}" ]; then
        echo "  WARNING: no endpoint node found under $RADIOCAM_NODE"
        echo "  actual layout:"
        find "$RADIOCAM_NODE" -maxdepth 3 2>/dev/null | sed 's/^/    /'
    fi
else
    echo "  node           : $DT_EP"

    NLANES=""
    if [ -r "$DT_EP/data-lanes" ]; then
        # data-lanes is an array of big-endian u32, one entry per data lane
        HEX="$(od -An -tx1 -v "$DT_EP/data-lanes" | tr -d ' \n')"
        NLANES=$(( ${#HEX} / 8 ))
        LANE_LIST=""; i=0
        while [ $i -lt "$NLANES" ]; do
            LANE_LIST="$LANE_LIST $((16#${HEX:$((i*8)):8}))"
            i=$((i+1))
        done
        echo "  data-lanes     :$LANE_LIST"
        echo "  lane count     : $NLANES"
    else
        echo "  WARNING: data-lanes not readable"
    fi

    if [ -r "$DT_EP/link-frequencies" ]; then
        # link-frequencies is big-endian u64; per-lane bit rate = link_freq * 2 (DDR)
        LFHEX="$(od -An -tx1 -v "$DT_EP/link-frequencies" | tr -d ' \n')"
        LF=$(( 16#${LFHEX:0:16} ))
        BITRATE=$(( LF * 2 ))
        echo "  link-frequency : $LF Hz"
        echo "  per-lane rate  : $(awk -v v="$BITRATE" 'BEGIN{printf "%.1f", v/1e6}') Mbps"
        if [ -n "$NLANES" ]; then
            echo "  total rate     : $(awk -v v="$((BITRATE * NLANES))" 'BEGIN{printf "%.1f", v/1e6}') Mbps"
        fi
    else
        echo "  WARNING: link-frequencies not readable"
    fi
fi

# cross-check: what the csi2-dphy driver actually programmed
DPHY_LINE="$(dmesg 2>/dev/null | grep -a 'data_rate_mbps' | tail -1)"
[ -n "$DPHY_LINE" ] && echo "  kernel dphy    : ${DPHY_LINE#*] }"

echo
echo "Done: ${RATE} Mbps configuration loaded."
