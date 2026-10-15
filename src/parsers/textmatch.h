// Small text predicates the renderer parsers need (replacing the old QRegExps).
// Pure functions over UTF-8 bytes; ASCII-only semantics, like the patterns they replace.
#ifndef YT_TEXTMATCH_H
#define YT_TEXTMATCH_H
#include <cstdint>
#include <string_view>
namespace yt::gj {
int64_t digitsOf(std::string_view s);   // "1,234,567 views" -> 1234567; no digits -> 0
bool isClockText(std::string_view s);   // ^(\d+:)?\d?\d:\d\d$  ("3:07","12:03","1:02:03")
int64_t clockSecs(std::string_view s);  // "4:56" → 296; 0 when !isClockText
bool hasYear(std::string_view s);       // contains \b\d{4}\b   ("Mar 3, 2021" yes; "12345" no)
bool hasDigit(std::string_view s);      // [0-9] anywhere
bool hasAsciiLetter(std::string_view s);// [A-Za-z] anywhere
bool containsCI(std::string_view hay, std::string_view needle);   // ASCII case-insensitive find
}
#endif
