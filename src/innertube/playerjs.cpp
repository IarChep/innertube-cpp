// Byte scans over the /tv page and base.js (see playerjs.h): no JS engine, no copies.
#include "innertube/playerjs.h"
namespace yt {

namespace {
bool idChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}
// The id after `marker` up to the next '/' (or the "\/" of escaped JSON).
std::string idAfter(std::string_view html, std::string_view marker)
{
    const size_t at = html.find(marker);
    if (at == std::string_view::npos) return std::string();
    size_t b = at + marker.size(), e = b;
    while (e < html.size() && idChar(html[e]) && e - b < 32) ++e;
    if (e == b || e >= html.size() || (html[e] != '/' && html[e] != '\\')) return std::string();
    return std::string(html.substr(b, e - b));
}
}

std::string playerIdIn(std::string_view html)
{
    std::string id = idAfter(html, "/s/player/");
    if (id.empty()) id = idAfter(html, "\\/s\\/player\\/");
    return id;
}

int signatureTimestampIn(std::string_view js)
{
    static const std::string_view kName = "signatureTimestamp";
    for (size_t at = js.find(kName); at != std::string_view::npos; at = js.find(kName, at + 1)) {
        size_t i = at + kName.size();
        while (i < js.size() && (js[i] == ':' || js[i] == '=' || js[i] == ' ' || js[i] == '"' || js[i] == '\'')) ++i;
        int value = 0, digits = 0;
        while (i < js.size() && js[i] >= '0' && js[i] <= '9' && digits < 9) {
            value = value * 10 + (js[i] - '0');
            ++i;
            ++digits;
        }
        if (digits > 0) return value;
    }
    return 0;
}

std::string playerScriptUrl(std::string_view playerId)
{
    return "https://www.youtube.com/s/player/" + std::string(playerId) + "/player_ias.vflset/en_US/base.js";
}

}
