#ifndef YT_TOKENSCAN_H
#define YT_TOKENSCAN_H
// Qt-free helpers shared by continuation.cpp and rendererparser.cpp for
// recognising and extracting continuation tokens in a single structural scan.
//
#include "ytjson.h"
#include <string>
#include <string_view>

namespace yt {
namespace gj {

// The three object keys whose VALUE is a token-bearing wrapper object.
inline bool isTokenKey(std::string_view k)
{
    return k == "continuationCommand" || k == "nextContinuationData"
        || k == "reloadContinuationData";
}

// The two shapes the token string hides under (Glaze-reflected against the
// wrapper object value captured by the scanner — same as continuation.cpp's
// TokenShapes).
struct TokenShapes {
    std::optional<std::string> token;          // continuationCommand
    std::optional<std::string> continuation;   // next/reloadContinuationData
};

// Extract the token from one captured wrapper extent and write it into *out
// only when *out is still empty and the extraction yields a non-empty string —
// this is the "first non-empty wins" fall-through semantics of TokenFinder.
inline void captureToken(std::string_view key, std::string_view value, std::string *out)
{
    if (!out || !out->empty()) return;
    TokenShapes s{};
    readJson(s, value);
    if (key == "continuationCommand") {
        if (s.token && !s.token->empty()) *out = *s.token;
    } else {
        if (s.continuation && !s.continuation->empty()) *out = *s.continuation;
    }
}

} // namespace gj
} // namespace yt
#endif
