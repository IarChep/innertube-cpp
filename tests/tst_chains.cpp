// core::chains — the request layer on std types, driven over FakeHttp (port of
// MeeTube's tst_meetube_chains: same fixtures, same sent-body and routing asserts;
// Outcome<T> -> yt::Result<T> with error codes).
#include <doctest/doctest.h>
#include "core/chains.h"
#include "core/cursor.h"
#include "innertube/catalog.h"
#include "fakehttp.h"
#include "testutil.h"

using namespace yt;
using namespace yt::core;

namespace {
template <class T> struct Cap {
    int calls = 0;
    Result<T> r;
    Callback<T> fn() { return [this](const Result<T> &x) { ++calls; r = x; }; }
};
bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }
const std::string *formValue(const Form &f, const char *key) {
    for (const auto &kv : f) if (kv.first == key) return &kv.second;
    return nullptr;
}
}

// ---- fetchPlayer: the client walk ----
namespace {
const char *kOkEmpty = "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"adaptiveFormats\":[]}}";
const char *kTvSabr = "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"adaptiveFormats\":"
    "[{\"itag\":137,\"mimeType\":\"video/mp4\"}],\"serverAbrStreamingUrl\":\"https://rr.example/sabr?c=TVHTML5\"},"
    "\"playbackTracking\":{\"videostatsWatchtimeUrl\":{\"baseUrl\":\"https://s.youtube.com/api/stats/watchtime?c=TVHTML5\"}}}";
const char *kUnplayable = "{\"playabilityStatus\":{\"status\":\"UNPLAYABLE\",\"reason\":\"The page needs to be reloaded.\"}}";
const char *kSeed = "{\"responseContext\":{\"visitorData\":\"FRESH\"},\"playabilityStatus\":{\"status\":\"UNPLAYABLE\"},"
    "\"captions\":{\"playerCaptionsTracklistRenderer\":{\"captionTracks\":[{\"baseUrl\":\"https://www.youtube.com/api/timedtext?web=1\",\"languageCode\":\"en\"}]}}}";
std::vector<int> ids(std::initializer_list<ClientId> c)
{
    std::vector<int> v;
    for (ClientId x : c) v.push_back((int) x);
    return v;
}
}

