#!/bin/bash

# Overlay name
OVERLAY_NAME="radiocam"
OVERLAY_PATH="/sys/kernel/config/device-tree/overlays/$OVERLAY_NAME"
DTBO_PATH="$OVERLAY_NAME.dtbo"

# 1️⃣ delete overlay if exists
if [ -d "$OVERLAY_PATH" ]; then
    echo "Overlay '$OVERLAY_NAME' exists. Removing..."
    rmdir "$OVERLAY_PATH" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "Overlay '$OVERLAY_NAME' removed successfully."
    else
        echo "Failed to remove overlay '$OVERLAY_NAME'. Are you running as root?"
        exit 1
    fi
else
    echo "Overlay '$OVERLAY_NAME' does not exist."
fi

# 2️⃣ re-create overlay
echo "Creating overlay directory..."
mkdir "$OVERLAY_PATH" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Failed to create overlay directory '$OVERLAY_PATH'. Are you running as root?"
    exit 1
fi

# 3️⃣ load DTBO
echo "Loading DTBO..."
cat "$DTBO_PATH" > "$OVERLAY_PATH/dtbo"
if [ $? -eq 0 ]; then
    echo "DTBO loaded successfully."
else
    echo "Failed to load DTBO. Check path: $DTBO_PATH"
    exit 1
fi

# 4️⃣ check status
if [ -f "$OVERLAY_PATH/status" ]; then
    STATUS=$(cat "$OVERLAY_PATH/status")
    echo "Overlay '$OVERLAY_NAME' status: $STATUS"
    if [ "$STATUS" = "applied" ]; then
        echo "Overlay '$OVERLAY_NAME' applied successfully."
    else
        echo "Overlay '$OVERLAY_NAME' did not apply correctly."
    fi
else
    echo "No status file found for overlay '$OVERLAY_NAME'."
fi