// Compiled at -std=c++17: the public headers must not need anything newer.
#include <doctest/doctest.h>
#include <yt/types.h>
#include <yt/result.h>
#include <yt/player.h>
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
TEST_CASE("Player defaults") {
    yt::Player p;
    CHECK(p.status.kind == yt::Playability::Unknown);
    CHECK(p.sources.empty());
    CHECK(yt::PlayerRequest{}.want == yt::WantAll);
    yt::Format f;
    CHECK_FALSE(f.initRange.has_value());
    CHECK_FALSE(f.audio.has_value());
}
TEST_CASE("storyboardFrame") {
    yt::StoryboardLevel l;
    l.urlTemplate = "https://i.ytimg.com/sb/v/storyboard3_L1/M$M.jpg?sqp=S&sigh=X";
    l.width = 80; l.height = 45; l.count = 108; l.cols = 10; l.rows = 10; l.intervalMs = 2000;
    yt::StoryboardFrame f = yt::storyboardFrame(l, 0);
    CHECK(f.url == "https://i.ytimg.com/sb/v/storyboard3_L1/M0.jpg?sqp=S&sigh=X");
    CHECK(f.x == 0);
    CHECK(f.y == 0);
    CHECK(f.width == 80);
    CHECK(f.height == 45);
    f = yt::storyboardFrame(l, 23 * 2000 + 1999);       // cell 23 on sheet 0
    CHECK(f.x == 240);
    CHECK(f.y == 90);
    f = yt::storyboardFrame(l, 100 * 2000);             // first cell of sheet 1
    CHECK(f.url == "https://i.ytimg.com/sb/v/storyboard3_L1/M1.jpg?sqp=S&sigh=X");
    CHECK(f.x == 0);
    CHECK(f.y == 0);
    f = yt::storyboardFrame(l, 10000000);               // past the end: the last frame
    CHECK(f.url.find("/M1.jpg") != std::string::npos);
    CHECK(f.x == 560);
    CHECK(f.y == 0);
    f = yt::storyboardFrame(l, -5);
    CHECK(f.x == 0);
    CHECK(f.url.find("/M0.jpg") != std::string::npos);
    CHECK(yt::storyboardFrame(yt::StoryboardLevel{}, 0).url.empty());   // an empty level
}
