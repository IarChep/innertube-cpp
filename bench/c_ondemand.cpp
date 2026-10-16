// simdjson On-Demand: forward-only lazy iteration (Keiser & Lemire 2024). Kernel from $SJ_IMPL.
#include "harness.h"
#include "simdjson.h"
using namespace simdjson;
using sv = std::string_view;
using OV = ondemand::value;

static sv S(simdjson_result<OV> v) { sv s; if (v.error() || v.get_string().get(s)) return {}; return s; }
static sv S(OV v) { return S(simdjson_result<OV>(std::move(v))); }
static sv S(simdjson_result<ondemand::object> o, sv k) { if (o.error()) return {}; return S(o.find_field_unordered(k)); }
static void FI(Out &o, simdjson_result<OV> v)
{
    int64_t n = 0; bool ok = false;
    if (!v.error()) { ondemand::number_type t; if (!v.get_number_type().get(t) && t != ondemand::number_type::floating_point_number) { if (t == ondemand::number_type::signed_integer) ok = !v.get_int64().get(n); else { uint64_t u; ok = !v.get_uint64().get(u); n = (int64_t) u; } } }
    o.field(n, ok);
}
static std::string text(simdjson_result<OV> t);
static std::string text(OV t) { return text(simdjson_result<OV>(std::move(t))); }
static std::string text(simdjson_result<OV> t)
{
    ondemand::object o;
    if (t.error() || t.get_object().get(o)) return {};
    // simpleText wins over runs regardless of order: scan the members once
    std::string runs; bool simple = false; std::string st;
    for (auto f : o) {
        sv k; if (f.unescaped_key().get(k)) continue;
        if (k == "simpleText") { sv s; if (!f.value().get_string().get(s)) { simple = true; st = s; } }
        else if (k == "runs") { ondemand::array a; if (!f.value().get_array().get(a)) for (auto r : a) { ondemand::object ro; if (!r.get_object().get(ro)) runs += S(ro.find_field_unordered("text")); } }
    }
    return simple ? st : runs;
}
template <class F> static void items(simdjson_result<OV> v, F f);
template <class F> static void items(OV v, F f) { items(simdjson_result<OV>(std::move(v)), f); }
template <class F> static void items(simdjson_result<OV> v, F f) { ondemand::array a; if (v.error() || v.get_array().get(a)) return; for (auto x : a) { OV e; if (!x.get(e)) f(e); } }
static simdjson_result<OV> path(simdjson_result<OV> v, std::initializer_list<sv> ks);
static simdjson_result<OV> path(OV v, std::initializer_list<sv> ks) { return path(simdjson_result<OV>(std::move(v)), ks); }
static simdjson_result<OV> path(simdjson_result<OV> v, std::initializer_list<sv> ks)
{
    for (sv k : ks) { ondemand::object o; if (v.error() || v.get_object().get(o)) return INCORRECT_TYPE; v = o.find_field_unordered(k); }
    return v;
}

