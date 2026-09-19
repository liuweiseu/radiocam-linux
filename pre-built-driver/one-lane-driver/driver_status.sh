#!/bin/bash
#
# Check whether the radiocam driver/overlay is currently loaded, and if so,
# report the MIPI lane count and per-lane line rate from the live device tree.
#
#   ./driver_status.sh
#
# Run as root (or any user that can read /sys and /proc/device-tree) on the
# target board. Read-only: does not load, unload, or modify anything.

set -u

OVERLAY_NAME="radiocam"
OVERLAY_PATH="/sys/kernel/config/device-tree/overlays/$OVERLAY_NAME"
MODULE="radiocam"
DRIVER_SYSFS="/sys/bus/i2c/drivers/$MODULE"

echo "=== 1. overlay status ($OVERLAY_PATH) ==="
if [ -d "$OVERLAY_PATH" ]; then
    STATUS="$(cat "$OVERLAY_PATH/status" 2>/dev/null)"
    echo "  overlay present, status: ${STATUS:-<unreadable>}"
else
    echo "  overlay not applied"
fi

echo
echo "=== 2. lsmod ==="
if lsmod | grep "^$MODULE "; then
    echo "  OK: driver loaded"
else
    echo "  NOT loaded"
fi

echo
echo "=== 3. bound device / video nodes ==="
if [ -d "$DRIVER_SYSFS" ]; then
    ls "$DRIVER_SYSFS" 2>/dev/null | grep -E '^[0-9]+-[0-9a-f]+$' | sed 's/^/  bound: /' \
        || echo "  (no device bound yet)"
else
    echo "  (driver not registered)"
fi
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
