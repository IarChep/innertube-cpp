// Search suggestions (suggestqueries, client=firefox): ["query", ["s1", "s2", …]].
#include "suggestparser.h"

namespace yt {

std::vector<std::string> parseSuggestions(json::Value response)
{
    std::vector<std::string> out;
    for (json::Value s : response[1].items())
        if (s.isString()) out.push_back(s.string());
    return out;
}

}