static void lockup(Out &o, OV l)
{
    ondemand::object lo; if (l.get_object().get(lo)) return;
    std::string id, type, title, thumb, badges, parts, chan; bool fb = true, fp = true;
    for (auto f : lo) {
        sv k; if (f.unescaped_key().get(k)) continue;
        OV v; if (f.value().get(v)) continue;
        if (k == "contentId") id = S(v);
        else if (k == "contentType") type = S(v);
        else if (k == "contentImage") {
            auto tvm = path(v, {"thumbnailViewModel"});
            ondemand::object to; if (tvm.error() || tvm.get_object().get(to)) continue;
            for (auto g : to) {
                sv gk; if (g.unescaped_key().get(gk)) continue;
                if (gk == "image") { std::string last; items(path(g.value(), {"sources"}), [&](OV s) { last = S(path(s, {"url"})); }); thumb = last; }
                else if (gk == "overlays") items(g.value(), [&](OV ov) { items(path(ov, {"thumbnailBottomOverlayViewModel", "badges"}), [&](OV b) { if (!fb) badges += '\x1f'; fb = false; badges += S(path(b, {"thumbnailBadgeViewModel", "text"})); }); });
            }
        } else if (k == "metadata") {
            auto md = path(v, {"lockupMetadataViewModel"});
            ondemand::object mo; if (md.error() || md.get_object().get(mo)) continue;
            for (auto g : mo) {
                sv gk; if (g.unescaped_key().get(gk)) continue;
                if (gk == "title") title = S(path(g.value(), {"content"}));
                else if (gk == "metadata") items(path(g.value(), {"contentMetadataViewModel", "metadataRows"}), [&](OV row) { items(path(row, {"metadataParts"}), [&](OV p) { if (!fp) parts += '\x1f'; fp = false; parts += S(path(p, {"text", "content"})); }); });
                else if (gk == "image") chan = S(path(g.value(), {"decoratedAvatarViewModel", "rendererContext", "commandContext", "onTap", "innertubeCommand", "browseEndpoint", "browseId"}));
            }
        }
    }
    o.field("L"); o.field(id); o.field(type); o.field(title); o.field(thumb); o.field(badges); o.field(parts); o.field(chan); o.end();
}
static void video(Out &o, OV v)
{
    ondemand::object vo; if (v.get_object().get(vo)) return;
    std::string id, title, len, views, owner, thumb;
    for (auto f : vo) {
        sv k; if (f.unescaped_key().get(k)) continue;
        if (k == "videoId") id = S(f.value());
        else if (k == "title") title = text(f.value());
        else if (k == "lengthText") len = text(f.value());
        else if (k == "viewCountText") views = text(f.value());
        else if (k == "ownerText") owner = text(f.value());
        else if (k == "thumbnail") { std::string last; items(path(f.value(), {"thumbnails"}), [&](OV s) { last = S(path(s, {"url"})); }); thumb = last; }
    }
    o.field("V"); o.field(id); o.field(title); o.field(len); o.field(views); o.field(owner); o.field(thumb); o.end();
}
static void tile(Out &o, OV v)
{
    ondemand::object to; if (v.get_object().get(to)) return;
    std::string id, type, title;
    for (auto f : to) {
        sv k; if (f.unescaped_key().get(k)) continue;
        if (k == "contentId") id = S(f.value()); else if (k == "contentType") type = S(f.value());
        else if (k == "metadata") title = text(path(f.value(), {"tileMetadataRenderer", "title"}));
    }
    o.field("T"); o.field(id); o.field(type); o.field(title); o.end();
}
static void comment(Out &o, OV v)
{
    ondemand::object co; if (v.get_object().get(co)) return;
    std::string f7[7];
    for (auto f : co) {
        sv k; if (f.unescaped_key().get(k)) continue;
        ondemand::object x; if (f.value().get_object().get(x)) continue;
        if (k == "properties") for (auto g : x) { sv gk; if (g.unescaped_key().get(gk)) continue; if (gk == "commentId") f7[0] = S(g.value()); else if (gk == "content") f7[1] = S(path(g.value(), {"content"})); else if (gk == "publishedTime") f7[2] = S(g.value()); }
        else if (k == "author") for (auto g : x) { sv gk; if (g.unescaped_key().get(gk)) continue; if (gk == "displayName") f7[3] = S(g.value()); else if (gk == "channelId") f7[4] = S(g.value()); }
        else if (k == "toolbar") for (auto g : x) { sv gk; if (g.unescaped_key().get(gk)) continue; if (gk == "likeCountNotliked") f7[5] = S(g.value()); else if (gk == "replyCount") f7[6] = S(g.value()); }
    }
    o.field("C"); for (auto &s : f7) o.field(s); o.end();
}
static bool isVideoKind(sv k) { return k == "videoRenderer" || k == "compactVideoRenderer" || k == "gridVideoRenderer" || k == "playlistVideoRenderer"; }
static void walk(Out &o, OV v, std::string &tok, bool &have)
{
    ondemand::json_type t; if (v.type().get(t)) return;
    if (t == ondemand::json_type::object) {
        ondemand::object ob; if (v.get_object().get(ob)) return;
        for (auto f : ob) {
            sv k; if (f.unescaped_key().get(k)) return;
            OV x; if (f.value().get(x)) return;
            ondemand::json_type xt; if (x.type().get(xt)) return;
            if (xt == ondemand::json_type::object) {
                if (k == "lockupViewModel") { lockup(o, x); continue; }
                if (k == "tileRenderer") { tile(o, x); continue; }
                if (k == "commentEntityPayload") { comment(o, x); continue; }
                if (isVideoKind(k)) { video(o, x); continue; }
                if (k == "continuationCommand") { if (!have) { ondemand::object co; if (!x.get_object().get(co)) for (auto g : co) { sv gk; if (!g.unescaped_key().get(gk) && gk == "token") { sv s = S(g.value()); if (!s.empty()) { tok = s; have = true; } } } } continue; }
            }
            walk(o, x, tok, have);
        }
    } else if (t == ondemand::json_type::array) {
        ondemand::array a; if (v.get_array().get(a)) return;
        for (auto x : a) { OV e; if (x.get(e)) return; walk(o, e, tok, have); }
    }
}

