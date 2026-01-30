#!/bin/bash

OVERLAY_NAME="radiocam"
OVERLAY_PATH="/sys/kernel/config/device-tree/overlays/$OVERLAY_NAME"

if [ -d "$OVERLAY_PATH" ]; then
    echo "Overlay '$OVERLAY_NAME' exists. Unloading..."
    rmdir "$OVERLAY_PATH" 2>/dev/null

    if [ $? -eq 0 ]; then
        echo "Overlay '$OVERLAY_NAME' unloaded successfully."
    else
        echo "Failed to unload overlay '$OVERLAY_NAME'. Are you running as root?"
        exit 1
    fi
else
    echo "Overlay '$OVERLAY_NAME' does not exist. Nothing to do."
fi