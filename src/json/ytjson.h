#ifndef YT_YTJSON_H
#define YT_YTJSON_H
// Qt-free Glaze layer: the read options + the small JSON shapes shared by every
// InnerTube parser (text runs, thumbnail arrays, view-model "sources", flexible
// numbers), base64. Pure std types only — this header must stay includable from code
// that will later run on a worker thread with no Qt in sight.
//
// Glaze reflection requires namespace-scope aggregates; every struct here and in
// the parser .cpps is reflected automatically (no glz::meta needed — field names
// match the JSON keys exactly).
//
#include <glaze/json.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace yt {
namespace gj {

// Every InnerTube payload is read with these options:
//  - error_on_unknown_keys=false: our structs are deliberate partial views of
//    huge responses — unknown keys are structurally skipped (the fast path).
//  - null_terminated=false: parsers take arbitrary string_views (including
//    subtree extents that end mid-buffer), so no sentinel byte is assumed.
inline constexpr glz::opts kIn{.null_terminated = false, .error_on_unknown_keys = false};

// ---- text: {"simpleText": "..."} or {"runs": [{"text": "..."}]} -------------
struct Run {
    std::optional<std::string> text;
};
struct Text {
    std::optional<std::string> simpleText;
    std::optional<std::vector<Run>> runs;
};
// parseText semantics: simpleText wins, else the runs concatenated, else "".
inline std::string textOf(const Text &t)
{
    if (t.simpleText) return *t.simpleText;
    std::string out;
    if (t.runs)
        for (const Run &r : *t.runs)
            if (r.text) out += *r.text;
    return out;
}
inline std::string textOf(const std::optional<Text> &t) { return t ? textOf(*t) : std::string(); }

// ---- classic thumbnails: {"thumbnails": [{"url": ...}, ...]} ----------------
struct Thumb {
    std::optional<std::string> url;
};
struct ThumbSet {
    std::optional<std::vector<Thumb>> thumbnails;
};
// YouTube channel avatars come back protocol-relative ("//yt3.ggpht.com/..."); Qt's image
// loader rejects the missing scheme ("Unsupported protocol"). Force it to https.
inline std::string withScheme(std::string u)
{
    if (u.size() >= 2 && u[0] == '/' && u[1] == '/')
        u.insert(0, "https:");
    return u;
}
// Largest (= last) thumbnail url, "" when absent — mirrors lastThumb()/lastOf().
inline std::string lastThumbUrl(const ThumbSet &s)
{
    if (s.thumbnails && !s.thumbnails->empty() && s.thumbnails->back().url)
        return withScheme(*s.thumbnails->back().url);
    return std::string();
}
inline std::string lastThumbUrl(const std::optional<ThumbSet> &s)
{
    return s ? lastThumbUrl(*s) : std::string();
}

// ---- view-model images: {"sources": [{"url": ...}, ...]} --------------------
struct Src {
    std::optional<std::string> url;
};
struct Sources {
    std::optional<std::vector<Src>> sources;
};
// "sources" is ordered small→large, so back() is the largest (lastSourceUrl).
inline std::string lastSourceUrl(const std::optional<Sources> &s)
{
    if (s && s->sources && !s->sources->empty() && s->sources->back().url)
        return withScheme(*s->sources->back().url);
    return std::string();
}
inline std::string firstSourceUrl(const std::optional<Sources> &s)
{
    if (s && s->sources && !s->sources->empty() && s->sources->front().url)
        return withScheme(*s->sources->front().url);
    return std::string();
}

// ---- flexible integers ------------------------------------------------------
// InnerTube counts arrive as JSON numbers (itag, width) or quoted strings
// (videoDetails.viewCount). Mirrors the old jint(): int, float (truncated),
// or a strictly-numeric string; anything else — including a partial numeric
// prefix like "12a" — is 0.
using FlexInt = std::variant<std::int64_t, double, std::string>;
inline std::int64_t toInt64(const FlexInt &v)
{
    if (const std::int64_t *i = std::get_if<std::int64_t>(&v)) return *i;
    if (const double *d = std::get_if<double>(&v)) return (std::int64_t)*d;
    if (const std::string *s = std::get_if<std::string>(&v)) {
        std::int64_t n = 0;
        const char *b = s->data(), *e = s->data() + s->size();
        auto res = std::from_chars(b, e, n, 10);
        if (res.ec == std::errc() && res.ptr == e && b != e) return n;
    }
    return 0;
}
inline std::int64_t toInt64(const std::optional<FlexInt> &v) { return v ? toInt64(*v) : 0; }

// Typed partial read of a subtree extent. Errors degrade to "fields absent":
// the struct keeps whatever was parsed before the error point and the caller
// carries on — a malformed renderer never aborts a listing (matches the old
// per-field contains() tolerance).
template <class T>
inline void readJson(T &out, std::string_view sv)
{
    (void)glz::read<kIn>(out, sv);
}

// Whole-document variant: Reply.body is a std::string, whose data() is
// guaranteed NUL-terminated — Glaze's sentinel scanning skips per-byte
// end-pointer checks. Subtree extents MUST keep kIn (they end mid-buffer).
inline constexpr glz::opts kInDoc{.null_terminated = true, .error_on_unknown_keys = false};
template <class T>
inline void readJsonDoc(T &out, const std::string &doc) { (void)glz::read<kInDoc>(out, doc); }

// ---- one-field content wrappers reused across renderers ---------------------
// {"content": "..."} — lockup titles, metadata parts, attributed descriptions.
struct ContentText {
    std::optional<std::string> content;
};
inline std::string contentOf(const std::optional<ContentText> &c)
{
    return (c && c->content) ? *c->content : std::string();
}

// ---- base64 (xtags, data: URLs) ---------------------------------------------
// YouTube fields come in both alphabets and usually without padding (xtags,
// videoPlaybackUstreamerConfig are base64url; a QR data: URL is classic). Here, in
// the bottom layer, so the parsers and the chains share one decoder.
inline bool base64Decode(std::string_view in, std::string *out)
{
    out->clear();
    size_t n = in.size();
    while (n > 0 && in[n - 1] == '=') --n;
    if (n % 4 == 1) return false;                    // a lone sextet carries no byte
    out->reserve(n * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const char c = in[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else { out->clear(); return false; }
        acc = (acc << 6) | (std::uint32_t) v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back((char) ((acc >> bits) & 0xff));
        }
    }
    return true;
}

} // namespace gj
} // namespace yt
#endif
