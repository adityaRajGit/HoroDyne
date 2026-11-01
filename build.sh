#!/bin/sh
# Compile the watch firmware; "sh build.sh upload" also flashes it (PORT=COM3 by default).
# Every run bumps BUILD_NUMBER and stamps the time in Horodyne/build_info.h, so the splash and
# Settings > About show exactly which build is on the watch. Paths match the README so the
# arduino-cli cache stays valid (a different spelling rebuilds every library: ~45 min).
set -e
cd "$(dirname "$0")"
INFO=Horodyne/build_info.h
N=$(( $(sed -n 's/^#define BUILD_NUMBER //p' "$INFO") + 1 ))
STAMP=$(date '+%d %b %Y %H:%M' | tr '[:lower:]' '[:upper:]')
printf '// Written by build.sh before every compile. Do not edit.\n#pragma once\n#define BUILD_NUMBER %d\n#define BUILD_STAMP "%s"\n' "$N" "$STAMP" > "$INFO"
echo "build $N ($STAMP)"
arduino-cli --config-file D:/Desktop/Projects/Horodyne/.tools/arduino-cli.yaml compile --jobs 2 \
  --build-path D:/Desktop/Projects/Horodyne/.tools/build \
  -b "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc" \
  --library horodyne_lvconf Horodyne
if [ "$1" = upload ]; then
  python -m esptool --chip esp32s3 -p "${PORT:-COM3}" -b 921600 --before usb-reset write-flash 0x10000 .tools/build/Horodyne.ino.bin
fi
