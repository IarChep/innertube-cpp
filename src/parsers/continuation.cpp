#include "continuation.h"
#include "tokenscan.h"
#include "jsonscan.h"
namespace yt {

using namespace gj;

// DFS-first-match over the whole tree, single pass. A wrapper whose token
// string is missing/empty does not stop the search (the old fall-through).
// Delegates to captureToken (from tokenscan.h) which encodes the same logic.
struct TokenFinder {
    std::string found;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view key, int)
    {
        if (!found.empty()) return scan::Action::Skip;
        if (isTokenKey(key)) return scan::Action::Capture;
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        captureToken(key, value, &found);
    }
};

std::string findContinuationToken(std::string_view json)
{
    TokenFinder f;
    scan::document(json, f);
    return std::string(f.found);
}

std::string findContinuationTokenUnder(std::string_view json, const char *rootKey)
{
    const std::string_view sub = scan::topLevelValue(json, rootKey);
    if (sub.empty()) return std::string();
    return findContinuationToken(sub);
}
}
