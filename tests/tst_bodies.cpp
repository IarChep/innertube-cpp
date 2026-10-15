// InnerTube POST bodies (without the context block). Exact JSON: the transport
// splices the context in after the leading '{', so the shape is load-bearing.
#include <doctest/doctest.h>
#include "innertube/bodies.h"
using namespace yt;
TEST_CASE("browse") {
    CHECK(bodies::browse("FEx", "", "") == "{\"browseId\":\"FEx\"}");
    CHECK(bodies::browse("UCx", "EgZ2aWRlb3M%3D", "") == "{\"browseId\":\"UCx\",\"params\":\"EgZ2aWRlb3M%3D\"}");
    CHECK(bodies::browse("UCx", "P", "CONT") == "{\"continuation\":\"CONT\"}");
}
TEST_CASE("search keeps UTF-8 raw") {
    CHECK(bodies::search("кот", "") == "{\"query\":\"кот\"}");
    CHECK(bodies::search("a\"b", "CAISAhAB") == "{\"query\":\"a\\\"b\",\"params\":\"CAISAhAB\"}");
}
TEST_CASE("next / player / resolve") {
    CHECK(bodies::nextVideo("vid") == "{\"videoId\":\"vid\"}");
    CHECK(bodies::nextContinuation("T") == "{\"continuation\":\"T\"}");
    CHECK(bodies::player("vid") == "{\"videoId\":\"vid\",\"contentCheckOk\":true,\"racyCheckOk\":true}");
    CHECK(bodies::player("vid", {true, "CPN", 0}) ==
          "{\"videoId\":\"vid\",\"contentCheckOk\":true,\"racyCheckOk\":true,\"cpn\":\"CPN\","
          "\"playbackContext\":{\"contentPlaybackContext\":{\"html5Preference\":\"HTML5_PREF_WANTS\"}}}");
    CHECK(bodies::player("vid", {true, "CPN", 20737}) ==
          "{\"videoId\":\"vid\",\"contentCheckOk\":true,\"racyCheckOk\":true,\"cpn\":\"CPN\","
          "\"playbackContext\":{\"contentPlaybackContext\":{\"html5Preference\":\"HTML5_PREF_WANTS\","
          "\"signatureTimestamp\":20737}}}");
    CHECK(bodies::resolveUrl("https://www.youtube.com/@x") == "{\"url\":\"https://www.youtube.com/@x\"}");
}
TEST_CASE("account / actions") {
    CHECK(bodies::accountsList() == "{\"accountReadMask\":{\"returnOwner\":true}}");
    CHECK(bodies::subscribeChannels("UC1") == "{\"channelIds\":[\"UC1\"]}");
    CHECK(bodies::likeTarget("v") == "{\"target\":{\"videoId\":\"v\"}}");
    CHECK(bodies::createComment("P", "hi") == "{\"createCommentParams\":\"P\",\"commentText\":\"hi\"}");
    CHECK(bodies::editPlaylist("WL", true, "v") ==
          "{\"playlistId\":\"WL\",\"actions\":[{\"action\":\"ACTION_ADD_VIDEO\",\"addedVideoId\":\"v\"}]}");
    CHECK(bodies::editPlaylist("PL1", false, "S") ==
          "{\"playlistId\":\"PL1\",\"actions\":[{\"action\":\"ACTION_REMOVE_VIDEO\",\"setVideoId\":\"S\"}]}");
}

TEST_CASE("mdx handoff (QR)") {
    CHECK(bodies::mdxHandoff("ABC-DEF-GHI") ==
          "{\"handoffQrParams\":{\"rapidQrParams\":{\"qrPresetStyle\":"
          "\"HANDOFF_QR_LIMITED_PRESET_STYLE_MODERN_BIG_DOTS_INVERT_WITH_YT_LOGO\","
          "\"userCode\":\"ABC-DEF-GHI\",\"rapidQrFeature\":\"RAPID_QR_FEATURE_DEFAULT\"}}}");
}
