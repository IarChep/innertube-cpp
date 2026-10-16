// Continuation tokens (continuation.h). A wrapper whose token is missing or empty does
// not stop the search: the next one in document order is tried.
#include "continuation.h"
namespace yt {

bool isTokenKey(std::string_view k)
{
    return k == "continuationCommand" || k == "nextContinuationData" || k == "reloadContinuationData";
}

std::string tokenOf(std::string_view key, json::Value w)
{
    if (key != "continuationCommand") return w["continuation"].string();
    if (const std::string_view t = w["token"].str(); !t.empty()) return std::string(t);
    // continuationItemViewModel (2026 playlist pages): one level deeper.
    return w.path("innertubeCommand", "continuationCommand", "token").string();
}

std::string findContinuationToken(json::Value v)
{
    std::string found;
    v.find({"continuationCommand", "nextContinuationData", "reloadContinuationData"},
           [&](std::string_view key, json::Value w) {
        found = tokenOf(key, w);
        return found.empty() ? json::Visit::Skip : json::Visit::Stop;
    });
    return found;
}

std::string findContinuationTokenUnder(json::Value v, std::string_view key)
{
    return findContinuationToken(v[key]);
}

}