TEST_CASE("the walk stops once every wanted kind is covered") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"dQw4w9WgXcQ", WantAdaptive | WantHls | WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS}));
    REQUIRE(c.r.value.sources.size() == 1);
    const Source &s = c.r.value.sources[0];
    CHECK(s.client == ClientId::VISIONOS);
    CHECK(s.userAgent == defaultClientInfo(ClientId::VISIONOS).userAgent);
    CHECK(s.formats.size() == 4);
    CHECK(s.hlsUrl.find("manifest.example") != std::string::npos);
    CHECK(s.sabrConfig == "CgUSA2FiYw");
    CHECK(s.expiresAtSecs > 1700000000);
    CHECK(c.r.value.status.kind == Playability::Ok);
    CHECK(c.r.value.details.title.find("Never Gonna") != std::string::npos);
    CHECK(c.r.value.storyboard.size() == 2);
    CHECK(c.r.value.captions.size() == 2);
    CHECK(c.r.value.cpn.size() == 16);
    CHECK(has(t.sent[0], "\"videoId\":\"dQw4w9WgXcQ\""));
    CHECK_FALSE(has(t.sent[0], "playbackContext"));   // only TV needs it
}
TEST_CASE("progressive comes from ANDROID_VR and every source keeps its own UA") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    t.queue("player", loadFixtureRaw("player_vr_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::ANDROID_VR}));   // HLS already covered: no IOS
    REQUIRE(c.r.value.sources.size() == 2);
    CHECK(c.r.value.sources[1].client == ClientId::ANDROID_VR);
    CHECK(c.r.value.sources[1].userAgent == defaultClientInfo(ClientId::ANDROID_VR).userAgent);
    CHECK(c.r.value.sources[1].formats[0].itag == 18);
}
TEST_CASE("without visitorData a WEB answer seeds it first") {
    FakeHttp t;
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::WEB, ClientId::VISIONOS}));
    CHECK(t.session().visitorData == "FRESH");
    REQUIRE(c.r.value.sources.size() == 1);          // the seed is never a source
    CHECK(c.r.value.sources[0].client == ClientId::VISIONOS);
    REQUIRE_FALSE(c.r.value.captions.empty());       // WEB caption URLs need pot: not taken
    CHECK(c.r.value.captions[0].url.find("web=1") == std::string::npos);
}
TEST_CASE("a bot check reseeds visitorData once and repeats the same client") {
    FakeHttp t; t.session().visitorData = "FLAGGED";
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::WEB, ClientId::VISIONOS}));
    CHECK(t.visitorResets == 1);
    CHECK(c.r.value.status.kind == Playability::Ok);
    CHECK(c.r.value.sources.size() == 1);
}
TEST_CASE("a second bot check is not retried and the verdict is reported") {
    FakeHttp t; t.session().visitorData = "FLAGGED";
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());                                // an answer arrived: not a failure
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::WEB, ClientId::VISIONOS, ClientId::ANDROID_VR}));
    CHECK(t.visitorResets == 1);
    CHECK(c.r.value.status.kind == Playability::BotCheck);
    CHECK(c.r.value.sources.empty());
}
TEST_CASE("a later bot check does not hide an earlier OK") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    t.queue("player", kSeed, 200, "FRESH");
    t.queue("player", loadFixtureRaw("player_botcheck.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantProgressive}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.status.kind == Playability::Ok);
    CHECK(c.r.value.sources.empty());                 // nothing progressive anywhere
}
TEST_CASE("a removed video stops the walk") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_removed.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
    CHECK(c.r.value.status.kind == Playability::Unplayable);
    CHECK(c.r.value.status.reason == "This video is unavailable");
}
TEST_CASE("a scheduled stream stops the walk and keeps its start time") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\",\"liveStreamability\":"
        "{\"liveStreamabilityRenderer\":{\"offlineSlate\":{\"liveStreamOfflineSlateRenderer\":"
        "{\"scheduledStartTime\":\"1792000000\"}}}}}}");
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
    CHECK(c.r.value.status.kind == Playability::Upcoming);
    CHECK(c.r.value.status.scheduledStartSecs == 1792000000LL);
}
TEST_CASE("signed in: TV is asked for SABR with sts, cpn and playbackContext") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.session().playerId = "p1"; t.session().signatureTimestamp = 20737;
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kTvSabr);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::ANDROID_VR, ClientId::TVHTML5}));
    CHECK(t.fetched.empty());                         // sts already known
    REQUIRE(t.sent.size() == 3);
    CHECK(has(t.sent[2], "\"signatureTimestamp\":20737"));
    CHECK(has(t.sent[2], ("\"cpn\":\"" + c.r.value.cpn + "\"").c_str()));
    CHECK(has(t.sent[2], "contentPlaybackContext"));
    REQUIRE(c.r.value.sources.size() == 1);
    CHECK(c.r.value.sources[0].client == ClientId::TVHTML5);
    CHECK(c.r.value.watchtimeUrl.find("c=TVHTML5") != std::string::npos);   // the account's view
}
// Review focus: a signed-in user whose needs VISIONOS covers pays neither for the TV
// request nor for the 3 MB base.js download.
TEST_CASE("signed in but covered: no TV request and no base.js") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive | WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS}));
    CHECK(t.fetched.empty());
}
TEST_CASE("sts is read from base.js once and persisted") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.queue(Catalog::kTvPageUrl, "<script src=\"/s/player/22cb88bc/tv-player-ias.vflset/tv-player-ias.js\">");
    t.queue("https://www.youtube.com/s/player/22cb88bc/player_ias.vflset/en_US/base.js", "x={signatureTimestamp:20737}");
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kTvSabr);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(t.fetched.size() == 2);
    CHECK(t.fetched[0].url == Catalog::kTvPageUrl);
    REQUIRE_FALSE(t.fetched[0].headers.empty());
    CHECK(t.fetched[0].headers[0].value == defaultClientInfo(ClientId::TVHTML5).userAgent);
    CHECK(t.playerJsSets == 1);
    CHECK(t.session().signatureTimestamp == 20737);
    CHECK(has(t.sent.back(), "\"signatureTimestamp\":20737"));
    // A second walk does not download anything again.
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kTvSabr);
    Cap<Player> d;
    fetchPlayer(t, {"v", WantSabr}, newJob(), d.fn());
    t.flush();
    CHECK(t.fetched.size() == 2);
}
TEST_CASE("a TV UNPLAYABLE re-reads the player build once") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "tok";
    t.session().playerId = "old"; t.session().signatureTimestamp = 1;
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", kUnplayable);
    t.queue(Catalog::kTvPageUrl, "/s/player/new1/tv-player-ias.vflset/x.js");
    t.queue("https://www.youtube.com/s/player/new1/player_ias.vflset/en_US/base.js", "signatureTimestamp:2");
    t.queue("player", kTvSabr);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::ANDROID_VR, ClientId::TVHTML5, ClientId::TVHTML5}));
    CHECK(has(t.sent.back(), "\"signatureTimestamp\":2"));
    CHECK(t.session().playerId == "new1");
    REQUIRE(c.r.value.sources.size() == 1);
    CHECK(c.r.value.status.kind == Playability::Ok);
}
TEST_CASE("empty sources after a TV 401 report 401 so the bearer is refreshed") {
    FakeHttp t; t.session().visitorData = "VD"; t.session().bearer = "stale";
    t.session().playerId = "p1"; t.session().signatureTimestamp = 1;
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    t.queue("player", "{}", 401);
    Cap<Player> c;
    fetchPlayer(t, {"v", WantSabr}, newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->httpStatus == 401);
}
TEST_CASE("no answer at all fails with the transport error") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", "", 0);
    t.queue("player", "", 0);
    t.queue("player", "", 0);
    Cap<Player> c;
    fetchPlayer(t, {"v"}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Transport);
}
TEST_CASE("want 0 asks one client for metadata only") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_visionos_ok.json"));
    Cap<Player> c;
    fetchPlayer(t, {"v", 0}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
    CHECK(c.r.value.captions.size() == 2);
}
TEST_CASE("a walk canceled between steps delivers nothing") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", kOkEmpty);
    t.queue("player", kOkEmpty);
    JobToken job = newJob();
    Cap<Player> c;
    fetchPlayer(t, {"v"}, job, c.fn());
    t.flushOne(0);                                    // VISIONOS answers; ANDROID_VR goes out
    job->canceled = true;
    t.flush();
    CHECK(c.calls == 0);
    CHECK(t.sent.size() <= 2);
}
TEST_CASE("HLS still missing after VISIONOS: IOS is asked, without a playback context") {
    FakeHttp t; t.session().visitorData = "VD";
    t.queue("player", loadFixtureRaw("player_vr_ok.json"));          // as VISIONOS: no HLS
    t.queue("player", kOkEmpty);
    t.queue("player", "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":"
                      "{\"hlsManifestUrl\":\"https://manifest.example/ios.m3u8\"}}");
    Cap<Player> c;
    fetchPlayer(t, {"v", WantAdaptive | WantHls}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sentClients == ids({ClientId::VISIONOS, ClientId::ANDROID_VR, ClientId::IOS}));
    REQUIRE(c.r.value.sources.size() == 2);
    CHECK(c.r.value.sources[1].client == ClientId::IOS);
    CHECK(c.r.value.sources[1].hlsUrl == "https://manifest.example/ios.m3u8");
    CHECK_FALSE(has(t.sent[2], "playbackContext"));
}

