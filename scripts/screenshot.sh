# script accept a name (emulator name), looks in the asset folder for current screennshots and add new one after current series 
# (for example if user pass name "basalt", there are 4 screenshots, the last one says "basalt_4.png" the new screenhost to create should be "basalt_5.png")
# script is "pebble screenshot assets/basalt_5.png --emulator basalt". Optional second value is number, in case of override

#!/bin/bash
if [ -z "$1" ]; then
    echo "Usage: $0 <emulator_name> [screenshot_number]"
    exit 1
fi
EMULATOR_NAME=$1
SCREENSHOT_DIR="./assets"
SCREENSHOT_PREFIX="${EMULATOR_NAME}_"
SCREENSHOT_EXTENSION=".png"
if [ -n "$2" ]; then
  SCREENSHOT_NUMBER=$2
else
  SCREENSHOT_NUMBER=$(ls -1 "$SCREENSHOT_DIR"/${SCREENSHOT_PREFIX}*.png 2>/dev/null | sed 's/.*_\([0-9]*\)\.png$/\1/' | sort -n | tail -1)
  SCREENSHOT_NUMBER=$((SCREENSHOT_NUMBER + 1))
fi

SCREENSHOT_FILE="$SCREENSHOT_DIR/${SCREENSHOT_PREFIX}${SCREENSHOT_NUMBER}${SCREENSHOT_EXTENSION}"
pebble screenshot "$SCREENSHOT_FILE" --emulator "$EMULATOR_NAME"