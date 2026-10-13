// Minimal, byte-exact URL helpers for the redirect policy. Nothing here decodes or
// re-encodes: server-issued URLs carry signatures whose %-escapes must survive.
#ifndef YT_NET_URL_H
#define YT_NET_URL_H
#include <string>
#include <string_view>
namespace yt::net {
// "http"/"https" (lower-cased) or "" when the URL has another/no scheme.
std::string schemeOf(std::string_view url);
// RFC 3986 §5.2 reference resolution (with dot-segment removal): absolute "https://h/p",
// network-path "//h/p", absolute-path "/p", query-only "?q", relative "p".
std::string resolveLocation(std::string_view base, std::string_view location);
}
#endif