// ---- lists: Page<T> + cursors ----
namespace {
template <class T> std::string tokenOf(const Cursor<T> &c) { return detail::CursorAccess::token(c); }
const char *kSearchPage1 =
    "{\"contents\":{\"twoColumnSearchResultsRenderer\":{\"primaryContents\":{\"sectionListRenderer\":{\"contents\":["
    "{\"itemSectionRenderer\":{\"contents\":[{\"videoRenderer\":{\"videoId\":\"aaa11111111\",\"title\":{\"runs\":[{\"text\":\"A\"}]}}}]}},"
    "{\"continuationItemRenderer\":{\"continuationEndpoint\":{\"continuationCommand\":{\"token\":\"SEARCH1\"}}}}]}}}}}";
const char *kSearchPage2 =
    "{\"onResponseReceivedCommands\":[{\"appendContinuationItemsAction\":{\"continuationItems\":["
    "{\"itemSectionRenderer\":{\"contents\":[{\"videoRenderer\":{\"videoId\":\"bbb22222222\",\"title\":{\"runs\":[{\"text\":\"B\"}]}}}]}}]}}]}";
}

TEST_CASE("videoBrowseFeed") {
    FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEwhat_to_watch";
    Cap<Page<VideoItem>> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 2);
    CHECK(c.r.value.items[0].id == "ccc33333333");
    CHECK(tokenOf(c.r.value.next) == "FEEDNEXT");
    CHECK(has(t.sent[0], "\"browseId\":\"FEwhat_to_watch\""));
}
TEST_CASE("a signed-in feed continues on the client that served page 1") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", loadFixtureRaw("browse_feed.json"));
    t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.browseId = "FEsubscriptions";
    Cap<Page<VideoItem>> first, second;
    fetchVideoList(t, spec, newJob(), first.fn());
    t.flush();
    REQUIRE(first.r.ok());
    REQUIRE(first.r.value.next);
    fetchMore(t, first.r.value.next, newJob(), second.fn());
    t.flush();
    REQUIRE(second.r.ok());
    CHECK(t.sentClients == std::vector<int>{(int) ClientId::TVHTML5, (int) ClientId::TVHTML5});
    CHECK(t.sent[1] == "{\"continuation\":\"FEEDNEXT\"}");
}
// Review focus: the second search page posts only the continuation — resending the query
// makes YouTube answer page 1 again.
TEST_CASE("search pages continue through the search endpoint without the query") {
    FakeHttp t;
    t.queue("search", kSearchPage1);
    t.queue("search", kSearchPage2);
    VideoListSpec spec; spec.kind = VideoListSpec::Search; spec.query = "cats"; spec.sort = SearchSort::UploadDate;
    Cap<Page<VideoItem>> first, second;
    fetchVideoList(t, spec, newJob(), first.fn());
    t.flush();
    REQUIRE(first.r.ok());
    CHECK(has(t.sent[0], "\"query\":\"cats\""));
    CHECK(has(t.sent[0], "\"params\":\"CAISAhAB\""));
    CHECK(tokenOf(first.r.value.next) == "SEARCH1");
    fetchMore(t, first.r.value.next, newJob(), second.fn());
    t.flush();
    REQUIRE(second.r.ok());
    REQUIRE(second.r.value.items.size() == 1);
    CHECK(second.r.value.items[0].id == "bbb22222222");
    CHECK(t.sent[1] == "{\"continuation\":\"SEARCH1\"}");
    CHECK(t.lastClientFor("search") == (int) ClientId::WEB);
    CHECK_FALSE(second.r.value.next);                 // the last page
}
// Review focus: an exhausted cursor fails and sends nothing.
TEST_CASE("more with no next page fails with InvalidArgument and sends nothing") {
    FakeHttp t;
    Cap<Page<VideoItem>> c;
    fetchMore(t, Cursor<VideoItem>{}, newJob(), c.fn());
    t.flush();
    REQUIRE(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::InvalidArgument);
    CHECK(t.sent.empty());
}
TEST_CASE("videoListTransportErrorKeepsCode") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", "{\"error\":{}}", 401);
    VideoListSpec spec; spec.browseId = "FEsubscriptions";
    Cap<Page<VideoItem>> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Api);
    CHECK(c.r.error->httpStatus == 401);
}
TEST_CASE("comments: two steps, then pages through next") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    Cap<Page<CommentThread>> c, more;
    fetchComments(t, "aaa11111111", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.items.size() == 2);
    CHECK(tokenOf(c.r.value.next) == "MORE_COMMENTS");
    CHECK(has(t.sent[0], "\"videoId\":\"aaa11111111\""));
    CHECK(has(t.sent[1], "\"continuation\":\"COMMENTS_TOKEN\""));
    fetchMore(t, c.r.value.next, newJob(), more.fn());
    t.flush();
    REQUIRE(more.r.ok());
    CHECK(t.sent[2] == "{\"continuation\":\"MORE_COMMENTS\"}");
    CHECK(t.lastClientFor("next") == (int) ClientId::WEB);
}
TEST_CASE("commentsDisabled") {
    FakeHttp t; t.queue("next", "{}");
    Cap<Page<CommentThread>> c;
    fetchComments(t, "aaa11111111", newJob(), c.fn());
    t.flush();
    CHECK(t.sent.size() == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.items.empty());
    CHECK(c.r.value.disabled);
    CHECK_FALSE(c.r.value.next);
}
TEST_CASE("commentsCanceledBetweenSteps") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    JobToken job = newJob();
    Cap<Page<CommentThread>> c;
    fetchComments(t, "aaa11111111", job, c.fn());
    job->canceled = true;
    t.flush();
    CHECK(c.calls == 0);
    CHECK(t.sent.size() == 1);
}
TEST_CASE("playlistSearch") {
    FakeHttp t;
    t.queue("search", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL1\",\"title\":{\"simpleText\":\"L\"}}}]}");
    Cap<Page<Playlist>> c;
    fetchPlaylistSearch(t, "foo", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 1);
    CHECK(c.r.value.items[0].id == "PL1");
    CHECK(has(t.sent[0], "\"params\":\"EgIQAw==\""));
}
TEST_CASE("channelListBrowsesGrid") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse",
            "{\"contents\":[{\"gridChannelRenderer\":{\"channelId\":\"UCsub1\","
            "\"title\":{\"simpleText\":\"Sub One\"}}},"
            "{\"gridChannelRenderer\":{\"channelId\":\"UCsub2\","
            "\"title\":{\"simpleText\":\"Sub Two\"}}}]}");
    Cap<Page<Channel>> c;
    fetchChannelList(t, "FEchannels", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 2);
    CHECK(c.r.value.items[0].id == "UCsub1");
    CHECK(has(t.sent[0], "\"browseId\":\"FEchannels\""));
    CHECK(t.lastClientFor("browse") == (int) ClientId::TVHTML5);
}
TEST_CASE("playlistBrowse") {
    FakeHttp t;
    t.queue("browse", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"X\"}}}]}");
    Cap<Page<Playlist>> c;
    fetchPlaylists(t, "UCabc", "", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 1);
    CHECK(c.r.value.items[0].id == "PL9");
    CHECK(has(t.sent[0], "\"browseId\":\"UCabc\""));
}

