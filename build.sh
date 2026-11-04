#!/bin/sh
# Compile the watch firmware; "PORT=COM3 sh build.sh upload" also flashes it.
# Every run bumps BUILD_NUMBER and stamps the time in Horodyne/build_info.h, so the splash and
# Settings > About show exactly which build is on the watch.
set -e
cd "$(dirname "$0")"
# Optional and not in git: CONFIG, BUILD, JOBS or PORT for this machine. Keep BUILD's spelling once
# it has a cache: a different path makes arduino-cli rebuild every library (~45 min on a slow PC).
[ -f .tools/local.sh ] && . ./.tools/local.sh
BUILD=${BUILD:-build}
INFO=Horodyne/build_info.h
N=$(( $(sed -n 's/^#define BUILD_NUMBER //p' "$INFO") + 1 ))
STAMP=$(date '+%d %b %Y %H:%M' | tr '[:lower:]' '[:upper:]')
OLD_INFO=$(cat "$INFO")   # put back if the compile fails: a failed build keeps the number
printf '// Written by build.sh before every compile. Do not edit.\n#pragma once\n#define BUILD_NUMBER %d\n#define BUILD_STAMP "%s"\n' "$N" "$STAMP" > "$INFO"
echo "build $N ($STAMP)"
arduino-cli ${CONFIG:+--config-file "$CONFIG"} compile --jobs "${JOBS:-2}" \
  --build-path "$BUILD" \
  -b "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc" \
  --library horodyne_lvconf Horodyne || { printf '%s\n' "$OLD_INFO" > "$INFO"; exit 1; }
# App only, at 0x10000. A partition table change needs the full image flashed at 0x0.
if [ "$1" = upload ]; then
  python -m esptool --chip esp32s3 -p "${PORT:?set PORT, for example PORT=COM3 sh build.sh upload}" -b 921600 --before usb-reset write-flash 0x10000 "$BUILD/Horodyne.ino.bin"
fi
