// The Glaze-backed InnerTube parsers on std types (port of MeeTube's
// tst_meetube_parsers: same fixtures, same expected values; CT:: field renames per
// include/yt/types.h).
#include <doctest/doctest.h>
#include "innertube/playerjs.h"
#include "parsers/continuation.h"
#include "json/json3.h"
#include "json/shapes/common.h"
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
    CHECK(clockSecs("4:56") == 296); CHECK(clockSecs("1:02:03") == 3723); CHECK(clockSecs("LIVE") == 0);
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
    VideoItem v = parseVideoRenderer(std::string(payloads::kVideoRendererAvatar));
    CHECK(v.channel.avatarUrl == "https://big/a.jpg");
}
TEST_CASE("playlistRendererParses") {
    Playlist p = parsePlaylistRenderer(std::string(payloads::kPlaylistRenderer));
    CHECK(p.id == "PL123");
    CHECK(p.title == "My List");
    CHECK(p.videoCount == 42);
    CHECK(p.owner.name == "Chan");
}
TEST_CASE("playlistVideosCollected") {
    std::string next;
    std::vector<VideoItem> v = parseVideoList(std::string(payloads::kPlaylistVideos), &next);
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
    CHECK(u.subscriberText == "1.2M subscribers");
    CHECK(u.avatarUrl == "https://a/l.jpg");
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
    CHECK(u.subscriberText == "2.66M subscribers");
    CHECK(u.avatarUrl == "http://a/s160.jpg");
    CHECK(u.id == "UCabc");
    CHECK(u.handle == "@GoogleDevelopers");
    CHECK(u.videoCountText == "6K videos");
    CHECK(u.bannerUrl == "http://b/banner1600.jpg");
}
TEST_CASE("watchPageParses") {
    const std::string next = loadFixtureRaw("watch_next.json");
    Video primary;
    parseWatchPage(next, &primary);
    CHECK(primary.title == "The Title");
    CHECK(primary.description == "Hello description");
    CHECK(primary.channel.name == "Creator");
    CHECK(primary.channel.avatarUrl == "https://a/av.jpg");
    CHECK(primary.channel.id == "UCowner");
    CHECK(primary.likeText == "9.9K");
    CHECK(primary.viewText == "1,234 views");
    REQUIRE(primary.related.items.size() >= 1);
    CHECK(primary.related.items.front().id == "rel1");
}
TEST_CASE("watch_extracts_subscribe_state") {
    Video primary;
    parseWatchPage(std::string_view(payloads::kNextSubscribedTv), &primary);
    CHECK(primary.channel.subscribed);
    CHECK(primary.channel.id == "UCowner");
}
TEST_CASE("watch_tv_like_count_and_state") {
    Video primary;
    parseWatchPage(std::string_view(payloads::kNextTvLikeState), &primary);
    CHECK(primary.likeText == "8.8K");
    CHECK(primary.myRating == Rating::Like);
}
TEST_CASE("watch_extracts_like_state") {
    Video primary;
    parseWatchPage(std::string_view(payloads::kNextLikedWeb), &primary);
    CHECK(primary.myRating == Rating::Like);
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
    std::vector<VideoItem> v = parseVideoList(deep, &next);
    CHECK(v.empty());
}
TEST_CASE("continuationAbsent") {
    CHECK(findContinuationToken(std::string(payloads::kContinuationAbsent)).empty());
}
TEST_CASE("videoList") {
    const std::string resp = loadFixtureRaw("search_videos.json");
    std::string token;
    std::vector<VideoItem> v = parseVideoList(resp, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "aaa11111111");
    CHECK(v[0].title == "First Video");
    CHECK(v[0].channel.name == "Chan A");
    CHECK(v[0].durationText == "3:21");
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
    std::vector<VideoItem> v = parseVideoList(resp, &token);
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
    std::vector<VideoItem> v = parseVideoList(resp, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "Zi_XLOBDo_Y");
    CHECK(v[0].title == "Michael Jackson - Billie Jean (Official Video)");
    CHECK(!v[0].thumbnailUrl.empty());
    CHECK(!v[0].channel.avatarUrl.empty());
    CHECK(v[0].channel.id.rfind("UC", 0) == 0);
    CHECK(v[0].channel.name == "Michael Jackson");
    CHECK(v[0].durationText == "4:56");
    CHECK(v[0].durationSecs == 296);
    CHECK(v[0].viewText.find("views") != std::string::npos);
    CHECK(v[0].publishedText.find("ago") != std::string::npos);
}
// A channel's own Videos tab has a single metadata row ("N views", "date") and no channel
// name; row 0 used to be taken as the channel name, so channelName read "97K views".
TEST_CASE("a channel tab lockup has no channel row") {
    const std::string resp = loadFixtureRaw("channel_videos_lockup.json");
    std::string token;
    std::vector<VideoItem> v = parseVideoList(resp, &token);
    REQUIRE(v.size() == 1);
    CHECK(v[0].id == "5b3n-u_0QAI");
    CHECK(v[0].channel.name.empty());
    CHECK(v[0].viewText == "97K views");
    CHECK(v[0].publishedText == "1 day ago");
}
TEST_CASE("comments") {
    const std::string p = loadFixtureRaw("comments_page.json");
    std::string token;
    std::vector<CommentThread> c = parseComments(p, &token);
    REQUIRE(c.size() == 2);
    CHECK(c[0].comment.author.name == "Alice");
    CHECK(c[0].comment.text == "Nice video");
    CHECK(c[0].comment.author.avatarUrl == "https://yt3.ggpht.com/a.jpg");
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
    const AccountInfo a = parseAccountsList(j);
    CHECK(a.name == "Ivan Petrov");
    CHECK(a.handle == "@ivanpetrov");
    CHECK(a.avatarUrl == "https://yt3.example/big.jpg");
    CHECK(a.channelId == "UCabc123def");
    CHECK(parseAccountsList("{}").name.empty());
    CHECK(parseAccountsList("not json at all").name.empty());
}
TEST_CASE("parsesTileRenderers") {
    const std::string j = loadFixtureRaw("tiles_history.json");
    REQUIRE(!j.empty());
    std::string token;
    const std::vector<VideoItem> v = parseVideoList(j, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "u7OQ7kKBEHs");
    CHECK(v[0].title == "What if You Encounter an ENEMY");
    CHECK(v[0].channel.name == "kesMadgik");
    CHECK(v[0].viewText == "350K views");
    CHECK(v[0].durationText == "0:59");
    CHECK(v[0].thumbnailUrl == "https://i.ytimg.com/vi/u7OQ7kKBEHs/sddefault.jpg");
    CHECK(!v[0].thumbnailUrl.empty());
    CHECK(v[1].id == "dQw4w9WgXcQ");
    CHECK(v[1].title == "Second Video");
    CHECK(v[1].channel.name == "Channel B");
}
// Security review: AccountItemCollector re-scans every captured accountItem with a
// fresh depth counter, so a chain of nested "accountItem" keys defeated the scanner's
// depth cap and recursed once per level — a crafted accounts_list overflowed the stack.
TEST_CASE("accountsListNestedItemsDepthBounded") {
    const int n = 200000;
    std::string deep;
    deep.reserve(n * 16 + 8);
    for (int i = 0; i < n; ++i) deep += "{\"accountItem\":";
    deep += "{}";
    for (int i = 0; i < n; ++i) deep += "}";
    const AccountInfo a = parseAccountsList(deep);   // must return, not overflow the stack
    CHECK(a.name.empty());
}

