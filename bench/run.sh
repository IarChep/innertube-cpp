#!/bin/bash
# ./run.sh [ms per document]: unpack the corpus, regenerate the oracle's dumps, build, run
# every candidate on one pinned core (a mismatch with the oracle fails the candidate), and
# print the summary table the spec quotes.
set -e
cd "$(dirname "$0")"
[ -d ext ] || ./fetch.sh
[ -d out/corpus ] || { mkdir -p out && zstd -dc corpus.tar.zst | tar -C out -x; }
mkdir -p out/expect && OUT=out/expect python3 -I oracle.py out/corpus/*
./build.sh
g++ -std=c++17 -O2 -I../src suite.cpp ../src/json/json.cpp -o out/suite && out/suite
rm -f out/r_*.tsv
for b in yt_json yt_json_swar sonic simdjson_od simdjson_dom yyjson rapidjson; do
    taskset -c 2 out/$b out/corpus out/expect ${1:-300} > out/r_$b.tsv || echo "$b: MISMATCH against the oracle"
done
SJ_IMPL=fallback taskset -c 2 out/simdjson_od_fallback out/corpus out/expect ${1:-300} > out/r_simdjson_od_fallback.tsv
SJ_IMPL=fallback taskset -c 2 out/simdjson_dom out/corpus out/expect ${1:-300} > out/r_simdjson_dom_fallback.tsv
python3 -I summarize.py out/r_*.tsv
