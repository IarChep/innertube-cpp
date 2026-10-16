#ifndef YT_YTJSON_H
#define YT_YTJSON_H
// Qt-free Glaze layer: the read options, flexible numbers, partial reads and base64 —
// the machinery every parser shares. The JSON shapes themselves live in
// json/shapes/ (one header per API family). Pure std types only.
//
#include "json/json.h"
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

// ---- reading InnerTube shapes over yt::json ---------------------------------
// {"simpleText": "…"} or {"runs": [{"text": "…"}, …]}: simpleText wins, else the runs
// joined, else "" (also for anything that is not such an object).
inline std::string textOf(json::Value t)
{
    if (const json::Value s = t["simpleText"]; s.isString()) return s.string();
    std::string out;
    for (json::Value r : t["runs"].items()) out += r["text"].str();
    return out;
}

// YouTube channel avatars come back protocol-relative ("//yt3.ggpht.com/..."); Qt's image
// loader rejects the missing scheme ("Unsupported protocol"). Force it to https.
inline std::string withScheme(std::string u)
{
    if (u.size() >= 2 && u[0] == '/' && u[1] == '/')
        u.insert(0, "https:");
    return u;
}

// {"thumbnails": [{"url": …}, …]} is ordered small→large: the last url, "" when absent.
inline std::string lastThumbUrl(json::Value set) { return withScheme(set["thumbnails"].last()["url"].string()); }
// View-model images, {"sources": [{"url": …}, …]}, also small→large.
inline std::string lastSourceUrl(json::Value set) { return withScheme(set["sources"].last()["url"].string()); }
inline std::string firstSourceUrl(json::Value set) { return withScheme(set["sources"][0]["url"].string()); }

// InnerTube counts arrive as JSON numbers (itag, width) or quoted strings
// (videoDetails.viewCount): an integer, a float (truncated), or a strictly numeric string;
// anything else — including a partial numeric prefix like "12a" — is 0.
inline std::int64_t flexInt(json::Value v)
{
    std::int64_t n = 0;
    if (v.toInt(n)) return n;
    double d = 0;
    if (v.toDouble(d)) return (d > -9.2e18 && d < 9.2e18) ? (std::int64_t) d : 0;
    const std::string_view s = v.str();
    const std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), n, 10);
    return (!s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size()) ? n : 0;
}

// The first member named key anywhere below v, in document order; absent when none.
inline json::Value first(json::Value v, std::string_view key)
{
    json::Value got;
    v.find({key}, [&](std::string_view, json::Value x) {
        got = x;
        return json::Visit::Stop;
    });
    return got;
}

} // namespace gj
} // namespace yt
#endif
