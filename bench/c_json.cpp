// The oracle workload on the library's yt::json (src/json), written the way a parser is.
#include "harness.h"
#include "json/json.h"
using yt::json::Value;
using yt::json::Visit;

static std::string text(Value t)
{
    if (Value s = t["simpleText"]; s.isString()) return s.string();
    std::string out;
    for (Value r : t["runs"].items()) out += r["text"].str();
    return out;
}
static void join(std::string &dst, bool &first, std::string_view s) { if (!first) dst += '\x1f'; first = false; dst += s; }

static void lockup(Out &o, Value l)
{
    Value md = l.path("metadata", "lockupMetadataViewModel");
    Value tvm = l.path("contentImage", "thumbnailViewModel");
    std::string badges, parts;
    bool fb = true, fp = true;
    for (Value ov : tvm["overlays"].items())
        for (Value b : ov.path("thumbnailBottomOverlayViewModel", "badges").items())
            join(badges, fb, b.path("thumbnailBadgeViewModel", "text").str());
    for (Value row : md.path("metadata", "contentMetadataViewModel", "metadataRows").items())
        for (Value p : row["metadataParts"].items())
            join(parts, fp, p.path("text", "content").str());
    o.field("L");
    o.field(l["contentId"].str());
    o.field(l["contentType"].str());
    o.field(md.path("title", "content").str());
    o.field(tvm.path("image", "sources").last()["url"].str());
    o.field(badges);
    o.field(parts);
    o.field(md.path("image", "decoratedAvatarViewModel", "rendererContext", "commandContext", "onTap",
                    "innertubeCommand", "browseEndpoint", "browseId").str());
    o.end();
}
static void video(Out &o, Value v)
{
    o.field("V"); o.field(v["videoId"].str()); o.field(text(v["title"])); o.field(text(v["lengthText"]));
    o.field(text(v["viewCountText"])); o.field(text(v["ownerText"]));
    o.field(v.path("thumbnail", "thumbnails").last()["url"].str()); o.end();
}
static void comment(Out &o, Value c)
{
    Value p = c["properties"], a = c["author"], t = c["toolbar"];
    o.field("C"); o.field(p["commentId"].str()); o.field(p.path("content", "content").str()); o.field(p["publishedTime"].str());
    o.field(a["displayName"].str()); o.field(a["channelId"].str()); o.field(t["likeCountNotliked"].str()); o.field(t["replyCount"].str());
    o.end();
}
static void num(Out &o, Value v) { int64_t n; const bool ok = v.toInt(n); o.field(ok ? n : 0, ok); }

std::string extract(const std::string &body, Kind k)
{
    kName = NAME;
    yt::json::Doc doc(body);
    Out o;
    o.field("E"); o.field(doc["error"]["message"].str()); o.field(doc["responseContext"]["visitorData"].str()); o.end();
    if (k == Kind::Lists) {
        std::string tok;
        doc.root().find({"lockupViewModel", "videoRenderer", "compactVideoRenderer", "gridVideoRenderer",
                         "playlistVideoRenderer", "tileRenderer", "commentEntityPayload", "continuationCommand"},
                        [&](std::string_view key, Value v) {
            if (!v.isObject()) return Visit::Descend;    // not a renderer: look inside
            if (key == "lockupViewModel") lockup(o, v);
            else if (key == "tileRenderer") {
                o.field("T"); o.field(v["contentId"].str()); o.field(v["contentType"].str());
                o.field(text(v.path("metadata", "tileMetadataRenderer", "title"))); o.end();
            }
            else if (key == "commentEntityPayload") comment(o, v);
            else if (key == "continuationCommand") { if (tok.empty()) tok = v["token"].str(); }
            else video(o, v);
            return Visit::Skip;
        });
        o.field("K"); o.field(tok); o.end();
    } else if (k == Kind::Player) {
        Value ps = doc["playabilityStatus"], vd = doc["videoDetails"], sd = doc["streamingData"];
        o.field("S"); o.field(ps["status"].str()); o.field(ps["reason"].str()); o.end();
        o.field("D");
        for (const char *f : {"videoId", "title", "lengthSeconds", "viewCount", "author", "channelId", "shortDescription"})
            o.field(vd[f].str());
        o.end();
        o.field("X"); o.field(sd["expiresInSeconds"].str()); o.field(sd["hlsManifestUrl"].str()); o.end();
        for (const char *list : {"formats", "adaptiveFormats"})
            for (Value f : sd[list].items()) {
                o.field("F"); num(o, f["itag"]); o.field(f["url"].str()); o.field(f["mimeType"].str());
                num(o, f["bitrate"]); num(o, f["width"]); num(o, f["height"]);
                o.field(f["contentLength"].str()); o.field(f["qualityLabel"].str()); o.field(f["audioQuality"].str());
                o.field(f["signatureCipher"].str()); o.end();
            }
        for (Value c : doc.root().path("captions", "playerCaptionsTracklistRenderer", "captionTracks").items()) {
            o.field("A"); o.field(c["baseUrl"].str()); o.field(c["languageCode"].str()); o.field(text(c["name"])); o.field(c["kind"].str()); o.end();
        }
        o.field("B"); o.field(doc.root().path("storyboards", "playerStoryboardSpecRenderer", "spec").str()); o.end();
    } else {
        for (Value e : doc["events"].items()) {
            o.field("J"); num(o, e["tStartMs"]); num(o, e["dDurationMs"]);
            std::string s;
            for (Value g : e["segs"].items()) s += g["utf8"].str();
            o.field(s); o.end();
        }
    }
    return std::move(o.s);
}