// ---- fetchWatch ----
TEST_CASE("videoLoadWatch") {
    FakeHttp t; t.queue("next", loadFixtureRaw("watch_next.json"));
    Cap<Video> c;
    fetchWatch(t, "vid42", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.id == "vid42");
    CHECK(c.r.value.description == "Hello description");
    CHECK(c.r.value.related.items.size() >= 1);
    CHECK(has(t.sent[0], "\"videoId\":\"vid42\""));
}



// ---- playlists / channels ----
TEST_CASE("channelByIdUnavailable") {
    FakeHttp t; t.queue("browse", "{}");
    Cap<Channel> c;
    fetchChannelById(t, "UCbad", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(has(c.r.error->message, "channel unavailable"));
    CHECK(c.r.error->code == ErrorCode::Unavailable);
}
TEST_CASE("channelSubscribedUsesTv") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", "{\"subscribeButtonRenderer\":{\"subscribed\":true}}");
    Cap<bool> c;
    fetchChannelSubscribed(t, "UCx", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value);
    CHECK(t.lastClientFor("browse") == (int)ClientId::TVHTML5);
}
TEST_CASE("userSearch") {
    FakeHttp t;
    t.queue("search", "{\"contents\":[{\"channelRenderer\":{\"channelId\":\"UCx\",\"title\":{\"simpleText\":\"Ch\"}}}]}");
    Cap<Page<Channel>> c;
    fetchUserSearch(t, "foo", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 1);
    CHECK(c.r.value.items[0].id == "UCx");
    CHECK(has(t.sent[0], "\"query\":\"foo\""));
    CHECK(has(t.sent[0], "EgIQAg=="));
}

