#include <doctest/doctest.h>
#include "net/url.h"
#include "core/pct.h"
using yt::net::resolveLocation; using yt::net::schemeOf;
TEST_CASE("schemeOf") {
    CHECK(schemeOf("HTTPS://x/") == "https"); CHECK(schemeOf("http://x") == "http");
    CHECK(schemeOf("file:///etc/passwd") == ""); CHECK(schemeOf("/relative") == "");
    CHECK(schemeOf("javascript:alert(1)") == ""); CHECK(schemeOf("") == "");
}
TEST_CASE("resolveLocation") {
    const std::string b = "https://r.googlevideo.com/videoplayback?sig=a%2Cb";
    CHECK(resolveLocation(b, "https://x.googlevideo.com/vp?id=1%3D") == "https://x.googlevideo.com/vp?id=1%3D");
    CHECK(resolveLocation(b, "//cdn.example/a") == "https://cdn.example/a");
    CHECK(resolveLocation(b, "/other?q=1") == "https://r.googlevideo.com/other?q=1");
    CHECK(resolveLocation(b, "?n=2") == "https://r.googlevideo.com/videoplayback?n=2");
    CHECK(resolveLocation("https://h/a/b/c", "d") == "https://h/a/b/d");
    CHECK(resolveLocation("https://h:8443/a", "/z") == "https://h:8443/z");
    CHECK(resolveLocation("https://h", "z") == "https://h/z");
    CHECK(resolveLocation("https://h/a#frag", "/z#f2") == "https://h/z#f2");
}
TEST_CASE("pctEncodeMatchesQt") {
    using yt::core::pctEncode;
    CHECK(pctEncode("abc-._~XYZ09") == "abc-._~XYZ09");
    CHECK(pctEncode("a b&c=d/e?") == "a%20b%26c%3Dd%2Fe%3F");
    CHECK(pctEncode("кот") == "%D0%BA%D0%BE%D1%82");
    CHECK(pctEncode("\xF0\x9F\x98\x80") == "%F0%9F%98%80");
    CHECK(yt::core::formEncode({{"grant_type", "refresh_token"}, {"x y", "1+1"}}) == "grant_type=refresh_token&x%20y=1%2B1");
    CHECK(yt::core::formEncode({}) == "");
}
TEST_CASE("resolveLocation removes dot segments") {
    CHECK(resolveLocation("https://h/a/b/c", "../d") == "https://h/a/d");
    CHECK(resolveLocation("https://h/a/b/c", "./e") == "https://h/a/b/e");
    CHECK(resolveLocation("https://h/a/b/c", "../../../x") == "https://h/x");
    CHECK(resolveLocation("https://h/a/b/c", "..") == "https://h/a/");
}
