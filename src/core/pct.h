// RFC 3986 percent-encoding for user-supplied values (search queries, OAuth form
// fields). Same output as Qt's QUrl::toPercentEncoding defaults.
#ifndef YT_CORE_PCT_H
#define YT_CORE_PCT_H
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace yt::core {
using Form = std::vector<std::pair<std::string, std::string>>;
// Unreserved A-Z a-z 0-9 - . _ ~ pass; every other byte -> %XX (uppercase hex).
std::string pctEncode(std::string_view s);
// application/x-www-form-urlencoded body: k=v&k=v, both sides encoded, in order.
std::string formEncode(const Form &fields);
}
#endif