TEST_CASE("base64Decode") {
    std::string o;
    CHECK(gj::base64Decode("aGVsbG8=", &o));
    CHECK(o == "hello");
    CHECK(gj::base64Decode("aGVsbG8", &o));          // padding is optional (xtags omit it)
    CHECK(o == "hello");
    CHECK(gj::base64Decode("-_8", &o));              // base64url alphabet
    CHECK(o == "\xfb\xff");
    CHECK(gj::base64Decode("+/8=", &o));             // classic alphabet, same bytes
    CHECK(o == "\xfb\xff");
    CHECK(gj::base64Decode("", &o));
    CHECK(o.empty());
    CHECK_FALSE(gj::base64Decode("aGV*bG8=", &o));   // foreign byte
    CHECK(o.empty());
    CHECK_FALSE(gj::base64Decode("aGVsb", &o));      // one stray sextet carries no byte
    CHECK(o.empty());
}

// ---- /player (parsePlayerResponse) ----
TEST_CASE("playerResponseVisionos") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_ok.json"));
    CHECK(p.status.kind == Playability::Ok);
    CHECK_FALSE(p.final);
    REQUIRE(p.formats.size() == 4);
    const Format &v = p.formats[0];
    CHECK(v.itag == 137);
    CHECK(v.url == "https://rr.example/videoplayback?itag=137&c=VISIONOS");
    CHECK(v.mimeType == "video/mp4");
    CHECK(v.codecs == "avc1.640028");
    CHECK(v.qualityLabel == "1080p");
    CHECK(v.width == 1920);
    CHECK(v.height == 1080);
    CHECK(v.fps == 25);
    CHECK(v.bitrate == 4334157);
    CHECK(v.averageBitrate == 3038377);
    CHECK(v.contentLength == 80911999);
    CHECK(v.durationMs == 213040);
    REQUIRE(v.initRange.has_value());
    CHECK(v.initRange->first == 0);
    CHECK(v.initRange->last == 741);
    REQUIRE(v.indexRange.has_value());
    CHECK(v.indexRange->first == 742);
    CHECK(v.lastModified == 1766957926174250LL);
    CHECK((v.hasVideo && !v.hasAudio));
    CHECK_FALSE(v.audio.has_value());
    CHECK_FALSE(v.sealed);
    const Format &a = p.formats[2];
    CHECK((a.hasAudio && !a.hasVideo));
    CHECK(a.codecs == "mp4a.40.2");
    CHECK(p.formats[3].codecs == "opus");
    CHECK(p.hlsUrl == "https://manifest.example/hls_variant/VISIONOS/index.m3u8");
    CHECK(p.sabrUrl == "https://rr.example/videoplayback?sabr=1&c=VISIONOS");
    CHECK(p.sabrConfig == "CgUSA2FiYw");
    CHECK(p.expiresInSecs == 21540);
    REQUIRE(p.captions.size() == 2);
    CHECK(p.captions[0].language == "en");
    CHECK(p.captions[0].name == "English");
    CHECK(p.captions[0].translatable);
    CHECK_FALSE(p.captions[0].autoGenerated);
    CHECK(p.captions[1].autoGenerated);
    CHECK(p.captions[1].url.find("kind=asr") != std::string::npos);
    CHECK(p.storyboardSpec.find("storyboard3_L$L") != std::string::npos);
    CHECK(p.watchtimeUrl == "https://s.youtube.com/api/stats/watchtime?cl=1&docid=dQw4w9WgXcQ&ei=EI&c=VISIONOS");
    CHECK(p.playbackUrl.find("/api/stats/playback") != std::string::npos);
    CHECK(p.details.id == "dQw4w9WgXcQ");
    CHECK(p.details.author == "Rick Astley");
    CHECK(p.details.channelId == "UCuAXFkgsw1L7xaCfnd5JJOw");
    CHECK(p.details.lengthSecs == 213);
    CHECK(p.details.viewCount == 1826490705LL);
    CHECK(p.details.thumbnailUrl == "https://i.ytimg.com/vi/dQw4w9WgXcQ/hq720.jpg");
}
TEST_CASE("playerResponseProgressiveFromFormats") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_vr_ok.json"));
    REQUIRE(p.formats.size() == 3);
    CHECK(p.formats[0].itag == 18);
    CHECK((p.formats[0].hasVideo && p.formats[0].hasAudio));   // formats[] are muxed
    CHECK(p.formats[0].codecs == "avc1.42001E, mp4a.40.2");
    CHECK(p.hlsUrl.empty());
}
TEST_CASE("playerResponseAudioTracks") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_dubbed.json"));
    REQUIRE(p.formats.size() == 3);
    REQUIRE(p.formats[1].audio.has_value());
    const AudioTrack &ru = *p.formats[1].audio;
    CHECK(ru.id == "ru.3");
    CHECK(ru.name == "Russian");
    CHECK(ru.language == "ru");
    CHECK_FALSE(ru.isDefault);
    CHECK_FALSE(ru.original);                         // xtags: acont=dubbed
    CHECK_FALSE(ru.autoDubbed);
    REQUIRE(p.formats[2].audio.has_value());
    const AudioTrack &en = *p.formats[2].audio;
    CHECK(en.language == "en-US");
    CHECK(en.isDefault);
    CHECK(en.original);                               // xtags: acont=original
    CHECK(p.formats[2].xtags == "ChEKBWFjb250EghvcmlnaW5hbAoNCgRsYW5nEgVlbi1VUw");
}
TEST_CASE("playerResponseVerdicts") {
    PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_botcheck.json"));
    CHECK(p.status.kind == Playability::BotCheck);
    CHECK(p.status.status == "LOGIN_REQUIRED");
    CHECK(p.status.reason.find("not a bot") != std::string::npos);
    CHECK_FALSE(p.final);
    p = parsePlayerResponse(loadFixtureRaw("player_removed.json"));
    CHECK(p.status.kind == Playability::Unplayable);
    CHECK(p.status.reason == "This video is unavailable");
    CHECK(p.final);
    // Review focus: the reason is localized by hl — the verdict must not read it.
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
        "\"reason\":\"Войдите, чтобы подтвердить, что вы не бот\"}}"));
    CHECK(p.status.kind == Playability::BotCheck);
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
        "\"reason\":\"Sign in to confirm your age\",\"desktopLegacyAgeGateReason\":1}}"));
    CHECK(p.status.kind == Playability::AgeRestricted);
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"AGE_CHECK_REQUIRED\"}}"));
    CHECK(p.status.kind == Playability::AgeRestricted);
    p = parsePlayerResponse(std::string("not json"));
    CHECK(p.status.kind == Playability::Unknown);
    CHECK(p.formats.empty());
}
TEST_CASE("playerResponseUpcoming") {
    // Shape per Troubadour (YTApi.m scheduledStartIn): no scheduled stream was at hand to capture.
    PlayerPart p = parsePlayerResponse(std::string(
        "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\",\"reason\":\"This live event will begin in 3 days.\","
        "\"liveStreamability\":{\"liveStreamabilityRenderer\":{\"offlineSlate\":{\"liveStreamOfflineSlateRenderer\":"
        "{\"scheduledStartTime\":\"1792000000\",\"mainText\":{\"runs\":[{\"text\":\"Live in 3 days\"}]},"
        "\"subtitleText\":{\"simpleText\":\"October 13, 8:00 PM\"}}}}}}}"));
    CHECK(p.status.kind == Playability::Upcoming);
    CHECK(p.status.scheduledStartSecs == 1792000000LL);
    CHECK(p.status.slateText == "Live in 3 days\nOctober 13, 8:00 PM");
    CHECK(p.final);
    // No slate: the microformat's ISO time (with its zone) is the fallback.
    p = parsePlayerResponse(std::string(
        "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"},\"microformat\":{\"playerMicroformatRenderer\":"
        "{\"liveBroadcastDetails\":{\"startTimestamp\":\"2026-09-09T20:00:00+03:00\"}}}}"));
    CHECK(p.status.kind == Playability::Upcoming);
    CHECK(p.status.scheduledStartSecs == 1788973200LL);
    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"}}"));
    CHECK(p.status.kind == Playability::LiveOffline);
    CHECK(p.final);
}
TEST_CASE("playerResponseSealedFormat") {
    const PlayerPart p = parsePlayerResponse(std::string(
        "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"formats\":[{\"itag\":18,"
        "\"mimeType\":\"video/mp4\",\"signatureCipher\":\"s=AB&sp=sig&url=https%3A%2F%2Fx\"}]}}"));
    REQUIRE(p.formats.size() == 1);
    CHECK(p.formats[0].sealed);
    CHECK(p.formats[0].url.empty());
}
// The slate search walks the whole answer: it must stay bounded like every scan.
TEST_CASE("playerResponseSlateScanDepthBounded") {
    const int n = 200000;
    std::string deep = "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"},\"x\":";
    for (int i = 0; i < n; ++i) deep += "{\"a\":";
    deep += "{}";
    for (int i = 0; i < n; ++i) deep += "}";
    deep += "}";
    const PlayerPart p = parsePlayerResponse(deep);   // must return, not overflow the stack
    CHECK(p.status.kind == Playability::LiveOffline);
}
TEST_CASE("storyboardSpec") {
    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_ok.json"));
    const std::vector<StoryboardLevel> l = parseStoryboardSpec(p.storyboardSpec, 213);
    REQUIRE(l.size() == 2);
    CHECK(l[0].urlTemplate == "https://i.ytimg.com/sb/dQw4w9WgXcQ/storyboard3_L0/default.jpg?sqp=SQP&sigh=rs$SIGH0");
    CHECK(l[0].width == 48);
    CHECK(l[0].height == 27);
    CHECK(l[0].count == 100);
    CHECK(l[0].intervalMs == 2130);                   // 0 in the spec: length / count
    CHECK(l[1].urlTemplate == "https://i.ytimg.com/sb/dQw4w9WgXcQ/storyboard3_L1/M$M.jpg?sqp=SQP&sigh=rs$SIGH1");
    CHECK(l[1].count == 108);
    CHECK(l[1].cols == 10);
    CHECK(l[1].rows == 10);
    CHECK(l[1].intervalMs == 2000);
    CHECK(parseStoryboardSpec("", 10).empty());
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|1#2", 10).empty());   // a short level is skipped
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|a#b#c#d#e#f#g#h", 10).empty());
}
TEST_CASE("json3Cues") {
    std::vector<Cue> c = parseJson3(loadFixtureRaw("captions_manual.json3"));
    REQUIRE(c.size() == 3);
    CHECK(c[0].startMs == 1360);
    CHECK(c[0].endMs == 3040);
    CHECK(c[0].text == "[♪♪♪]");
    CHECK(c[2].text == "♪ You know the rules\nand so do I ♪");
    c = parseJson3(loadFixtureRaw("captions_asr.json3"));
    REQUIRE(c.size() == 2);                           // the window event and the "\n" append are not cues
    CHECK(c[0].text == "[Music]");
    CHECK(c[1].text == "We're no strangers to");
    CHECK(c[1].startMs == 18800);
    CHECK(c[1].endMs == 25960);
    CHECK(parseJson3("").empty());
    CHECK(parseJson3("<html>Sorry</html>").empty());
}

