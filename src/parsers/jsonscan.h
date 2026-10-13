#ifndef YT_JSONSCAN_H
#define YT_JSONSCAN_H
// Qt-free single-pass structural JSON scanner — the engine under the recursive
// renderer collectors.
//
// WHY NOT A DOM / LAZY VIEWS: the InnerTube envelope is schemaless, so the
// collectors must visit the whole tree. A DOM (nlohmann, glz::generic) pays a
// malloc per node — the dominant cost on the N9. Lazy views avoid the DOM but
// re-scan every subtree once per ancestor level (sibling-skip + descend), an
// O(depth) multiplier that measured ~3-4x SLOWER than the old DOM on ARM. This
// scanner visits every byte exactly once: containers are descended, scalars are
// token-skipped, and when the visitor recognizes a renderer key it takes the
// value's extent (skipped once) and hands it to a typed glz::read.
//
// ORDER NOTE: children are visited in DOCUMENT order. nlohmann's std::map gave
// the old walkers LEXICOGRAPHIC sibling order; the two differ only when one
// object owns several render-bearing / token-bearing children — which real
// InnerTube payloads don't do (verified against the full fixture golden dump).
//
// It is NOT a validator: on malformed input it stops scanning and returns
// (production payloads are validated at the transport; test garbage degrades
// to "nothing collected", same as nlohmann's discarded-DOM behavior).
#include <cstring>
#include <string_view>

namespace yt {
namespace gj {

// Recursion bound shared by the collectors: real InnerTube responses nest a few
// dozen levels; a pathological payload must not blow the stack on a 1 GB-RAM N9.
// Subtrees below the cap are skipped, not visited — the old walkers' "return".
inline constexpr int kScanMaxDepth = 100;

namespace scan {

inline const char *skipWs(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p;
}

// p at the opening quote; returns just past the closing quote.
inline const char *skipString(const char *p, const char *e)
{
    ++p;
    while (p < e) {
        const char *q = (const char *)std::memchr(p, '"', (size_t)(e - p));
        if (!q) return e;
        // A quote preceded by an even number of backslashes closes the string.
        int bs = 0;
        for (const char *b = q - 1; b >= p && *b == '\\'; --b) ++bs;
        if ((bs & 1) == 0) return q + 1;
        p = q + 1;
    }
    return e;
}

// Skips one whole value (container, string or scalar); returns just past it.
// Iterative bracket counting — no C-stack recursion regardless of nesting.
inline const char *skipValue(const char *p, const char *e)
{
    p = skipWs(p, e);
    if (p >= e) return e;
    if (*p == '"') return skipString(p, e);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < e) {
            const char c = *p;
            if (c == '"') { p = skipString(p, e); continue; }
            if (c == '{' || c == '[') { ++depth; ++p; continue; }
            if (c == '}' || c == ']') {
                ++p;
                if (--depth == 0) return p;
                continue;
            }
            ++p;
        }
        return e;
    }
    // scalar (number / true / false / null)
    while (p < e && *p != ',' && *p != '}' && *p != ']'
           && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') ++p;
    return p;
}

// The visitor contract (duck-typed). For every object member, in document
// order, the visitor answers from the key alone:
//   Action what(std::string_view key, int depth)  -> Descend | Skip | Capture
// Capture additionally delivers the value's extent:
//   void capture(std::string_view key, std::string_view value, int depth)
// Object boundaries (for per-node visitor state, e.g. the collectors' leaf flag):
//   void enter(int depth) / void leave(int depth)   — objects only, not arrays
// Extents are computed only for Skip/Capture answers, so every byte is scanned
// exactly once — either by the descent or by the one skip.
enum class Action { Descend, Skip, Capture };

// Scans one value; visitor sees every object member. Returns just past it.
template <class V>
inline const char *value(const char *p, const char *e, int depth, V &v)
{
    p = skipWs(p, e);
    if (p >= e) return e;
    if (*p == '{') {
        v.enter(depth);
        ++p;
        while (true) {
            p = skipWs(p, e);
            if (p >= e) break;
            if (*p == '}') { ++p; break; }
            if (*p != '"') { p = skipValue(p, e); break; }   // malformed — bail
            const char *ks = p + 1;
            p = skipString(p, e);
            const std::string_view key(ks, (size_t)(p - ks - 1));
            p = skipWs(p, e);
            if (p >= e || *p != ':') { p = e; break; }   // malformed
            ++p;
            p = skipWs(p, e);
            const Action a = (depth > kScanMaxDepth) ? Action::Skip : v.what(key, depth);
            if (a == Action::Descend) {
                p = value(p, e, depth + 1, v);
            } else {
                const char *vs = p;
                p = skipValue(p, e);
                if (a == Action::Capture)
                    v.capture(key, std::string_view(vs, (size_t)(p - vs)), depth);
            }
            p = skipWs(p, e);
            if (p < e && *p == ',') { ++p; continue; }
            if (p < e && *p == '}') { ++p; break; }
            break;   // malformed — stop
        }
        v.leave(depth);
        return p;
    }
    if (*p == '[') {
        ++p;
        while (true) {
            p = skipWs(p, e);
            if (p >= e) return e;
            if (*p == ']') return p + 1;
            p = (depth > kScanMaxDepth) ? skipValue(p, e) : value(p, e, depth + 1, v);
            p = skipWs(p, e);
            if (p < e && *p == ',') { ++p; continue; }
            if (p < e && *p == ']') return p + 1;
            return p;   // malformed — stop
        }
    }
    return skipValue(p, e);
}

template <class V>
inline void document(std::string_view json, V &v)
{
    value(json.data(), json.data() + json.size(), 0, v);
}

// First top-level member named `key` of an object document → its value extent.
// Empty view when absent (or when the document is not an object).
inline std::string_view topLevelValue(std::string_view json, std::string_view key)
{
    struct Finder {
        std::string_view want, got;
        Action what(std::string_view k, int depth)
        {
            if (depth != 0) return Action::Skip;
            return (k == want && got.empty()) ? Action::Capture : Action::Skip;
        }
        void capture(std::string_view, std::string_view v, int) { got = v; }
        void enter(int) {}
        void leave(int) {}
    } f{key, {}};
    document(json, f);
    return f.got;
}

} // namespace scan
} // namespace gj
} // namespace yt
#endif
