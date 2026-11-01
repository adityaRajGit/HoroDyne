#!/bin/sh
# Builds and runs the PC tests. Needs g++ with C++17 and ArduinoJson; set ARDUINOJSON to its src
# folder if it isn't in your Arduino libraries folder.
set -e
cd "$(dirname "$0")"
# The Windows setup in .tools (not in git) has its own compiler and libraries.
[ -d ../.tools/msys64/ucrt64/bin ] && PATH="$(pwd)/../.tools/msys64/ucrt64/bin:$PATH"
JSON=${ARDUINOJSON:-../.tools/user/libraries/ArduinoJson/src}
[ -d "$JSON" ] || JSON=$HOME/Arduino/libraries/ArduinoJson/src
for t in test_battery test_charge_offset test_activity test_github; do
    g++ -std=c++17 -Wall -Istubs -I../Horodyne -I"$JSON" -o $t.exe $t.cpp
    ./$t.exe
done
