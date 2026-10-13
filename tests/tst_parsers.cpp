// The Glaze-backed InnerTube parsers on std types (port of MeeTube's
// tst_meetube_parsers: same fixtures, same expected values; CT:: field renames per
// include/yt/types.h).
#include <doctest/doctest.h>
#include "parsers/continuation.h"
#include "parsers/ytjson.h"
#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"
#include "parsers/suggestparser.h"
#include "parsers/textmatch.h"
#include "testutil.h"
#include "parserpayloads.h"

#include <string>

using namespace yt;

TEST_CASE("textmatch") {
    using namespace yt::gj;
    CHECK(digitsOf("1,234,567 views") == 1234567);
    CHECK(digitsOf("no views") == 0);
    CHECK(isClockText("3:07"));  CHECK(isClockText("12:03"));  CHECK(isClockText("1:02:03"));
    CHECK_FALSE(isClockText("LIVE")); CHECK_FALSE(isClockText("123:4")); CHECK_FALSE(isClockText(""));
    CHECK(isClockText("1:2:03"));   // the QRegExp allows a 1-digit minute group
    CHECK_FALSE(isClockText("100:03"));
    CHECK(hasYear("Streamed Mar 3, 2021")); CHECK_FALSE(hasYear("12345 views")); CHECK_FALSE(hasYear("3 years ago"));
    CHECK(hasYear("2021")); CHECK_FALSE(hasYear("x2021"));
}

