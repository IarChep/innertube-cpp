#ifndef YT_SUGGESTPARSER_H
#define YT_SUGGESTPARSER_H
#include <yt/json.h>
#include <string>
#include <vector>
namespace yt {
// YouTube suggest endpoint (client=firefox) returns ["query",["s1","s2",…]].
// Returns the suggestion strings; malformed/empty input → empty list.
std::vector<std::string> parseSuggestions(json::Value response);
}
#endif
