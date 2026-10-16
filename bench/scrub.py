# Scrubs a freshly captured corpus in place before it is committed: the capturing client's
# IP (YouTube puts it in every stream, thumbnail and tracking URL as ip=… and /ip/…/) and
# the visitorData it was given. Usage: python3 -I scrub.py <corpus dir>
import os, re, sys
d = sys.argv[1]
files = [os.path.join(d, f) for f in sorted(os.listdir(d))]
text = {f: open(f, encoding="utf-8", errors="surrogatepass").read() for f in files}
ips, visitors = set(), set()
for t in text.values():
    ips.update(re.findall(r"[?&/]ip[=/]([0-9a-fA-F:.]{3,45})", t))
    visitors.update(re.findall(r'"visitorData":"([^"]+)"', t))
for f, t in text.items():
    for ip in sorted(ips, key=len, reverse=True):
        t = t.replace(ip, "2001:db8::1" if ":" in ip else "192.0.2.1")   # RFC 3849 / 5737
    for v in visitors:
        t = t.replace(v, "Cg" + "x" * (len(v) - 2))
    with open(f, "w", encoding="utf-8", errors="surrogatepass", newline="") as out:
        out.write(t)
left = [f for f, t in ((f, open(f).read()) for f in files) for ip in ips if ip in t]
print(f"scrubbed {len(ips)} address(es), {len(visitors)} visitorData value(s) in {len(files)} files")
sys.exit(1 if left else 0)