std::string extract(const std::string &doc, Kind k)
{
    static bool init = [] { if (const char *i = getenv("SJ_IMPL")) get_active_implementation() = get_available_implementations()[i]; return true; }();
    (void) init;
    static std::string name = std::string("simdjson_od_") + SIMDJSON_STRINGIFY(SIMDJSON_BUILTIN_IMPLEMENTATION);
    kName = name.c_str();
    ondemand::parser p;
    padded_string ps(doc);
    ondemand::document d = p.iterate(ps).value_unsafe();
    Out o;
    // The envelope: two top-level lookups on the root object, then rewind for the body.
    { ondemand::object r; std::string em, vd; if (!d.get_object().get(r)) { em = S(path(r.find_field_unordered("error"), {"message"})); vd = S(path(r.find_field_unordered("responseContext"), {"visitorData"})); }
      o.field("E"); o.field(em); o.field(vd); o.end(); }
    d.rewind();
    if (k == Kind::Lists) {
        std::string tok; bool have = false;
        OV root; if (!d.get_value().get(root)) walk(o, root, tok, have);
        o.field("K"); o.field(tok); o.end();
    } else if (k == Kind::Player) {
        ondemand::object r; if (d.get_object().get(r)) return o.s;
        std::string st, rs, vd[7], ex, hls, spec; std::vector<std::string> F, A;
        for (auto f : r) {
            sv key; if (f.unescaped_key().get(key)) continue;
            ondemand::object x; if (f.value().get_object().get(x)) continue;
            if (key == "playabilityStatus") for (auto g : x) { sv gk; if (g.unescaped_key().get(gk)) continue; if (gk == "status") st = S(g.value()); else if (gk == "reason") rs = S(g.value()); }
            else if (key == "videoDetails") { static const sv ks[7] = {"videoId", "title", "lengthSeconds", "viewCount", "author", "channelId", "shortDescription"}; for (auto g : x) { sv gk; if (g.unescaped_key().get(gk)) continue; for (int i = 0; i < 7; ++i) if (gk == ks[i]) vd[i] = S(g.value()); } }
            else if (key == "streamingData") {
                std::vector<std::string> fm, af;
                for (auto g : x) {
                    sv gk; if (g.unescaped_key().get(gk)) continue;
                    if (gk == "expiresInSeconds") ex = S(g.value()); else if (gk == "hlsManifestUrl") hls = S(g.value());
                    else if (gk == "formats" || gk == "adaptiveFormats") {
                        auto &dst = gk == "formats" ? fm : af;
                        items(g.value(), [&](OV fv) {
                            ondemand::object fo; if (fv.get_object().get(fo)) return;
                            Out t; std::string s[10]; int64_t n[4] = {}; bool ok[4] = {};
                            for (auto h : fo) {
                                sv hk; if (h.unescaped_key().get(hk)) continue;
                                static const sv ik[4] = {"itag", "bitrate", "width", "height"}; static const sv sk[6] = {"url", "mimeType", "contentLength", "qualityLabel", "audioQuality", "signatureCipher"};
                                for (int i = 0; i < 4; ++i) if (hk == ik[i]) { Out tmp; FI(tmp, h.value()); if (!tmp.s.empty()) { ok[i] = true; n[i] = std::stoll(tmp.s); } }
                                for (int i = 0; i < 6; ++i) if (hk == sk[i]) s[i] = S(h.value());
                            }
                            t.field("F"); t.field(n[0], ok[0]); t.field(s[0]); t.field(s[1]); t.field(n[1], ok[1]); t.field(n[2], ok[2]); t.field(n[3], ok[3]);
                            t.field(s[2]); t.field(s[3]); t.field(s[4]); t.field(s[5]); t.end();
                            dst.push_back(t.s);
                        });
                    }
                }
                F = fm; F.insert(F.end(), af.begin(), af.end());
            } else if (key == "captions") [&] {
                ondemand::object pc; auto v = x.find_field_unordered("playerCaptionsTracklistRenderer");
                if (v.error() || v.get_object().get(pc)) return;
                items(pc.find_field_unordered("captionTracks"), [&](OV c) {
                    ondemand::object co; if (c.get_object().get(co)) return;
                    std::string b, l, nm, kd;
                    for (auto h : co) { sv hk; if (h.unescaped_key().get(hk)) continue; if (hk == "baseUrl") b = S(h.value()); else if (hk == "languageCode") l = S(h.value()); else if (hk == "name") nm = text(h.value()); else if (hk == "kind") kd = S(h.value()); }
                    Out t; t.field("A"); t.field(b); t.field(l); t.field(nm); t.field(kd); t.end(); A.push_back(t.s);
                });
            }();
            else if (key == "storyboards") spec = S(path(x.find_field_unordered("playerStoryboardSpecRenderer"), {"spec"}));
        }
        o.field("S"); o.field(st); o.field(rs); o.end();
        o.field("D"); for (auto &s : vd) o.field(s); o.end();
        o.field("X"); o.field(ex); o.field(hls); o.end();
        for (auto &s : F) o.s += s;
        for (auto &s : A) o.s += s;
        o.field("B"); o.field(spec); o.end();
    } else {
        ondemand::object r; if (d.get_object().get(r)) return o.s;
        items(r.find_field_unordered("events"), [&](OV e) {
            ondemand::object eo; if (e.get_object().get(eo)) return;
            Out a, b; std::string segs;
            for (auto h : eo) { sv hk; if (h.unescaped_key().get(hk)) continue; if (hk == "tStartMs") FI(a, h.value()); else if (hk == "dDurationMs") FI(b, h.value()); else if (hk == "segs") items(h.value(), [&](OV g) { segs += S(path(g, {"utf8"})); }); }
            o.field("J"); o.field(a.s); o.field(b.s); o.field(segs); o.end();
        });
    }
    return std::move(o.s);
}
