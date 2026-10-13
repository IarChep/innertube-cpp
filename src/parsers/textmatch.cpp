#include "parsers/textmatch.h"
#include <vector>
namespace yt::gj {
static bool dig(char c) { return c >= '0' && c <= '9'; }
static bool alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

int64_t digitsOf(std::string_view s) {
    int64_t v = 0;
    for (char c : s) if (dig(c)) v = v * 10 + (c - '0');
    return v;
}
bool isClockText(std::string_view s) {
    // Groups split on ':'. Two groups: \d?\d : \d\d. Three: \d+ : \d?\d : \d\d.
    std::vector<std::string_view> g;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == ':') { g.push_back(s.substr(start, i - start)); start = i + 1; }
    for (std::string_view x : g) {
        if (x.empty()) return false;
        for (char c : x) if (!dig(c)) return false;
    }
    if (g.size() == 2) return g[0].size() <= 2 && g[1].size() == 2;
    if (g.size() == 3) return g[1].size() <= 2 && g[2].size() == 2;
    return false;
}
bool hasYear(std::string_view s) {
    // \b\d{4}\b: exactly four digits bounded by non-word chars or the ends.
    auto word = [](char c) { return dig(c) || alpha(c) || c == '_'; };
    for (size_t i = 0; i + 4 <= s.size(); ++i) {
        if (!(dig(s[i]) && dig(s[i + 1]) && dig(s[i + 2]) && dig(s[i + 3]))) continue;
        const bool lb = (i == 0) || !word(s[i - 1]);
        const bool rb = (i + 4 == s.size()) || !word(s[i + 4]);
        if (lb && rb) return true;
    }
    return false;
}
bool hasDigit(std::string_view s) { for (char c : s) if (dig(c)) return true; return false; }
bool hasAsciiLetter(std::string_view s) { for (char c : s) if (alpha(c)) return true; return false; }
bool containsCI(std::string_view hay, std::string_view needle) {
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t k = 0;
        while (k < needle.size() && lower(hay[i + k]) == lower(needle[k])) ++k;
        if (k == needle.size()) return true;
    }
    return needle.empty();
}
}
