#!/bin/bash
# Re-captures the corpus from the live service (needs a built library in ../build), then
# scrubs it. The committed corpus.tar.zst is what the spec's numbers were measured on.
set -e
cd "$(dirname "$0")"
mkdir -p out/new/corpus
g++ -std=c++23 -O1 -I../include cap.cpp ../build/libinnertube.a -lcurl -o out/cap
C=out/new/corpus; cap=out/cap
$cap search WEB '{"query":"lofi hip hop"}' > $C/search_lofi.json
$cap search WEB '{"query":"nasa artemis"}' > $C/search_nasa.json
$cap search WEB '{"query":"рецепт борща"}' > $C/search_ru.json
$cap browse WEB '{"browseId":"FEwhat_to_watch"}' > $C/browse_home_web.json
$cap browse TVHTML5 '{"browseId":"FEwhat_to_watch"}' > $C/browse_home_tv.json
$cap browse WEB '{"browseId":"UCLA_DiR1FfKNvjuUpBHmylQ"}' > $C/browse_channel_home.json
$cap browse WEB '{"browseId":"UCLA_DiR1FfKNvjuUpBHmylQ","params":"EgZ2aWRlb3PyBgQKAjoA"}' > $C/browse_channel_videos.json
$cap browse WEB '{"browseId":"VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"}' > $C/browse_playlist.json
$cap next WEB '{"videoId":"dQw4w9WgXcQ"}' > $C/next_rickroll.json
$cap next WEB '{"videoId":"jNQXAC9IVRw"}' > $C/next_zoo.json
for c in WEB IOS ANDROID_VR TVHTML5 VISIONOS; do $cap player $c '{"videoId":"dQw4w9WgXcQ"}' > $C/player_$c.json; done
# Continuations: the first token of each kind, then the page it opens.
python3 -I - $C <<'PY' > out/conts.sh
import json, sys
d = sys.argv[1]
def toks(o, out, ctx=""):
    if isinstance(o, dict):
        for k, v in o.items():
            if k == "continuationCommand" and isinstance(v, dict) and "token" in v: out.append((ctx, v["token"]))
            toks(v, out, ctx + "/" + k)
    elif isinstance(o, list):
        for x in o: toks(x, out, ctx)
def first(f, pred):
    out = []; toks(json.load(open(f"{d}/{f}")), out)
    return next(t for c, t in out if pred(c, t))
q = lambda t: json.dumps({"continuation": t})
print(f"out/cap next WEB '{q(first('next_rickroll.json', lambda c, t: 'itemSectionRenderer' in c and len(t) > 100))}' > {d}/next_comments.json")
print(f"out/cap next WEB '{q(first('next_rickroll.json', lambda c, t: 'secondaryResults' in c))}' > {d}/next_related_cont.json")
print(f"out/cap search WEB '{q(first('search_lofi.json', lambda c, t: True))}' > {d}/search_cont.json")
print(f"out/cap browse WEB '{q(first('browse_channel_videos.json', lambda c, t: 'richGridRenderer' in c))}' > {d}/browse_channel_videos_cont.json")
print(f"out/cap browse WEB '{q(first('browse_playlist.json', lambda c, t: len(t) > 150))}' > {d}/browse_playlist_cont.json")
PY
bash out/conts.sh
u=$(python3 -I -c "import json;print(json.load(open('$C/player_IOS.json'))['captions']['playerCaptionsTracklistRenderer']['captionTracks'][0]['baseUrl'])")
curl -s "$u&fmt=json3" -o $C/captions.json3
python3 -I scrub.py $C
echo "captured into $C; to replace the committed corpus: tar -C out/new -cf - corpus | zstd -19 -f -o corpus.tar.zst"
