// core::chains — the request layer on std types, driven over FakeHttp (port of
// MeeTube's tst_meetube_chains: same fixtures, same sent-body and routing asserts;
// Outcome<T> -> yt::Result<T> with error codes).
#include <doctest/doctest.h>
#include "core/chains.h"
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
struct PlayerCap {
    int calls = 0;
    PlayerInfo r;
    std::function<void(const PlayerInfo &)> fn() { return [this](const PlayerInfo &x) { ++calls; r = x; }; }
};
}

// ---- fetchPlayer: streams side ----
TEST_CASE("streamsFromIos") {
    FakeHttp t; t.queue("player", loadFixtureRaw("player_ios.json"));
    PlayerCap c;
    fetchPlayer(t, "aaa11111111", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.streams.ok());
    CHECK(c.r.streams.value.size() >= 3);
    CHECK(c.r.streams.value[0].id == "hls");
}
TEST_CASE("streamsFromAndroidFallback") {
    FakeHttp t;
    t.queue("player", "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"formats\":[]}}");
    t.queue("player", loadFixtureRaw("player_ios.json"));
    PlayerCap c;
    fetchPlayer(t, "vid", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.streams.ok());
    CHECK(c.r.streams.value[0].id == "hls");
    CHECK(t.sent.size() == 2);
}
TEST_CASE("streamsFromAuthedTvFirst") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("player", loadFixtureRaw("player_ios.json"));
    PlayerCap c;
    fetchPlayer(t, "vid", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK(c.r.streams.ok());
    CHECK(t.sent.size() == 1);
    CHECK(t.lastClientFor("player") == (int)ClientId::TVHTML5);
    CHECK(has(t.sent[0], "contentPlaybackContext"));     // TV needs the playback context
}
TEST_CASE("bothClientsFail") {
    FakeHttp t;
    const std::string np = "{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\",\"reason\":\"Sign in\"}}";
    t.queue("player", np); t.queue("player", np);
    PlayerCap c;
    fetchPlayer(t, "vid", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE_FALSE(c.r.streams.ok());
    CHECK((has(c.r.streams.error->message, "LOGIN_REQUIRED") || has(c.r.streams.error->message, "Sign in")));
    CHECK(c.r.streams.error->code == ErrorCode::Unavailable);
    CHECK(t.sent.size() == 2);
}
TEST_CASE("streamsAllCiphered") {
    FakeHttp t;
    const std::string ciph = "{\"playabilityStatus\":{\"status\":\"OK\"},"
                             "\"streamingData\":{\"formats\":[{\"itag\":18,\"signatureCipher\":\"s=xx\"}]}}";
    t.queue("player", ciph); t.queue("player", ciph);
    PlayerCap c;
    fetchPlayer(t, "vid", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE_FALSE(c.r.streams.ok());
    CHECK(has(c.r.streams.error->message, "decipher"));
    CHECK(c.r.streams.error->code == ErrorCode::Unsupported);
    CHECK(t.sent.size() == 2);
}
TEST_CASE("captionsSurviveStreamFailure") {
    FakeHttp t;
    const std::string capOnly =
        "{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\",\"reason\":\"Sign in\"},"
        "\"captions\":{\"playerCaptionsTracklistRenderer\":{\"captionTracks\":["
        "{\"baseUrl\":\"https://x/cap\",\"languageCode\":\"en\","
        "\"name\":{\"simpleText\":\"English\"}}]}}}";
    t.queue("player", capOnly); t.queue("player", capOnly);
    PlayerCap c;
    fetchPlayer(t, "vid", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK_FALSE(c.r.streams.ok());
    REQUIRE(c.r.captions.ok());
    REQUIRE(c.r.captions.value.size() == 1);
    CHECK(c.r.captions.value[0].language == "en");
}
TEST_CASE("captionsFailWhenTransportDown") {
    FakeHttp t;
    PlayerCap c;
    fetchPlayer(t, "vid", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK_FALSE(c.r.streams.ok());
    CHECK_FALSE(c.r.captions.ok());
    CHECK(c.r.streams.error->code == ErrorCode::Transport);
    CHECK(t.sent.size() == 2);
}

// ---- fetchVideoList ----
TEST_CASE("videoBrowseFeed") {
    FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEwhat_to_watch";
    Cap<VideoPage> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 2);
    CHECK(c.r.value.items[0].id == "ccc33333333");
    CHECK(c.r.value.next == "FEEDNEXT");
    CHECK(has(t.sent[0], "\"browseId\":\"FEwhat_to_watch\""));
}
TEST_CASE("videoSearchVideos") {
    FakeHttp t; t.queue("search", loadFixtureRaw("search_videos.json"));
    VideoListSpec spec; spec.kind = VideoListSpec::Search; spec.query = "cats"; spec.order = "date";
    Cap<VideoPage> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK(c.r.ok());
    CHECK(has(t.sent[0], "\"query\":\"cats\""));
    CHECK(has(t.sent[0], "\"params\":\"CAISAhAB\""));
}
TEST_CASE("videoBrowseContinuation") {
    FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEwhat_to_watch"; spec.page = "SOMETOKEN";
    Cap<VideoPage> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK(has(t.sent[0], "\"continuation\":"));
    CHECK_FALSE(has(t.sent[0], "\"browseId\":"));
}
TEST_CASE("videoAuthedFeedBrowses") {
    FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
    VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEsubscriptions";
    Cap<VideoPage> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK(c.r.ok());
    CHECK(has(t.sent[0], "\"browseId\":\"FEsubscriptions\""));
}
TEST_CASE("videoListTransportErrorKeepsCode") {
    FakeHttp t; t.queue("browse", "{\"error\":{}}", 401);
    VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEsubscriptions";
    Cap<VideoPage> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    REQUIRE_FALSE(c.r.ok());
    CHECK(c.r.error->code == ErrorCode::Api);
    CHECK(c.r.error->httpStatus == 401);
}

// ---- fetchWatch ----
TEST_CASE("videoLoadWatch") {
    FakeHttp t; t.queue("next", loadFixtureRaw("watch_next.json"));
    Cap<WatchResult> c;
    fetchWatch(t, "vid42", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.primary.id == "vid42");
    CHECK(c.r.value.primary.description == "Hello description");
    CHECK(c.r.value.related.size() >= 1);
    CHECK(has(t.sent[0], "\"videoId\":\"vid42\""));
}

// ---- fetchComments ----
TEST_CASE("commentsTwoStep") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    Cap<CommentPage> c;
    fetchComments(t, "aaa11111111", "", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.items.size() == 2);
    CHECK(c.r.value.next == "MORE_COMMENTS");
    CHECK(has(t.sent[0], "\"videoId\":\"aaa11111111\""));
    CHECK(has(t.sent[1], "\"continuation\":\"COMMENTS_TOKEN\""));
}
TEST_CASE("commentsDisabled") {
    FakeHttp t; t.queue("next", "{}");
    Cap<CommentPage> c;
    fetchComments(t, "aaa11111111", "", newJob(), c.fn());
    t.flush();
    CHECK(t.sent.size() == 1);
    CHECK(c.calls == 1);
    CHECK(c.r.ok());
    CHECK(c.r.value.items.empty());
    CHECK(c.r.value.disabled);
}
TEST_CASE("commentsDirectContinuation") {
    FakeHttp t; t.queue("next", loadFixtureRaw("comments_page.json"));
    Cap<CommentPage> c;
    fetchComments(t, "", "EXISTING_TOKEN", newJob(), c.fn());
    t.flush();
    CHECK(t.sent.size() == 1);
    CHECK(has(t.sent[0], "\"continuation\":\"EXISTING_TOKEN\""));
    CHECK(c.calls == 1);
    CHECK(c.r.ok());
}
TEST_CASE("commentsCanceledBetweenSteps") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", loadFixtureRaw("comments_page.json"));
    JobToken job = newJob();
    Cap<CommentPage> c;
    fetchComments(t, "aaa11111111", "", job, c.fn());
    job->canceled = true;
    t.flush();
    CHECK(c.calls == 0);
    CHECK(t.sent.size() == 1);
}

// ---- postComment ----
TEST_CASE("postCommentSucceeds") {
    FakeHttp t; t.queue("comment/create_comment", "{}");
    Cap<Done> c;
    postComment(t, "PARAMS", "hi", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK(c.r.ok());
    CHECK(has(t.sent[0], "\"createCommentParams\":\"PARAMS\""));
    CHECK(has(t.sent[0], "\"commentText\":\"hi\""));
    CHECK(t.lastClientFor("comment/create_comment") == (int)ClientId::TVHTML5);
}
TEST_CASE("postCommentTransportFails") {
    FakeHttp t;
    Cap<Done> c;
    postComment(t, "PARAMS", "hi", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK_FALSE(c.r.ok());
}

// ---- playlists / channels ----
TEST_CASE("playlistSearch") {
    FakeHttp t;
    t.queue("search", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL1\",\"title\":{\"simpleText\":\"L\"}}}]}");
    Cap<PlaylistPage> c;
    fetchPlaylistSearch(t, "foo", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
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
    Cap<ChannelPage> c;
    fetchChannelList(t, "FEchannels", "", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 2);
    CHECK(c.r.value.items[0].id == "UCsub1");
    CHECK(c.r.value.items[0].name == "Sub One");
    CHECK(has(t.sent[0], "\"browseId\":\"FEchannels\""));
    CHECK(t.lastClientFor("browse") == (int)ClientId::TVHTML5);
}
TEST_CASE("playlistBrowse") {
    FakeHttp t;
    t.queue("browse", "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"X\"}}}]}");
    Cap<PlaylistPage> c;
    fetchPlaylists(t, "UCabc", "", "", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    REQUIRE(c.r.value.items.size() == 1);
    CHECK(c.r.value.items[0].id == "PL9");
    CHECK(has(t.sent[0], "\"browseId\":\"UCabc\""));
}
TEST_CASE("userResolveThenBrowse") {
    FakeHttp t;
    t.queue("navigation/resolve_url", "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabc\"}}}");
    t.queue("browse", "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCabc\"}}}");
    Cap<Channel> c;
    fetchChannelByUrl(t, "https://youtube.com/@chan", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    REQUIRE(c.r.ok());
    CHECK(c.r.value.id == "UCabc");
    CHECK(t.sent.size() == 2);
}
TEST_CASE("userResolveFails") {
    FakeHttp t; t.queue("navigation/resolve_url", "{}");
    Cap<Channel> c;
    fetchChannelByUrl(t, "https://youtube.com/@nope", newJob(), c.fn());
    t.flush();
    CHECK(t.sent.size() == 1);
    CHECK(c.calls == 1);
    REQUIRE_FALSE(c.r.ok());
    CHECK(has(c.r.error->message, "could not resolve channel"));
    CHECK(c.r.error->code == ErrorCode::Unavailable);
}
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
    Cap<ChannelPage> c;
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
    Cap<Account> c;
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
    Cap<Account> c;
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
TEST_CASE("editPlaylistAdd") {
    FakeHttp t; t.queue("browse/edit_playlist", "{}");
    Cap<Done> c;
    editPlaylist(t, "WL", true, "vid42", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK(c.r.ok());
    CHECK(has(t.sent[0], "\"playlistId\":\"WL\""));
    CHECK(has(t.sent[0], "ACTION_ADD_VIDEO"));
    CHECK(has(t.sent[0], "\"addedVideoId\":\"vid42\""));
    CHECK_FALSE(has(t.sent[0], "setVideoId"));
    CHECK(t.lastClientFor("browse/edit_playlist") == (int)ClientId::TVHTML5);
}
TEST_CASE("editPlaylistRemove") {
    FakeHttp t; t.queue("browse/edit_playlist", "{}");
    Cap<Done> c;
    editPlaylist(t, "PLabc", false, "setVid", newJob(), c.fn());
    t.flush();
    CHECK(c.calls == 1);
    CHECK(c.r.ok());
    CHECK(has(t.sent[0], "ACTION_REMOVE_VIDEO"));
    CHECK(has(t.sent[0], "\"setVideoId\":\"setVid\""));
    CHECK_FALSE(has(t.sent[0], "addedVideoId"));
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
    fetchVideoList(t, spec, job, [&](const Result<VideoPage> &) { ++calls; });
    job->canceled = true;
    t.flush();
    CHECK(calls == 0);
}
TEST_CASE("routing") {
    auto browseClient = [](const char *id, bool signedIn) {
        FakeHttp http;
        if (signedIn) http.session().bearer = "tok";
        VideoListSpec s; s.kind = VideoListSpec::Browse; s.browseId = id;
        fetchVideoList(http, s, newJob(), [](const Result<VideoPage> &) {});
        return http.lastClientFor("browse");
    };
    CHECK(browseClient("FEwhat_to_watch", true) == (int)ClientId::TVHTML5);
    CHECK(browseClient("FEwhat_to_watch", false) == (int)ClientId::WEB);
    CHECK(browseClient("FEtrending", true) == (int)ClientId::WEB);
    CHECK(browseClient("VLLL", true) == (int)ClientId::TVHTML5);
    CHECK(browseClient("VLWL", false) == (int)ClientId::TVHTML5);   // requires auth: always TV
    FakeHttp authed; authed.session().bearer = "tok";
    fetchWatch(authed, "vid", newJob(), [](const Result<WatchResult> &) {});
    CHECK(authed.lastClientFor("next") == (int)ClientId::TVHTML5);
    FakeHttp anon;
    fetchWatch(anon, "vid", newJob(), [](const Result<WatchResult> &) {});
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