// The /tv page (TV UA, 2026-10-14) names the build as ".../s/player/22cb88bc/tv-player-ias…";
// base.js carries "signatureTimestamp:20737".
TEST_CASE("playerJsScans") {
    CHECK(playerIdIn("<script src=\"/s/player/22cb88bc/tv-player-ias.vflset/tv-player-ias.js\">") == "22cb88bc");
    CHECK(playerIdIn("\"jsUrl\":\"\\/s\\/player\\/22cb88bc\\/tv-player-ias.vflset\\/x.js\"") == "22cb88bc");
    CHECK(playerIdIn("<html>no player here</html>").empty());
    CHECK(playerIdIn("/s/player/bad\"id/x.js").empty());
    CHECK(playerScriptUrl("22cb88bc") == "https://www.youtube.com/s/player/22cb88bc/player_ias.vflset/en_US/base.js");
    CHECK(signatureTimestampIn("var a={signatureTimestamp:20737,b:1}") == 20737);
    CHECK(signatureTimestampIn("\"signatureTimestamp\": 20737") == 20737);
    CHECK(signatureTimestampIn("x.signatureTimestamp=y; z={signatureTimestamp:19999}") == 19999);
    CHECK(signatureTimestampIn("nothing") == 0);
}
// Security review: a count past INT_MAX truncated to (int) 0 and the interval fallback
// divided by it (SIGFPE on the engine thread); huge cells overflowed storyboardFrame's int
// math and a huge length overflowed lengthSecs·1000. Out-of-range levels are skipped.
TEST_CASE("storyboardSpecRejectsOutOfRangeNumbers") {
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|48#27#4294967296#10#10#0#d#s", 213).empty());
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|99999999999#27#100#10#10#0#d#s", 213).empty());
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|48#27#100#4294967306#10#0#d#s", 213).empty());
    CHECK(parseStoryboardSpec("https://x/$L/$N.jpg|48#27#100#70000#10#0#d#s", 213).empty());
    const std::vector<StoryboardLevel> l =
        parseStoryboardSpec("https://x/$L/$N.jpg|48#27#100#10#10#0#d#s", INT64_MAX);
    REQUIRE(l.size() == 1);
    CHECK(l[0].intervalMs == 0);                      // no fallback from an absurd length
}

