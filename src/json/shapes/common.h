// Shared JSON shapes: the small pieces every InnerTube answer is built from (text runs,
// thumbnail arrays, view-model "sources", {"content"} wrappers, continuation token
// wrappers) and the top-level error envelope, with their accessors. Namespace gj.
//
// Every shape in json/shapes/ is a namespace-scope aggregate of std::optional fields
// named exactly like the JSON keys: Glaze reflects it with no glz::meta, and unknown
// keys are skipped (gj::kIn), so a shape is a deliberate partial view.
#ifndef YT_SHAPES_COMMON_H
#define YT_SHAPES_COMMON_H
#include "json/ytjson.h"
#include <optional>
#include <string>
#include <vector>
namespace yt::gj {

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

// ---- one-field content wrappers reused across renderers ---------------------
// {"content": "..."} — lockup titles, metadata parts, attributed descriptions.
struct ContentText {
    std::optional<std::string> content;
};
inline std::string contentOf(const std::optional<ContentText> &c)
{
    return (c && c->content) ? *c->content : std::string();
}

// ---- continuation tokens ----------------------------------------------------
// The two shapes a continuation token hides under (Glaze-reflected against the
// wrapper object value captured by the scanner — same as continuation.cpp's
// TokenShapes).
struct InnerContinuation { std::optional<std::string> token; };
struct InnerCommand { std::optional<InnerContinuation> continuationCommand; };
struct TokenShapes {
    std::optional<std::string> token;          // continuationCommand
    std::optional<std::string> continuation;   // next/reloadContinuationData
    // continuationItemViewModel (2026 playlist pages): the token one level deeper, in
    // continuationCommand.innertubeCommand.continuationCommand.
    std::optional<InnerCommand> innertubeCommand;
};

// ---- the top-level InnerTube error envelope: {"error": {"code":.., "message": ".."}}
struct ErrorEnvelope {
    std::optional<std::string> message;
};

}
#endif
