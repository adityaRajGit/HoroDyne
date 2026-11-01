#!/bin/sh
# Builds and runs the PC tests with the project-local GCC (.tools/msys64, see README).
set -e
cd "$(dirname "$0")"
GXX=../.tools/msys64/ucrt64/bin/g++.exe
PATH="../.tools/msys64/ucrt64/bin:$PATH"
for t in test_battery test_charge_offset test_activity test_github; do
    "$GXX" -std=c++17 -Wall -Istubs -I../Horodyne -I../.tools/user/libraries/ArduinoJson/src -o $t.exe $t.cpp
    ./$t.exe
done