// The fields of a real (trimmed) commentEntityPayload: the comment id, the author's channel,
// the viewer's own comment, the displayed like and reply counts.
TEST_CASE("a comment carries its id, author channel, likes, replies and mine") {
    const std::string page =
        "{\"frameworkUpdates\":{\"entityBatchUpdate\":{\"mutations\":[{\"payload\":{\"commentEntityPayload\":{"
        "\"properties\":{\"commentId\":\"Ugxzbv\",\"content\":{\"content\":\"Hi\"},\"publishedTime\":\"6 years ago\"},"
        "\"author\":{\"channelId\":\"UCNLgz\",\"displayName\":\"@candy\",\"avatarThumbnailUrl\":\"https://yt3/a\","
        "\"isCurrentUser\":true},"
        "\"toolbar\":{\"likeCountNotliked\":\"291K\",\"replyCount\":\"675\"}}}}]}}}";
    std::string token;
    const std::vector<CommentThread> c = parseComments(page, &token);
    REQUIRE(c.size() == 1);
    CHECK(c[0].comment.id == "Ugxzbv");
    CHECK(c[0].comment.text == "Hi");
    CHECK(c[0].comment.publishedText == "6 years ago");
    CHECK(c[0].comment.author.id == "UCNLgz");
    CHECK(c[0].comment.author.name == "@candy");
    CHECK(c[0].comment.author.avatarUrl == "https://yt3/a");
    CHECK(c[0].comment.mine);
    CHECK(c[0].comment.likeText == "291K");
    CHECK(c[0].replyText == "675");
}
// The related list continues from secondaryResults; the comments section's token comes
// first in the document and must not be taken for it.
TEST_CASE("a watch page: date, exact views and the related list's continuation") {
    const std::string next =
        "{\"contents\":{\"twoColumnWatchNextResults\":{\"results\":{\"results\":{\"contents\":["
        "{\"videoPrimaryInfoRenderer\":{\"title\":{\"simpleText\":\"T\"},\"dateText\":{\"simpleText\":\"Oct 25, 2009\"},"
        "\"viewCount\":{\"videoViewCountRenderer\":{\"viewCount\":{\"simpleText\":\"1,234,567 views\"}}}}},"
        "{\"itemSectionRenderer\":{\"contents\":[{\"continuationItemRenderer\":{\"continuationEndpoint\":"
        "{\"continuationCommand\":{\"token\":\"COMMENTS\"}}}}]}}]}},"
        "\"secondaryResults\":{\"secondaryResults\":{\"results\":["
        "{\"lockupViewModel\":{\"contentId\":\"rel1\",\"contentType\":\"LOCKUP_CONTENT_TYPE_VIDEO\"}},"
        "{\"continuationItemRenderer\":{\"continuationEndpoint\":{\"continuationCommand\":{\"token\":\"RELATED\"}}}}]}}}}}";
    Video v;
    std::string related;
    parseWatchPage(next, &v, &related);
    CHECK(v.title == "T");
    CHECK(v.publishedText == "Oct 25, 2009");
    CHECK(v.viewText == "1,234,567 views");
    CHECK(v.viewCount == 1234567);
    REQUIRE(v.related.items.size() == 1);
    CHECK(v.related.items[0].id == "rel1");
    CHECK(related == "RELATED");
}