TEST_CASE("suggestionsParsed") {
    const std::string body = "[\"cat\",[\"cat videos\",\"cats\",\"cat memes\"]]";
    std::vector<std::string> s = parseSuggestions(body);
    REQUIRE(s.size() == 3);
    CHECK(s[0] == "cat videos");
    CHECK(s[2] == "cat memes");
}
TEST_CASE("suggestionsGarbageIsEmpty") {
    CHECK(parseSuggestions(std::string("not json")).empty());
    CHECK(parseSuggestions(std::string("[\"only-query\"]")).empty());
}
TEST_CASE("flexIntParses") {
    gj::FlexInt s = std::string("1234");
    CHECK(gj::toInt64(s) == 1234);
    gj::FlexInt i = (std::int64_t)42;
    CHECK(gj::toInt64(i) == 42);
    gj::FlexInt bad = std::string("abc");
    CHECK(gj::toInt64(bad) == 0);
    gj::FlexInt partial = std::string("12a");
    CHECK(gj::toInt64(partial) == 0);
    std::optional<gj::FlexInt> missing;
    CHECK(gj::toInt64(missing) == 0);
    gj::FlexInt f = 3.9;
    CHECK(gj::toInt64(f) == 3);
}
TEST_CASE("parseTextBehavior") {
    CHECK(parseText(payloads::kSimpleText) == "hi");
    CHECK(parseText(payloads::kRunsText) == "ab");
    CHECK(parseText(payloads::kBareString).empty());
    CHECK(parseText("5").empty());
    CHECK(parseText("{}").empty());
}
TEST_CASE("continuationFound") {
    CHECK(findContinuationToken(std::string(payloads::kContinuationModern)) == "CTOKEN123");
}
TEST_CASE("continuationLegacy") {
    CHECK(findContinuationToken(std::string(payloads::kContinuationLegacy)) == "LEG456");
}
TEST_CASE("videoRendererAvatar") {
    Video v = parseVideoRenderer(std::string(payloads::kVideoRendererAvatar));
    CHECK(v.avatarUrl == "https://big/a.jpg");
}
TEST_CASE("playlistRendererParses") {
    Playlist p = parsePlaylistRenderer(std::string(payloads::kPlaylistRenderer));
    CHECK(p.id == "PL123");
    CHECK(p.title == "My List");
    CHECK(p.videoCount == 42);
    CHECK(p.channelName == "Chan");
}
TEST_CASE("playlistVideosCollected") {
    std::string next;
    std::vector<Video> v = parseVideoList(std::string(payloads::kPlaylistVideos), &next);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "v1");
}
TEST_CASE("tilePlaylistParses") {
    std::string token;
    std::vector<Playlist> ps = parsePlaylistList(std::string(payloads::kTilePlaylistLibrary), &token);
    REQUIRE(ps.size() == 1);
    CHECK(ps[0].id == "PLtest123");
    CHECK(ps[0].title == "My Mix");
    CHECK(ps[0].videoCount == 1709);
}
TEST_CASE("channelHeaderParses") {
    Channel u = parseChannel(std::string(payloads::kC4Header));
    CHECK(u.id == "UCxyz");
    CHECK(u.name == "Cool Channel");
    CHECK(u.subscriberCount == "1.2M subscribers");
    CHECK(u.thumbnailUrl == "https://a/l.jpg");
    Channel u2 = parseChannel(std::string(payloads::kC4HeaderBanner));
    CHECK(u2.bannerUrl == "http://b/c4big.jpg");
}
TEST_CASE("channel_extracts_subscribed") {
    Channel u = parseChannel(std::string_view(payloads::kChannelSubscribed));
    CHECK(u.subscribed);
}
TEST_CASE("channelSubscribedFlagParses") {
    CHECK(parseChannelSubscribed(std::string_view(payloads::kChannelSubscribed)));
    CHECK_FALSE(parseChannelSubscribed(std::string_view(payloads::kC4Header)));
}
TEST_CASE("channelPageHeaderParses") {
    const std::string resp = loadFixtureRaw("channel_pageheader.json");
    REQUIRE(!resp.empty());
    Channel u = parseChannel(resp);
    CHECK(u.name == "Google for Developers");
    CHECK(u.subscriberCount == "2.66M subscribers");
    CHECK(u.thumbnailUrl == "http://a/s160.jpg");
    CHECK(u.id == "UCabc");
    CHECK(u.handle == "@GoogleDevelopers");
    CHECK(u.videoCount == "6K videos");
    CHECK(u.bannerUrl == "http://b/banner1600.jpg");
}
TEST_CASE("watchPageParses") {
    const std::string next = loadFixtureRaw("watch_next.json");
    Video primary; std::vector<Video> related;
    parseWatchPage(next, &primary, &related);
    CHECK(primary.title == "The Title");
    CHECK(primary.description == "Hello description");
    CHECK(primary.channelName == "Creator");
    CHECK(primary.avatarUrl == "https://a/av.jpg");
    CHECK(primary.channelId == "UCowner");
    CHECK(primary.likeText == "9.9K");
    CHECK(primary.viewText == "1,234 views");
    REQUIRE(related.size() >= 1);
    CHECK(related.front().id == "rel1");
}
TEST_CASE("watch_extracts_subscribe_state") {
    Video primary; std::vector<Video> related;
    parseWatchPage(std::string_view(payloads::kNextSubscribedTv), &primary, &related);
    CHECK(primary.subscribed);
    CHECK(primary.channelId == "UCowner");
}
TEST_CASE("watch_tv_like_count_and_state") {
    Video primary; std::vector<Video> related;
    parseWatchPage(std::string_view(payloads::kNextTvLikeState), &primary, &related);
    CHECK(primary.likeText == "8.8K");
    CHECK(primary.likeStatus == 1);
}
TEST_CASE("watch_extracts_like_state") {
    Video primary; std::vector<Video> related;
    parseWatchPage(std::string_view(payloads::kNextLikedWeb), &primary, &related);
    CHECK(primary.likeStatus == 1);
    REQUIRE(primary.likeCount.has_value());
    CHECK(*primary.likeCount == 1234);
}
TEST_CASE("recursionDepthGuarded") {
    std::string deep;
    for (int i = 0; i < 500; ++i) deep += "{\"child\":";
    deep += "{\"continuationCommand\":{\"token\":\"DEEP\"}}";
    for (int i = 0; i < 500; ++i) deep += "}";
    (void)findContinuationToken(deep);
    std::string next;
    std::vector<Video> v = parseVideoList(deep, &next);
    CHECK(v.empty());
}
TEST_CASE("continuationAbsent") {
    CHECK(findContinuationToken(std::string(payloads::kContinuationAbsent)).empty());
}
TEST_CASE("videoList") {
    const std::string resp = loadFixtureRaw("search_videos.json");
    std::string token;
    std::vector<Video> v = parseVideoList(resp, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "aaa11111111");
    CHECK(v[0].title == "First Video");
    CHECK(v[0].channelName == "Chan A");
    CHECK(v[0].duration == "3:21");
    CHECK(v[0].viewCount == 1234);
    CHECK(v[1].id == "bbb22222222");
    CHECK(v[1].title == "Second Video");
    CHECK(token == "NEXTPAGE");
    CHECK(v[0].thumbnailUrl == "https://i.ytimg.com/vi/aaa11111111/hqdefault.jpg");
}
TEST_CASE("videoListBrowseFeed") {
    const std::string resp = loadFixtureRaw("browse_feed.json");
    REQUIRE(!resp.empty());
    std::string token;
    std::vector<Video> v = parseVideoList(resp, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "ccc33333333");
    CHECK(v[0].title == "Feed One");
    CHECK(v[1].id == "ddd44444444");
    CHECK(token == "FEEDNEXT");
}
TEST_CASE("lockupRelatedParses") {
    const std::string resp = loadFixtureRaw("next_lockup.json");
    REQUIRE(!resp.empty());
    std::string token;
    std::vector<Video> v = parseVideoList(resp, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "Zi_XLOBDo_Y");
    CHECK(v[0].title == "Michael Jackson - Billie Jean (Official Video)");
    CHECK(!v[0].thumbnailUrl.empty());
    CHECK(!v[0].avatarUrl.empty());
    CHECK(v[0].channelId.rfind("UC", 0) == 0);
    CHECK(v[0].channelName == "Michael Jackson");
    CHECK(v[0].duration == "4:56");
    CHECK(v[0].viewText.find("views") != std::string::npos);
    CHECK(v[0].date.find("ago") != std::string::npos);
}
TEST_CASE("streams") {
    const std::string p = loadFixtureRaw("player_ios.json");
    std::vector<Stream> s = parseStreams(p);
    REQUIRE(s.size() == 6);
    CHECK(s[0].id == "hls");
    CHECK(s[0].url.find("index.m3u8") != std::string::npos);
    CHECK(s[0].hasAudio);
    Stream m18, m137, a140, a251;
    for (const Stream &x : s) {
        if (x.id == "18")  m18  = x;
        if (x.id == "137") m137 = x;
        if (x.id == "140") a140 = x;
        if (x.id == "251") a251 = x;
    }
    CHECK(m18.height == 360);
    CHECK((m18.width > 0 && m18.hasAudio));
    CHECK(m137.height == 1080);
    CHECK((m137.width > 0 && !m137.hasAudio));
    CHECK(m137.url == "https://gv.example/vid137");
    CHECK((a140.width == 0 && a140.hasAudio));
    CHECK(a140.url == "https://gv.example/aud140");
    CHECK((a251.width == 0 && a251.hasAudio));
}
TEST_CASE("videoDetails") {
    const std::string p = loadFixtureRaw("player_ios.json");
    Video v = parseVideoDetails(p);
    CHECK(v.id == "aaa11111111");
    CHECK(v.title == "First Video");
    CHECK(v.channelName == "Chan A");
    CHECK(v.channelId == "UCxxxx");
    CHECK(v.viewCount == 1234);
    CHECK(v.description == "hello");
    CHECK(!v.thumbnailUrl.empty());
}
TEST_CASE("captions") {
    const std::string p = loadFixtureRaw("player_ios.json");
    std::vector<Subtitle> c = parseCaptions(p);
    REQUIRE(c.size() == 2);
    CHECK(c[0].language == "en");
    CHECK(c[0].url.find("timedtext") != std::string::npos);
}
TEST_CASE("comments") {
    const std::string p = loadFixtureRaw("comments_page.json");
    std::string token;
    std::vector<Comment> c = parseComments(p, &token);
    REQUIRE(c.size() == 2);
    CHECK(c[0].author == "Alice");
    CHECK(c[0].body == "Nice video");
    CHECK(token == "MORE_COMMENTS");
}
TEST_CASE("parsesPlaylistLockups") {
    const std::string j = loadFixtureRaw("browse_playlists_lockup.json");
    REQUIRE(!j.empty());
    std::string token;
    const std::vector<Playlist> p = parsePlaylistList(j, &token);
    REQUIRE(p.size() == 1);
    CHECK(p[0].id == "PLOU2XLYxmsIJ78oALT6XEKLHNFEggOI75");
    CHECK(p[0].title == "GDG Inspiring Stories");
    CHECK(p[0].videoCount == 5);
    CHECK(p[0].thumbnailUrl == "https://i.ytimg.com/vi/YFJZk4H_Bk4/hqdefault.jpg");
}
TEST_CASE("parsesAccountsList") {
    const std::string j = loadFixtureRaw("accounts_list.json");
    REQUIRE(!j.empty());
    const Account a = parseAccountsList(j);
    CHECK(a.name == "Ivan Petrov");
    CHECK(a.handle == "@ivanpetrov");
    CHECK(a.thumbnailUrl == "https://yt3.example/big.jpg");
    CHECK(a.channelId == "UCabc123def");
    CHECK(parseAccountsList("{}").name.empty());
    CHECK(parseAccountsList("not json at all").name.empty());
}
TEST_CASE("parsesTileRenderers") {
    const std::string j = loadFixtureRaw("tiles_history.json");
    REQUIRE(!j.empty());
    std::string token;
    const std::vector<Video> v = parseVideoList(j, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "u7OQ7kKBEHs");
    CHECK(v[0].title == "What if You Encounter an ENEMY");
    CHECK(v[0].channelName == "kesMadgik");
    CHECK(v[0].viewText == "350K views");
    CHECK(v[0].duration == "0:59");
    CHECK(v[0].largeThumbnailUrl == "https://i.ytimg.com/vi/u7OQ7kKBEHs/sddefault.jpg");
    CHECK(!v[0].thumbnailUrl.empty());
    CHECK(v[1].id == "dQw4w9WgXcQ");
    CHECK(v[1].title == "Second Video");
    CHECK(v[1].channelName == "Channel B");
}
TEST_CASE("isPlayableStatus") {
    CHECK(isPlayable(std::string(payloads::kPlayableOk), nullptr));
    std::string reason;
    CHECK_FALSE(isPlayable(std::string(payloads::kPlayableLogin), &reason));
    CHECK(reason.find("LOGIN_REQUIRED") != std::string::npos);
}
