# Reference extraction (the oracle): every C++ candidate must print byte-identical dumps.
# Usage: python3 -I oracle.py <file>...  -> writes <file>.expect next to the bench outputs dir.
import json, sys, os

def esc(s):
    return s.replace('\\', '\\\\').replace('\n', '\\n').replace('|', '\\p')

def S(v):                      # JSON string -> its value, anything else -> ""
    return v if isinstance(v, str) else ""

def I(v):                      # JSON integer -> decimal, anything else -> ""
    return str(v) if isinstance(v, int) and not isinstance(v, bool) else ""

def at(v, *path):
    for k in path:
        if isinstance(k, int):
            if not isinstance(v, list) or not v: return None
            v = v[k]
        else:
            if not isinstance(v, dict) or k not in v: return None
            v = v[k]
    return v

def arr(v):
    return v if isinstance(v, list) else []

def text(t):
    if not isinstance(t, dict): return ""
    if isinstance(t.get("simpleText"), str): return t["simpleText"]
    return "".join(S(r.get("text")) for r in arr(t.get("runs")) if isinstance(r, dict))

def line(out, *fields):
    out.append("|".join(esc(f) for f in fields))

VIDEO_KINDS = ("videoRenderer", "compactVideoRenderer", "gridVideoRenderer", "playlistVideoRenderer")

def lockup(o, out):
    md = at(o, "metadata", "lockupMetadataViewModel")
    tvm = at(o, "contentImage", "thumbnailViewModel")
    badges = []
    for ov in arr(at(tvm, "overlays")):
        for b in arr(at(ov, "thumbnailBottomOverlayViewModel", "badges")):
            badges.append(S(at(b, "thumbnailBadgeViewModel", "text")))
    parts = []
    for row in arr(at(md, "metadata", "contentMetadataViewModel", "metadataRows")):
        for p in arr(at(row, "metadataParts")):
            parts.append(S(at(p, "text", "content")))
    line(out, "L", S(o.get("contentId")), S(o.get("contentType")), S(at(md, "title", "content")),
         S(at(tvm, "image", "sources", -1, "url")), "\x1f".join(badges), "\x1f".join(parts),
         S(at(md, "image", "decoratedAvatarViewModel", "rendererContext", "commandContext", "onTap",
              "innertubeCommand", "browseEndpoint", "browseId")))

def video(o, out):
    line(out, "V", S(o.get("videoId")), text(o.get("title")), text(o.get("lengthText")),
         text(o.get("viewCountText")), text(o.get("ownerText")),
         S(at(o, "thumbnail", "thumbnails", -1, "url")))

def tile(o, out):
    line(out, "T", S(o.get("contentId")), S(o.get("contentType")),
         text(at(o, "metadata", "tileMetadataRenderer", "title")))

def comment(o, out):
    p, a, t = o.get("properties"), o.get("author"), o.get("toolbar")
    line(out, "C", S(at(p, "commentId")), S(at(p, "content", "content")), S(at(p, "publishedTime")),
         S(at(a, "displayName")), S(at(a, "channelId")), S(at(t, "likeCountNotliked")),
         S(at(t, "replyCount")))

LEAVES = {"lockupViewModel": lockup, "tileRenderer": tile, "commentEntityPayload": comment}
for k in VIDEO_KINDS: LEAVES[k] = video

def walk(v, out, tok):
    if isinstance(v, dict):
        for k, x in v.items():
            f = LEAVES.get(k)
            if f and isinstance(x, dict): f(x, out); continue
            if k == "continuationCommand" and isinstance(x, dict):
                if not tok and S(x.get("token")): tok.append(S(x.get("token")))
                continue
            walk(x, out, tok)
    elif isinstance(v, list):
        for x in v: walk(x, out, tok)

def lists(d, out):
    tok = []
    walk(d, out, tok)
    line(out, "K", tok[0] if tok else "")

def player(d, out):
    ps, vd, sd = d.get("playabilityStatus"), d.get("videoDetails"), d.get("streamingData")
    line(out, "S", S(at(ps, "status")), S(at(ps, "reason")))
    line(out, "D", *(S(at(vd, k)) for k in ("videoId", "title", "lengthSeconds", "viewCount", "author",
                                             "channelId", "shortDescription")))
    line(out, "X", S(at(sd, "expiresInSeconds")), S(at(sd, "hlsManifestUrl")))
    for f in arr(at(sd, "formats")) + arr(at(sd, "adaptiveFormats")):
        line(out, "F", I(at(f, "itag")), S(at(f, "url")), S(at(f, "mimeType")), I(at(f, "bitrate")),
             I(at(f, "width")), I(at(f, "height")), S(at(f, "contentLength")), S(at(f, "qualityLabel")),
             S(at(f, "audioQuality")), S(at(f, "signatureCipher")))
    for c in arr(at(d, "captions", "playerCaptionsTracklistRenderer", "captionTracks")):
        line(out, "A", S(at(c, "baseUrl")), S(at(c, "languageCode")), text(at(c, "name")), S(at(c, "kind")))
    line(out, "B", S(at(d, "storyboards", "playerStoryboardSpecRenderer", "spec")))

def json3(d, out):
    for e in arr(d.get("events")):
        line(out, "J", I(at(e, "tStartMs")), I(at(e, "dDurationMs")),
             "".join(S(at(s, "utf8")) for s in arr(at(e, "segs"))))

def kind(name):
    if name.startswith("player_"): return player
    if name.endswith(".json3"): return json3
    return lists

def dump(path):
    d = json.load(open(path, encoding="utf-8"))
    out = []
    line(out, "E", S(at(d, "error", "message")), S(at(d, "responseContext", "visitorData")))
    kind(os.path.basename(path))(d, out)
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    outdir = os.environ.get("OUT", ".")
    for p in sys.argv[1:]:
        with open(os.path.join(outdir, os.path.basename(p) + ".expect"), "w", encoding="utf-8",
                  errors="surrogatepass", newline="\n") as f:
            f.write(dump(p))
