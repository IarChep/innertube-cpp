#include "core/pct.h"
namespace yt::core {
std::string pctEncode(std::string_view s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                             || c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) { out += char(c); continue; }
        out += '%';
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}
std::string formEncode(const Form &fields) {
    std::string out;
    for (const auto &kv : fields) {
        if (!out.empty()) out += '&';
        out += pctEncode(kv.first);
        out += '=';
        out += pctEncode(kv.second);
    }
    return out;
}
}
