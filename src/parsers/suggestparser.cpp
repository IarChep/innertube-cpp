#include "suggestparser.h"
#include "json/ytjson.h"
#include <vector>
#include <string>

namespace yt {
std::vector<std::string> parseSuggestions(std::string_view json) {
    std::vector<std::string> out;
    // Read the outer array generically (2 elements normally; tolerate more), then
    // read element [1] as an array of strings.
    std::vector<glz::raw_json> top;
    gj::readJson(top, json);
    if (top.size() < 2) return out;
    std::vector<std::string> sugg;
    gj::readJson(sugg, top[1].str);
    for (size_t i = 0; i < sugg.size(); ++i)
        out.push_back(std::move(sugg[i]));
    return out;
}
}
