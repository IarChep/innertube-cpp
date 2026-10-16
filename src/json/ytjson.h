#ifndef YT_YTJSON_H
#define YT_YTJSON_H
// The YouTube layer over yt::json: the pieces every InnerTube answer is built from (text
// runs, thumbnail and view-model image sets, counts that are numbers or strings) and the
// base64 the parsers and the chains share. Pure std types; the shapes themselves are
// written out as paths where they are read (parsers/).
//
#include "json/json.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace yt {
namespace gj {

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
