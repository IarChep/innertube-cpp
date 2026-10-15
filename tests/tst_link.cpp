// yt::Link — every link shape a user pastes, parsed without the network. Built at C++17.
#include <doctest/doctest.h>
#include <yt/link.h>

using yt::Link;
using yt::LinkKind;

namespace {
bool is(const char *text, LinkKind kind, const char *id, long long start = 0)
{
    const std::optional<Link> l = Link::parse(text);
    return l && l->kind == kind && l->id == id && l->startSecs == start;
}
}

TEST_CASE("videoLinks") {
    CHECK(is("dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://www.youtube.com/watch?v=dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://youtube.com/watch?feature=share&v=dQw4w9WgXcQ&t=1m30s", LinkKind::Video, "dQw4w9WgXcQ", 90));
    CHECK(is("https://youtu.be/dQw4w9WgXcQ?si=AbCdEf&t=42", LinkKind::Video, "dQw4w9WgXcQ", 42));
    CHECK(is("HTTPS://WWW.YOUTUBE.COM/watch?v=dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("  https://m.youtube.com/watch?v=dQw4w9WgXcQ\n", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=RDAMVM", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("youtube.com/watch?v=dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ?start=10", LinkKind::Video, "dQw4w9WgXcQ", 10));
    CHECK(is("https://www.youtube.com/v/dQw4w9WgXcQ", LinkKind::Video, "dQw4w9WgXcQ"));
    CHECK(is("https://www.youtube.com/shorts/aqz-KE-bpKQ", LinkKind::Short, "aqz-KE-bpKQ"));
    CHECK(is("https://www.youtube.com/live/jfKfPfyJRdk?si=x", LinkKind::Live, "jfKfPfyJRdk"));
    CHECK(is("https://youtu.be/dQw4w9WgXcQ?t=1h2m3s", LinkKind::Video, "dQw4w9WgXcQ", 3723));
}
TEST_CASE("playlistAndChannelLinks") {
    CHECK(is("https://www.youtube.com/playlist?list=PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", LinkKind::Playlist,
             "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(is("https://www.youtube.com/watch?list=PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", LinkKind::Playlist,
             "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(is("PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", LinkKind::Playlist, "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(is("WL", LinkKind::Playlist, "WL"));
    CHECK(is("LL", LinkKind::Playlist, "LL"));
    CHECK(is("https://www.youtube.com/channel/UCuAXFkgsw1L7xaCfnd5JJOw", LinkKind::Channel, "UCuAXFkgsw1L7xaCfnd5JJOw"));
    CHECK(is("UCuAXFkgsw1L7xaCfnd5JJOw", LinkKind::Channel, "UCuAXFkgsw1L7xaCfnd5JJOw"));
    CHECK(is("https://www.youtube.com/@NASA/videos", LinkKind::Handle, "NASA"));
    CHECK(is("@NASA", LinkKind::Handle, "NASA"));
    CHECK(is("https://www.youtube.com/c/LinusTechTips", LinkKind::Custom, "LinusTechTips"));
    CHECK(is("https://www.youtube.com/user/RickAstleyVEVO", LinkKind::User, "RickAstleyVEVO"));
}
TEST_CASE("notLinks") {
    CHECK_FALSE(Link::parse("").has_value());
    CHECK_FALSE(Link::parse("   ").has_value());
    CHECK_FALSE(Link::parse("dQw4w9WgXc").has_value());                         // 10 characters
    CHECK_FALSE(Link::parse("dQw4w9WgX!Q").has_value());                        // foreign character
    CHECK_FALSE(Link::parse("https://example.com/watch?v=dQw4w9WgXcQ").has_value());
    CHECK_FALSE(Link::parse("ftp://www.youtube.com/watch?v=dQw4w9WgXcQ").has_value());
    CHECK_FALSE(Link::parse("https://youtu.be/").has_value());
    CHECK_FALSE(Link::parse("https://www.youtube.com/watch?v=short").has_value());
    CHECK_FALSE(Link::parse("https://www.youtube.com/feed/history").has_value());
    CHECK_FALSE(Link::parse("@").has_value());
    CHECK_FALSE(Link::parse("rick astley").has_value());
}
TEST_CASE("linkUrl") {
    CHECK(Link::parse("dQw4w9WgXcQ")->url() == "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    CHECK(Link::parse("@NASA")->url() == "https://www.youtube.com/@NASA");
    CHECK(Link::parse("https://www.youtube.com/c/LinusTechTips")->url() == "https://www.youtube.com/c/LinusTechTips");
    CHECK(Link::parse("https://www.youtube.com/user/RickAstleyVEVO")->url() == "https://www.youtube.com/user/RickAstleyVEVO");
    CHECK(Link::parse("WL")->url() == "https://www.youtube.com/playlist?list=WL");
}
