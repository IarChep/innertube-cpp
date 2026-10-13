// Compiled at -std=c++17: the public headers must not need anything newer.
#include <doctest/doctest.h>
#include <yt/types.h>
#include <yt/result.h>
static_assert(__cplusplus < 201703L + 3, "this suite must build as C++17");
TEST_CASE("Result ok/error") {
    CHECK(yt::Result<int>{}.ok());
    yt::Result<int> bad;
    bad.error = yt::Error{yt::ErrorCode::Api, "x"};
    CHECK_FALSE(bad.ok());
    CHECK(bad.error->message == "x");
}
TEST_CASE("Video defaults") {
    yt::Video v;
    CHECK_FALSE(v.likeCount.has_value());
    CHECK_FALSE(v.dislikeCount.has_value());
    CHECK(v.viewCount == 0);
    CHECK(yt::Votes{}.likes == std::nullopt);
}
