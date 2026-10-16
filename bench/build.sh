#!/bin/bash
# Builds every candidate into out/ (x86-64 with AVX2 host; see arm.sh for ARMv7).
set -e
cd "$(dirname "$0")"
E=ext; S=../src; mkdir -p out
# simdjson.o dispatches at run time; only the On-Demand TU (compile-time kernel) and
# sonic need PCLMUL — mixing kernels between TUs of one binary crashes.
F="-std=c++23 -O2 -DNDEBUG -march=x86-64-v3"
gcc -O2 -DNDEBUG -march=x86-64-v3 -c $E/yyjson/src/yyjson.c -o out/yyjson.o
[ -f out/simdjson.o ] || g++ $F -c $E/simdjson/singleheader/simdjson.cpp -o out/simdjson.o
g++ $F -DNAME='"yt_json_sse2"' -I$S c_json.cpp $S/json/json.cpp -o out/yt_json
g++ $F -U__SSE2__ -DNAME='"yt_json_swar"' -I$S c_json.cpp $S/json/json.cpp -o out/yt_json_swar
g++ $F -I$E/yyjson/src c_yyjson.cpp out/yyjson.o -o out/yyjson
g++ $F -I$E/simdjson/singleheader c_simdjson_dom.cpp out/simdjson.o -o out/simdjson_dom
g++ $F -mpclmul -I$E/simdjson/singleheader c_ondemand.cpp out/simdjson.o -o out/simdjson_od
g++ $F -I$E/simdjson/singleheader c_ondemand.cpp out/simdjson.o -o out/simdjson_od_fallback
g++ $F -I$E/rapidjson/include c_rapidjson.cpp -o out/rapidjson
g++ $F -mpclmul -w -I$E/sonic-cpp/include c_sonic.cpp -o out/sonic