// ---- suggestions ----
TEST_CASE("searchSuggestions") {
    FakeHttp t; t.setGetBody("[\"cat\",[\"cat videos\",\"cats\"]]");
    Cap<std::vector<std::string>> c;
    fetchSearchSuggestions(t, "cat", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.size() == 2);
    CHECK(c.r.value[0] == "cat videos");
    CHECK(has(t.lastGetUrl(), "q=cat"));
    CHECK(has(t.lastGetUrl(), "client=firefox"));
    CHECK(has(t.lastGetUrl(), "hl=en"));
}
TEST_CASE("searchSuggestionsEncodesUserInput") {
    FakeHttp t; t.setGetBody("[\"x\",[]]");
    t.session().hl = "ru";
    Cap<std::vector<std::string>> c;
    fetchSearchSuggestions(t, "кот & co", newJob(), c.fn());
    t.flush();
    CHECK(has(t.lastGetUrl(), "q=%D0%BA%D0%BE%D1%82%20%26%20co"));
    CHECK(has(t.lastGetUrl(), "hl=ru"));
}
TEST_CASE("searchSuggestionsTransportError") {
    FakeHttp t;
    Cap<std::vector<std::string>> c;
    fetchSearchSuggestions(t, "cat", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK_FALSE(c.r.ok());
    CHECK(c.r.value.empty());
}

// ---- fetchAccount ----
TEST_CASE("accountFetchesIdentity") {
    FakeHttp t; t.queue("account/accounts_list", loadFixtureRaw("accounts_list.json"));
    Cap<AccountInfo> c;
    fetchAccount(t, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.name == "Ivan Petrov");
    CHECK(c.r.value.channelId == "UCabc123def");
    CHECK(has(t.sent[0], "accountReadMask"));
    CHECK(t.lastClientFor("account/accounts_list") == (int)ClientId::TVHTML5);
}
TEST_CASE("accountUnavailable") {
    FakeHttp t; t.queue("account/accounts_list", "{}");
    Cap<AccountInfo> c;
    fetchAccount(t, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(has(c.r.error->message, "account unavailable"));
}

// ---- submitAction / editPlaylist ----
TEST_CASE("actions") {
    struct Row { ActionKind kind; const char *endpoint; const char *target; const char *needle; };
    const Row rows[] = {
        {Subscribe, "subscription/subscribe", "UCabc", "channelIds"},
        {Unsubscribe, "subscription/unsubscribe", "UCabc", "channelIds"},
        {Like, "like/like", "vid1", "\"target\":{\"videoId\":\"vid1\"}"},
        {Dislike, "like/dislike", "vid2", "\"target\":{\"videoId\":\"vid2\"}"},
        {RemoveLike, "like/removelike", "vid3", "\"target\":{\"videoId\":\"vid3\"}"},
    };
    for (const Row &row : rows) {
        CAPTURE(row.endpoint);
        FakeHttp t; t.queue(row.endpoint, "{}");
        Cap<Done> c;
        submitAction(t, row.kind, row.target, newJob(), c.fn());
        t.flush();
        CHECK(c.calls == 1);
        CHECK(c.r.ok());
        CHECK(has(t.sent[0], row.needle));
        CHECK(t.lastClientFor(row.endpoint) == (int)ClientId::TVHTML5);
    }
}
TEST_CASE("actionTransportFails") {
    FakeHttp t;
    Cap<Done> c;
    submitAction(t, Like, "vidX", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK_FALSE(c.r.ok());
}

// ---- OAuth chains ----
TEST_CASE("oauthDeviceCodeFields") {
    FakeHttp t;
    t.queue(Catalog::kDeviceCodeUrl,
            "{\"device_code\":\"DC\",\"user_code\":\"ABCD-EFGH\","
            "\"verification_url\":\"https://youtube.com/activate\",\"interval\":7}");
    Cap<DeviceCodeGrant> c;
    oauthDeviceCode(t, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.deviceCode == "DC");
    CHECK(c.r.value.userCode == "ABCD-EFGH");
    CHECK(c.r.value.verificationUrl == "https://youtube.com/activate");
    CHECK(c.r.value.intervalSecs == 7);
    REQUIRE(t.sentForm.size() == 1);
    CHECK(t.sentForm[0].first == Catalog::kDeviceCodeUrl);
    CHECK(formValue(t.sentForm[0].second, "client_id") != nullptr);
    CHECK(formValue(t.sentForm[0].second, "scope") != nullptr);
}
TEST_CASE("oauthDeviceCodeUriFallback") {
    FakeHttp t;
    t.queue(Catalog::kDeviceCodeUrl, "{\"device_code\":\"DC\",\"user_code\":\"WXYZ\",\"verification_uri\":\"https://g.co/dev\"}");
    Cap<DeviceCodeGrant> c;
    oauthDeviceCode(t, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.verificationUrl == "https://g.co/dev");
    CHECK(c.r.value.intervalSecs == 5);
}
TEST_CASE("oauthDeviceCodeMalformed") {
    FakeHttp t; t.queue(Catalog::kDeviceCodeUrl, "{\"user_code\":\"X\"}");
    Cap<DeviceCodeGrant> c;
    oauthDeviceCode(t, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(has(c.r.error->message, "device code request failed"));
}
TEST_CASE("oauthPollTokenGrant") {
    FakeHttp t; t.queue(Catalog::kTokenUrl, "{\"access_token\":\"AT\",\"refresh_token\":\"RT\",\"expires_in\":3599}");
    TokenGrant g; int calls = 0;
    oauthPollToken(t, "DC", newJob(), [&](const TokenGrant &r) { g = r; ++calls; });
    t.flush();
    CHECK(calls == 1);
    CHECK(g.transportOk);
    CHECK(g.accessToken == "AT");
    CHECK(g.refreshToken == "RT");
    CHECK(g.expiresInSecs == 3599);
    CHECK(g.error.empty());
    REQUIRE(t.sentForm.size() == 1);
    CHECK(t.sentForm[0].first == Catalog::kTokenUrl);
    REQUIRE(formValue(t.sentForm[0].second, "device_code") != nullptr);
    CHECK(*formValue(t.sentForm[0].second, "device_code") == "DC");
    CHECK(has(*formValue(t.sentForm[0].second, "grant_type"), "device_code"));
}
TEST_CASE("oauthPollTokenPending") {
    FakeHttp t; t.queue(Catalog::kTokenUrl, "{\"error\":\"authorization_pending\"}");
    TokenGrant g; int calls = 0;
    oauthPollToken(t, "DC", newJob(), [&](const TokenGrant &r) { g = r; ++calls; });
    t.flush();
    CHECK(calls == 1);
    CHECK(g.transportOk);
    CHECK(g.accessToken.empty());
    CHECK(g.error == "authorization_pending");
}
TEST_CASE("oauthRefreshGrant") {
    FakeHttp t; t.queue(Catalog::kTokenUrl, "{\"access_token\":\"AT2\"}");
    TokenGrant g; int calls = 0;
    oauthRefresh(t, "RTOLD", newJob(), [&](const TokenGrant &r) { g = r; ++calls; });
    t.flush();
    CHECK(calls == 1);
    CHECK(g.transportOk);
    CHECK(g.accessToken == "AT2");
    CHECK(g.expiresInSecs == 0);
    CHECK(t.sentForm[0].first == Catalog::kTokenUrl);
    CHECK(*formValue(t.sentForm[0].second, "refresh_token") == "RTOLD");
    CHECK(*formValue(t.sentForm[0].second, "grant_type") == "refresh_token");
}

// ---- cancellation + routing ----
TEST_CASE("canceledJobDropsDelivery") {
    FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEwhat_to_watch";
    JobToken job = newJob();
    int calls = 0;
    fetchVideoList(t, spec, job, [&](const Result<Page<VideoItem>> &) { ++calls; });
    job->canceled = true;
    t.flush();
    CHECK(calls == 0);
}
TEST_CASE("routing") {
    auto browseClient = [](const char *id, bool signedIn) {
        FakeHttp http;
        if (signedIn) http.session().bearer = "tok";
        VideoListSpec s; s.kind = VideoListSpec::Browse; s.browseId = id;
        fetchVideoList(http, s, newJob(), [](const Result<Page<VideoItem>> &) {});
        return http.lastClientFor("browse");
    };
    CHECK(browseClient("FEwhat_to_watch", true) == (int)ClientId::TVHTML5);
    CHECK(browseClient("FEwhat_to_watch", false) == (int)ClientId::WEB);
    CHECK(browseClient("FEtrending", true) == (int)ClientId::WEB);
    CHECK(browseClient("VLLL", true) == (int)ClientId::TVHTML5);
    CHECK(browseClient("VLWL", true) == (int)ClientId::TVHTML5);    // requires auth: always TV
    CHECK(browseClient("VLWL", false) == -1);                       // ...and signed out, nothing is sent (spec S4)
    FakeHttp authed; authed.session().bearer = "tok";
    fetchWatch(authed, "vid", newJob(), [](const Result<Video> &) {});
    CHECK(authed.lastClientFor("next") == (int)ClientId::TVHTML5);
    FakeHttp anon;
    fetchWatch(anon, "vid", newJob(), [](const Result<Video> &) {});
    CHECK(anon.lastClientFor("next") == (int)ClientId::WEB);
}

// ---- RYD ----
TEST_CASE("dislikes_parses_count") {
    FakeHttp http; http.setGetBody("{\"id\":\"v\",\"likes\":9,\"dislikes\":42}");
    Cap<Votes> c;
    fetchDislikes(http, "v", newJob(), c.fn());
    http.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.dislikes.has_value());
    CHECK(*c.r.value.dislikes == 42);
    CHECK(*c.r.value.likes == 9);
    CHECK(has(http.lastGetUrl(), "returnyoutubedislikeapi.com/votes?videoId=v"));
}
TEST_CASE("dislikesAbsentAreUnknown") {
    FakeHttp http; http.setGetBody("{\"id\":\"v\"}");
    Cap<Votes> c;
    fetchDislikes(http, "v", newJob(), c.fn());
    http.flush();
    REQUIRE(c.r.ok());
    CHECK_FALSE(c.r.value.dislikes.has_value());
    CHECK_FALSE(c.r.value.likes.has_value());
}
TEST_CASE("dislikesVideoIdIsEncoded") {
    FakeHttp http; http.setGetBody("{}");
    Cap<Votes> c;
    fetchDislikes(http, "a&b=c", newJob(), c.fn());
    http.flush();
    CHECK(has(http.lastGetUrl(), "videoId=a%26b%3Dc"));
}

// Review focus: fmt=json3 is set whatever shape the server's URL has; the rest stays byte-exact.
TEST_CASE("json3Url") {
    CHECK(json3Url("https://y/api/timedtext?v=a&lang=en") == "https://y/api/timedtext?v=a&lang=en&fmt=json3");
    CHECK(json3Url("https://y/api/timedtext?v=a&fmt=srv3&lang=en") == "https://y/api/timedtext?v=a&fmt=json3&lang=en");
    CHECK(json3Url("https://y/api/timedtext?fmt=vtt") == "https://y/api/timedtext?fmt=json3");
    CHECK(json3Url("https://y/api/timedtext") == "https://y/api/timedtext?fmt=json3");
    CHECK(json3Url("https://y/t?v=a&xfmt=1&sig=%2F") == "https://y/t?v=a&xfmt=1&sig=%2F&fmt=json3");
}
TEST_CASE("captions fetch json3 cues") {
    FakeHttp t;
    const std::string url = "https://www.youtube.com/api/timedtext?v=dQw4w9WgXcQ&lang=en";
    t.queue(url + "&fmt=json3", loadFixtureRaw("captions_manual.json3"));
    CaptionTrack track; track.url = url; track.language = "en";
    Cap<std::vector<Cue>> c;
    fetchCaptions(t, track, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.size() == 3);
    REQUIRE(t.fetched.size() == 1);
    CHECK(t.fetched[0].url == url + "&fmt=json3");
}
TEST_CASE("captions: an empty 200 is no cues, a 429 is an error") {
    FakeHttp t;
    t.queue("https://y/t?fmt=json3", "");
    t.queue("https://y/u?fmt=json3", "<html>Sorry</html>", 429);
    CaptionTrack a; a.url = "https://y/t";
    CaptionTrack b; b.url = "https://y/u";
    Cap<std::vector<Cue>> ca, cb;
    fetchCaptions(t, a, newJob(), ca.fn());
    fetchCaptions(t, b, newJob(), cb.fn());
    t.flush();
    REQUIRE(ca.r.ok());
    CHECK(ca.r.value.empty());
    REQUIRE_FALSE(cb.r.ok());
    CHECK(cb.r.error->httpStatus == 429);
}

TEST_CASE("sign-in QR from a data URL") {
    FakeHttp t; t.queue("mdx/handoff", loadFixtureRaw("mdx_handoff.json"));
    Cap<std::string> c;
    fetchSignInQr(t, "U-1", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
    CHECK(t.lastClientFor("mdx/handoff") == (int) ClientId::TVHTML5);
    CHECK(has(t.sent[0], "\"userCode\":\"U-1\""));
}
TEST_CASE("sign-in QR from an image URL") {
    FakeHttp t;
    t.queue("mdx/handoff", "{\"rapidQrRenderer\":{\"qrCodeRenderer\":{\"qrCodeImage\":{\"thumbnails\":"
                           "[{\"url\":\"https://i.ytimg.com/qr.png\"}]}}}}");
    t.queue("https://i.ytimg.com/qr.png", std::string("\x89PNG\r\n\x1a\nrest", 12));
    Cap<std::string> c;
    fetchSignInQr(t, "U", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.size() == 12);
}
// Review focus: a broken answer must never hand garbage to an image decoder.
TEST_CASE("sign-in QR: garbage is refused") {
    FakeHttp t;
    t.queue("mdx/handoff", "{\"rapidQrRenderer\":{\"qrCodeRenderer\":{\"qrCodeImage\":{\"thumbnails\":"
                           "[{\"url\":\"data:image/png;base64,not*base64\"}]}}}}");
    t.queue("mdx/handoff", "{\"rapidQrRenderer\":{\"qrCodeRenderer\":{\"qrCodeImage\":{\"thumbnails\":"
                           "[{\"url\":\"data:image/png;base64,aGVsbG8=\"}]}}}}");   // valid base64, not a PNG
    t.queue("mdx/handoff", "{}");
    Cap<std::string> a, b, c;
    fetchSignInQr(t, "U", newJob(), a.fn());
    fetchSignInQr(t, "U", newJob(), b.fn());
    fetchSignInQr(t, "U", newJob(), c.fn());
    t.flush();
    CHECK_FALSE(a.r.ok());
    CHECK(a.r.value.empty());
    CHECK_FALSE(b.r.ok());
    CHECK(b.r.value.empty());
    CHECK_FALSE(c.r.ok());
}
TEST_CASE("cursorAccessRoundTrip") {
    const Cursor<VideoItem> c = detail::CursorAccess::make<VideoItem>("browse", ClientId::TVHTML5, "TOK");
    CHECK(bool(c));
    CHECK(detail::CursorAccess::endpoint(c) == "browse");
    CHECK(detail::CursorAccess::client(c) == ClientId::TVHTML5);
    CHECK(detail::CursorAccess::token(c) == "TOK");
    CHECK_FALSE(detail::CursorAccess::make<VideoItem>("browse", ClientId::WEB, ""));   // no token = no next page
}
// ---- links into ids ----
TEST_CASE("videoIdOf and playlistIdOf") {
    CHECK(videoIdOf("https://youtu.be/dQw4w9WgXcQ?si=x") == std::optional<std::string>("dQw4w9WgXcQ"));
    CHECK(videoIdOf("https://www.youtube.com/shorts/aqz-KE-bpKQ") == std::optional<std::string>("aqz-KE-bpKQ"));
    CHECK_FALSE(videoIdOf("@NASA").has_value());
    CHECK(playlistIdOf("https://www.youtube.com/playlist?list=PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI")
          == std::optional<std::string>("PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(playlistIdOf("WL") == std::optional<std::string>("WL"));
    CHECK_FALSE(playlistIdOf("dQw4w9WgXcQ").has_value());
    const Error e = badLink("xyz", "video");
    CHECK(e.code == ErrorCode::InvalidArgument);
    CHECK(e.message == "not a video link: xyz");
}
TEST_CASE("a channel id needs no request; a handle is resolved") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabcabcabcabcabcabcabca\"}}}");
    Cap<std::string> a, b;
    fetchChannelId(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), a.fn());
    fetchChannelId(t, "https://www.youtube.com/@NASA/videos", newJob(), b.fn());
    t.flush();
    REQUIRE(a.r.ok());
    CHECK(a.r.value == "UCuAXFkgsw1L7xaCfnd5JJOw");
    REQUIRE(b.r.ok());
    CHECK(b.r.value == "UCabcabcabcabcabcabcabca");
    REQUIRE(t.sent.size() == 1);
    CHECK(has(t.sent[0], "\"url\":\"https://www.youtube.com/@NASA\""));
}
// Review focus: a link of the wrong kind fails without a request.
TEST_CASE("a video link is not a channel") {
    FakeHttp t;
    Cap<std::string> c;
    fetchChannelId(t, "https://youtu.be/dQw4w9WgXcQ", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::InvalidArgument);
    CHECK(t.sent.empty());
}
TEST_CASE("a resolve that names no channel is Unavailable") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"FEwhat_to_watch\"}}}");
    Cap<std::string> c;
    fetchChannelId(t, "@nobody", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Unavailable);
}
TEST_CASE("signed in, a channel also carries the viewer's subscribe state") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCuAXFkgsw1L7xaCfnd5JJOw\"}}}");
    t.queue("browse", "{\"subscribeButton\":{\"subscribeButtonRenderer\":{\"subscribed\":true}}}");
    Cap<Channel> c;
    fetchChannel(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.id == "UCuAXFkgsw1L7xaCfnd5JJOw");
    CHECK(t.sentClients == std::vector<int>{(int) ClientId::WEB, (int) ClientId::TVHTML5});
    CHECK(c.r.value.subscribed);
}
TEST_CASE("anonymous, a channel is one request") {
    FakeHttp t;
    t.queue("browse", "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCuAXFkgsw1L7xaCfnd5JJOw\"}}}");
    Cap<Channel> c;
    fetchChannel(t, "https://www.youtube.com/channel/UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent.size() == 1);
}
TEST_CASE("channel tabs browse the channel with the tab params") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabcabcabcabcabcabcabca\"}}}");
    t.queue("browse", loadFixtureRaw("browse_feed.json"));
    t.queue("browse", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"X\"}}}]}");
    Cap<Page<VideoItem>> v;
    Cap<Page<Playlist>> p;
    fetchChannelVideos(t, "@NASA", newJob(), v.fn());
    t.flush();
    fetchChannelPlaylists(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), p.fn());
    t.flush();
    REQUIRE(v.r.ok());
    CHECK(has(t.sent[1], "\"browseId\":\"UCabcabcabcabcabcabcabca\""));
    CHECK(has(t.sent[1], (std::string("\"params\":\"") + Catalog::kChannelVideosTab + "\"").c_str()));
    REQUIRE(p.r.ok());
    CHECK(p.r.value.items.size() == 1);
    CHECK(has(t.sent[2], (std::string("\"params\":\"") + Catalog::kChannelPlaylistsTab + "\"").c_str()));
}
TEST_CASE("subscribe by link resolves the channel first") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabcabcabcabcabcabcabca\"}}}");
    t.queue("subscription/subscribe", "{}");
    Cap<Done> c;
    channelAction(t, "@NASA", Subscribe, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(has(t.sent[1], "\"channelIds\":[\"UCabcabcabcabcabcabcabca\"]"));
    CHECK(t.lastClientFor("subscription/subscribe") == (int) ClientId::TVHTML5);
}
TEST_CASE("posting a comment finds its createCommentParams on the comments page") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", "{\"x\":{\"createCommentParams\":\"CPARAMS\"}}");
    t.queue("comment/create_comment", "{}");
    Cap<Done> c;
    postComment(t, "aaa11111111", "hi", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(t.sent.size() == 3);
    CHECK(has(t.sent[2], "\"createCommentParams\":\"CPARAMS\""));
    CHECK(has(t.sent[2], "\"commentText\":\"hi\""));
    CHECK(t.lastClientFor("comment/create_comment") == (int) ClientId::TVHTML5);
}
TEST_CASE("posting where comments are off fails without a create request") {
    FakeHttp t; t.queue("next", "{}");
    Cap<Done> c;
    postComment(t, "aaa11111111", "hi", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Unavailable);
    CHECK(t.sent.size() == 1);
}
TEST_CASE("playlist edits add and remove by video id") {
    FakeHttp t;
    t.queue("browse/edit_playlist", "{}");
    t.queue("browse/edit_playlist", "{}");
    Cap<Done> a, r;
    editPlaylist(t, "WL", AddVideo, "vid42", newJob(), a.fn());
    editPlaylist(t, "PLabc", RemoveVideo, "vid42", newJob(), r.fn());
    t.flush();
    CHECK(a.r.ok());
    CHECK(r.r.ok());
    CHECK(has(t.sent[0], "\"addedVideoId\":\"vid42\""));
    CHECK(has(t.sent[1], "\"removedVideoId\":\"vid42\""));
    CHECK_FALSE(has(t.sent[1], "setVideoId"));
    CHECK(t.lastClientFor("browse/edit_playlist") == (int) ClientId::TVHTML5);
}
// A page served signed in (TVHTML5) used to be continued after sign-out: the cursor was
// posted to TV without a bearer instead of failing like page 1 would (spec S4).
TEST_CASE("a signed-in page continued after sign-out fails with AuthRequired and sends nothing") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.browseId = "FEsubscriptions";
    Cap<Page<VideoItem>> first, second;
    fetchVideoList(t, spec, newJob(), first.fn());
    t.flush();
    REQUIRE(first.r.value.next);
    t.session().bearer.clear();                       // signed out
    fetchMore(t, first.r.value.next, newJob(), second.fn());
    t.flush();
    REQUIRE(second.calls == 1);
    REQUIRE_FALSE(second.r.ok());
    CHECK(second.r.error->code == ErrorCode::AuthRequired);
    CHECK(t.sent.size() == 1);
}
// A 401 on the subscribe-state request used to be swallowed (header delivered, subscribed
// false), so the expired bearer was never refreshed: the 401 must reach the Client's retry.
TEST_CASE("signed in, a 401 on the subscribe state fails the channel with 401") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCuAXFkgsw1L7xaCfnd5JJOw\"}}}");
    t.queue("browse", "{\"error\":{}}", 401);
    Cap<Channel> c;
    fetchChannel(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->httpStatus == 401);
}
TEST_CASE("signed in, another subscribe-state failure still delivers the header") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCuAXFkgsw1L7xaCfnd5JJOw\"}}}");
    t.queue("browse", "", 0);                         // network down
    Cap<Channel> c;
    fetchChannel(t, "UCuAXFkgsw1L7xaCfnd5JJOw", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(c.r.value.id == "UCuAXFkgsw1L7xaCfnd5JJOw");
}
// A channel's own tabs list items without a channel row; they used to arrive with an empty
// channelId/channelName on every page.
TEST_CASE("a channel's Videos tab gives its videos the channel, on every page") {
    FakeHttp t;
    t.queue("browse", loadFixtureRaw("channel_videos_lockup.json"));
    t.queue("browse",
            "{\"onResponseReceivedActions\":[{\"appendContinuationItemsAction\":{\"continuationItems\":["
            "{\"richItemRenderer\":{\"content\":{\"lockupViewModel\":{\"contentId\":\"ErAqN6gXqZQ\","
            "\"contentType\":\"LOCKUP_CONTENT_TYPE_VIDEO\",\"metadata\":{\"lockupMetadataViewModel\":{"
            "\"title\":{\"content\":\"B\"}}}}}}}]}}]}");
    Cap<Page<VideoItem>> first, second;
    fetchChannelVideos(t, "UCLA_DiR1FfKNvjuUpBHmylQ", newJob(), first.fn());
    t.flush();
    REQUIRE(first.r.ok());
    REQUIRE(first.r.value.items.size() == 1);
    CHECK(first.r.value.items[0].channel.id == "UCLA_DiR1FfKNvjuUpBHmylQ");
    CHECK(first.r.value.items[0].channel.name == "NASA");
    REQUIRE(first.r.value.next);
    fetchMore(t, first.r.value.next, newJob(), second.fn());
    t.flush();
    REQUIRE(second.r.ok());
    REQUIRE(second.r.value.items.size() == 1);
    CHECK(second.r.value.items[0].id == "ErAqN6gXqZQ");
    CHECK(second.r.value.items[0].channel.id == "UCLA_DiR1FfKNvjuUpBHmylQ");
    CHECK(second.r.value.items[0].channel.name == "NASA");
}
TEST_CASE("a channel's Playlists tab gives its playlists the channel") {
    FakeHttp t;
    t.queue("browse", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"X\"}}}],"
                      "\"metadata\":{\"channelMetadataRenderer\":{\"title\":\"NASA\",\"externalId\":\"UCLA_DiR1FfKNvjuUpBHmylQ\"}}}");
    Cap<Page<Playlist>> c;
    fetchChannelPlaylists(t, "UCLA_DiR1FfKNvjuUpBHmylQ", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 1);
    CHECK(c.r.value.items[0].owner.id == "UCLA_DiR1FfKNvjuUpBHmylQ");
    CHECK(c.r.value.items[0].owner.name == "NASA");
}
// A watch page's related list is a Page: its cursor continues on /next with the client
// that served the page.
TEST_CASE("the related list of a watch page continues through next") {
    FakeHttp t;
    t.queue("next",
            "{\"contents\":{\"twoColumnWatchNextResults\":{\"secondaryResults\":{\"secondaryResults\":{\"results\":["
            "{\"lockupViewModel\":{\"contentId\":\"rel1\",\"contentType\":\"LOCKUP_CONTENT_TYPE_VIDEO\"}},"
            "{\"continuationItemRenderer\":{\"continuationEndpoint\":{\"continuationCommand\":{\"token\":\"RELATED\"}}}}]}}}}}");
    t.queue("next", loadFixtureRaw("browse_feed.json"));
    Cap<Video> page;
    Cap<Page<VideoItem>> more;
    fetchWatch(t, "dQw4w9WgXcQ", newJob(), page.fn());
    t.flush();
    REQUIRE(page.r.ok());
    REQUIRE(page.r.value.related.next);
    fetchMore(t, page.r.value.related.next, newJob(), more.fn());
    t.flush();
    REQUIRE(more.r.ok());
    CHECK(t.sent[1] == "{\"continuation\":\"RELATED\"}");
    CHECK(t.sentClients[1] == (int) ClientId::WEB);
    CHECK(more.r.value.items.size() == 2);
}
TEST_CASE("a playlist: its header and first page of entries, continued by more") {
    FakeHttp t;
    t.queue("browse", loadFixtureRaw("playlist_page.json"));
    Cap<Playlist> c;
    fetchPlaylist(t, "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent[0] == "{\"browseId\":\"VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI\"}");
    CHECK(c.r.value.id == "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI");
    CHECK(c.r.value.title == "Popular Music Videos");
    CHECK(c.r.value.owner.name == "Music");
    REQUIRE(c.r.value.entries.items.size() == 1);
    CHECK(c.r.value.entries.items[0].id == "fOT0BUpITw8");
    CHECK(tokenOf(c.r.value.entries.next) == "PLNEXT");
}
TEST_CASE("Watch Later signed out is AuthRequired, without a request") {
    FakeHttp t;
    Cap<Playlist> c;
    fetchPlaylist(t, "WL", newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::AuthRequired);
    CHECK(t.sent.empty());
}
