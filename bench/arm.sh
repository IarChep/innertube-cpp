#!/bin/bash
# ARMv7 (Cortex-A8, NEON, hard float): cross-build yt::json (NEON and SWAR) and yyjson and
# check them against the oracle. Under qemu this proves correctness only; run the binaries
# on the device for timing. Needs TC=<armv7-eabihf toolchain dir> (e.g. Bootlin) and qemu-arm.
set -e
cd "$(dirname "$0")"
: "${TC:?set TC to an armv7-eabihf toolchain}"
SYSROOT=$(echo $TC/*-linux-gnueabihf/sysroot)
A="-std=c++17 -O2 -DNDEBUG -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard"
mkdir -p out/arm
$TC/bin/arm-linux-g++ $A -DNAME='"yt_json_neon"' -I../src c_json.cpp ../src/json/json.cpp -o out/arm/yt_json
$TC/bin/arm-linux-g++ $A -U__ARM_NEON -DNAME='"yt_json_swar32"' -I../src c_json.cpp ../src/json/json.cpp -o out/arm/yt_json_swar
$TC/bin/arm-linux-gcc -O2 -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -c ext/yyjson/src/yyjson.c -o out/arm/yyjson.o
$TC/bin/arm-linux-g++ ${A/c++17/c++23} -Iext/yyjson/src c_yyjson.cpp out/arm/yyjson.o -o out/arm/yyjson
for b in yt_json yt_json_swar yyjson; do
    ${QEMU:-qemu-arm} -L $SYSROOT out/arm/$b out/corpus out/expect 1 > out/arm/r_$b.tsv
    echo "$b: $(grep -c '	ok$' out/arm/r_$b.tsv)/$(ls out/corpus | wc -l) match the oracle"
done
