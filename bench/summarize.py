# Summary of r_*.tsv: per candidate, per document group. Times are sums of per-doc medians.
import sys, glob, collections
rows = [l.rstrip("\n").split("\t") for f in sorted(sys.argv[1:] or glob.glob("r_*.tsv")) for l in open(f)]
def grp(doc):
    if doc.startswith("player_"): return "player"
    if doc.endswith(".json3"): return "json3"
    if doc.startswith("next_comments"): return "comments"
    if doc.startswith("next_"): return "next"
    if doc.startswith("search"): return "search"
    return "browse"
G = ["browse", "search", "next", "comments", "player", "json3"]
agg = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0, 0, 0, 0, 0]))
for name, doc, size, ns, cyc, ins, allocs, peak, ok in rows:
    for g in (grp(doc), "ALL"):
        a = agg[name][g]
        a[0] += int(size); a[1] += float(ns); a[2] += int(cyc); a[3] += int(ins); a[4] += int(allocs); a[5] = max(a[5], int(peak) / int(size)) if int(size) > 100000 else a[5]
base = agg.get("cur_scan_glaze")
print(f"{'candidate':24}" + "".join(f"{g:>10}" for g in G) + f"{'ALL us':>10}{'MB/s':>8}{'IPC':>6}{'allocs':>8}{'peak/doc':>9}")
for name in sorted(agg, key=lambda n: agg[n]["ALL"][1]):
    a = agg[name]
    cells = "".join(f"{a[g][1]/1000:>10.0f}" for g in G)
    al = a["ALL"]
    print(f"{name:24}{cells}{al[1]/1000:>10.0f}{al[0]/al[1]*1000:>8.0f}{al[3]/max(al[2],1):>6.2f}{al[4]:>8}{al[5]:>9.2f}")
print("(group columns: microseconds, sum of per-document medians; peak/doc = max over docs >100KB of peak heap / doc bytes)")
