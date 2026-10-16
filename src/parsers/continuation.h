#ifndef YT_CONTINUATION_H
#define YT_CONTINUATION_H
// Continuation tokens: the cursor of every InnerTube list, hidden under three wrapper
// keys whose shapes differ (parsers/continuation.cpp).
#include "json/json.h"
#include <string>
#include <string_view>
namespace yt {
// The token a token-bearing wrapper carries, "" when it carries none: `key` is the member
// name the wrapper sits under (one of isTokenKey's).
std::string tokenOf(std::string_view key, json::Value wrapper);
// The three member names whose value is a token-bearing wrapper.
bool isTokenKey(std::string_view key);
// The first non-empty continuation token below v, in document order — the modern
// continuationCommand.token plus the legacy next/reload shapes.
std::string findContinuationToken(json::Value v);
// Same, scoped to the value of v's member `key` ("" when absent) — e.g. the
// comments-section token lives under "engagementPanels" only.
std::string findContinuationTokenUnder(json::Value v, std::string_view key);
}
#endif
