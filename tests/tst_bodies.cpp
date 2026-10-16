// InnerTube POST bodies (without the context block). Exact JSON: the transport
// splices the context in after the leading '{', so the shape is load-bearing.
#include <doctest/doctest.h>
#include "innertube/bodies.h"
using namespace yt;
TEST_CASE("browse") {
    CHECK(bodies::browse("FEx", "") == "{\"browseId\":\"FEx\"}");
    CHECK(bodies::browse("UCx", "EgZ2aWRlb3M%3D") == "{\"browseId\":\"UCx\",\"params\":\"EgZ2aWRlb3M%3D\"}");
}
TEST_CASE("search keeps UTF-8 raw") {
    CHECK(bodies::search("кот", "") == "{\"query\":\"кот\"}");
    CHECK(bodies::search("a\"b", "CAISAhAB") == "{\"query\":\"a\\\"b\",\"params\":\"CAISAhAB\"}");
}
TEST_CASE("next / player / resolve") {
    CHECK(bodies::nextVideo("vid") == "{\"videoId\":\"vid\"}");
    CHECK(bodies::continuation("T") == "{\"continuation\":\"T\"}");
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
}

TEST_CASE("mdx handoff (QR)") {
    CHECK(bodies::mdxHandoff("ABC-DEF-GHI") ==
          "{\"handoffQrParams\":{\"rapidQrParams\":{\"qrPresetStyle\":"
          "\"HANDOFF_QR_LIMITED_PRESET_STYLE_MODERN_BIG_DOTS_INVERT_WITH_YT_LOGO\","
          "\"userCode\":\"ABC-DEF-GHI\",\"rapidQrFeature\":\"RAPID_QR_FEATURE_DEFAULT\"}}}");
}
TEST_CASE("continuation body") {
    CHECK(bodies::continuation("TOK") == "{\"continuation\":\"TOK\"}");
    CHECK(bodies::browse("UCx", "EgZ2aWRlb3PyBgQKAjoA") == "{\"browseId\":\"UCx\",\"params\":\"EgZ2aWRlb3PyBgQKAjoA\"}");
    CHECK(bodies::browse("FEwhat_to_watch", "") == "{\"browseId\":\"FEwhat_to_watch\"}");
}
TEST_CASE("playlist edits") {
    CHECK(bodies::playlistAddVideo("WL", "vid42") ==
          "{\"playlistId\":\"WL\",\"actions\":[{\"action\":\"ACTION_ADD_VIDEO\",\"addedVideoId\":\"vid42\"}]}");
    CHECK(bodies::playlistRemoveVideo("PLabc", "vid42") ==
          "{\"playlistId\":\"PLabc\",\"actions\":[{\"action\":\"ACTION_REMOVE_VIDEO_BY_VIDEO_ID\",\"removedVideoId\":\"vid42\"}]}");
}
// Glaze wrote a control byte raw — invalid JSON that YouTube refuses — so a comment or a
// query carrying one (pasted text) failed to send. It must go out escaped.
TEST_CASE("control bytes are escaped") {
    CHECK(bodies::createComment("P", "a\x01" "b") == "{\"createCommentParams\":\"P\",\"commentText\":\"a\\u0001b\"}");
    CHECK(bodies::search("x\ty\x1f", "") == "{\"query\":\"x\\ty\\u001f\"}");
}
