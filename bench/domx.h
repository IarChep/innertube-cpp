// The oracle's extraction written once over a DOM traits type D:
//   D::V                          value handle (falsy/null = absent)
//   V get(V, sv)                  object member, null when not an object / missing
//   bool isObj(V), isStr(V)       type tests
//   sv str(V)                     string value, "" otherwise
//   bool i64(V, int64_t &)        integer value
//   void items(V, F)              array elements in order (nothing when not an array)
//   void members(V, F(sv, V))     object members in document order
//   V last(V)                     last array element, null when none
#pragma once
#include "harness.h"
#include <string_view>

namespace domx {
using sv = std::string_view;

template <class D, class V> V at(V v) { return v; }
template <class D, class V, class... K> V at(V v, sv k, K... ks)
{
    if (!v) return v;
    return at<D>(D::get(v, k), ks...);
}
template <class D, class V> std::string text(V t)
{
    if (!t || !D::isObj(t)) return {};
    V s = D::get(t, "simpleText");
    if (s && D::isStr(s)) return std::string(D::str(s));
    std::string out;
    D::items(D::get(t, "runs"), [&](V r) { out += D::str(D::get(r, "text")); });
    return out;
}
template <class D, class V> void fint(Out &o, V v)
{
    int64_t n = 0;
    const bool ok = v && D::i64(v, n);
    o.field(n, ok);
}

template <class D, class V> void lockup(Out &o, V l)
{
    V md = at<D>(l, "metadata", "lockupMetadataViewModel");
    V tvm = at<D>(l, "contentImage", "thumbnailViewModel");
    std::string badges, parts;
    bool fb = true, fp = true;
    D::items(D::get(tvm, "overlays"), [&](V ov) {
        D::items(at<D>(ov, "thumbnailBottomOverlayViewModel", "badges"), [&](V b) {
            if (!fb) badges += '\x1f';
            fb = false;
            badges += D::str(at<D>(b, "thumbnailBadgeViewModel", "text"));
        });
    });
    D::items(at<D>(md, "metadata", "contentMetadataViewModel", "metadataRows"), [&](V row) {
        D::items(D::get(row, "metadataParts"), [&](V p) {
            if (!fp) parts += '\x1f';
            fp = false;
            parts += D::str(at<D>(p, "text", "content"));
        });
    });
    o.field("L");
    o.field(D::str(D::get(l, "contentId")));
    o.field(D::str(D::get(l, "contentType")));
    o.field(D::str(at<D>(md, "title", "content")));
    o.field(D::str(D::get(D::last(at<D>(tvm, "image", "sources")), "url")));
    o.field(badges);
    o.field(parts);
    o.field(D::str(at<D>(md, "image", "decoratedAvatarViewModel", "rendererContext", "commandContext",
                         "onTap", "innertubeCommand", "browseEndpoint", "browseId")));
    o.end();
}
template <class D, class V> void video(Out &o, V v)
{
    o.field("V");
    o.field(D::str(D::get(v, "videoId")));
    o.field(text<D>(D::get(v, "title")));
    o.field(text<D>(D::get(v, "lengthText")));
    o.field(text<D>(D::get(v, "viewCountText")));
    o.field(text<D>(D::get(v, "ownerText")));
    o.field(D::str(D::get(D::last(at<D>(v, "thumbnail", "thumbnails")), "url")));
    o.end();
}
template <class D, class V> void tile(Out &o, V t)
{
    o.field("T");
    o.field(D::str(D::get(t, "contentId")));
    o.field(D::str(D::get(t, "contentType")));
    o.field(text<D>(at<D>(t, "metadata", "tileMetadataRenderer", "title")));
    o.end();
}
template <class D, class V> void comment(Out &o, V c)
{
    V p = D::get(c, "properties"), a = D::get(c, "author"), t = D::get(c, "toolbar");
    o.field("C");
    o.field(D::str(D::get(p, "commentId")));
    o.field(D::str(at<D>(p, "content", "content")));
    o.field(D::str(D::get(p, "publishedTime")));
    o.field(D::str(D::get(a, "displayName")));
    o.field(D::str(D::get(a, "channelId")));
    o.field(D::str(D::get(t, "likeCountNotliked")));
    o.field(D::str(D::get(t, "replyCount")));
    o.end();
}

inline bool isVideoKind(sv k)
{
    return k == "videoRenderer" || k == "compactVideoRenderer" || k == "gridVideoRenderer"
        || k == "playlistVideoRenderer";
}
template <class D, class V> void walk(Out &o, V v, std::string &tok, bool &haveTok)
{
    if (D::isObj(v)) {
        D::members(v, [&](sv k, V x) {
            if (D::isObj(x)) {
                if (k == "lockupViewModel") return lockup<D>(o, x);
                if (k == "tileRenderer") return tile<D>(o, x);
                if (k == "commentEntityPayload") return comment<D>(o, x);
                if (isVideoKind(k)) return video<D>(o, x);
                if (k == "continuationCommand") {
                    if (!haveTok) {
                        sv t = D::str(D::get(x, "token"));
                        if (!t.empty()) { tok = t; haveTok = true; }
                    }
                    return;
                }
            }
            walk<D>(o, x, tok, haveTok);
        });
    } else {
        D::items(v, [&](V x) { walk<D>(o, x, tok, haveTok); });
    }
}

template <class D, class V> void player(Out &o, V d)
{
    V ps = D::get(d, "playabilityStatus"), vd = D::get(d, "videoDetails"), sd = D::get(d, "streamingData");
    o.field("S"); o.field(D::str(D::get(ps, "status"))); o.field(D::str(D::get(ps, "reason"))); o.end();
    o.field("D");
    for (sv k : {"videoId", "title", "lengthSeconds", "viewCount", "author", "channelId", "shortDescription"})
        o.field(D::str(D::get(vd, k)));
    o.end();
    o.field("X"); o.field(D::str(D::get(sd, "expiresInSeconds"))); o.field(D::str(D::get(sd, "hlsManifestUrl"))); o.end();
    auto fmt = [&](V f) {
        o.field("F");
        fint<D>(o, D::get(f, "itag"));
        o.field(D::str(D::get(f, "url")));
        o.field(D::str(D::get(f, "mimeType")));
        fint<D>(o, D::get(f, "bitrate"));
        fint<D>(o, D::get(f, "width"));
        fint<D>(o, D::get(f, "height"));
        o.field(D::str(D::get(f, "contentLength")));
        o.field(D::str(D::get(f, "qualityLabel")));
        o.field(D::str(D::get(f, "audioQuality")));
        o.field(D::str(D::get(f, "signatureCipher")));
        o.end();
    };
    D::items(D::get(sd, "formats"), fmt);
    D::items(D::get(sd, "adaptiveFormats"), fmt);
    D::items(at<D>(d, "captions", "playerCaptionsTracklistRenderer", "captionTracks"), [&](V c) {
        o.field("A");
        o.field(D::str(D::get(c, "baseUrl")));
        o.field(D::str(D::get(c, "languageCode")));
        o.field(text<D>(D::get(c, "name")));
        o.field(D::str(D::get(c, "kind")));
        o.end();
    });
    o.field("B"); o.field(D::str(at<D>(d, "storyboards", "playerStoryboardSpecRenderer", "spec"))); o.end();
}

template <class D, class V> void json3(Out &o, V d)
{
    D::items(D::get(d, "events"), [&](V e) {
        o.field("J");
        fint<D>(o, D::get(e, "tStartMs"));
        fint<D>(o, D::get(e, "dDurationMs"));
        std::string s;
        D::items(D::get(e, "segs"), [&](V g) { s += D::str(D::get(g, "utf8")); });
        o.field(s);
        o.end();
    });
}

template <class D, class V> std::string run(V root, Kind k)
{
    Out o;
    o.field("E");
    o.field(D::str(at<D>(root, "error", "message")));
    o.field(D::str(at<D>(root, "responseContext", "visitorData")));
    o.end();
    if (k == Kind::Player) player<D>(o, root);
    else if (k == Kind::Json3) json3<D>(o, root);
    else {
        std::string tok;
        bool have = false;
        walk<D>(o, root, tok, have);
        o.field("K"); o.field(tok); o.end();
    }
    return std::move(o.s);
}
} // namespace domx
