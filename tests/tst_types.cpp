// Compiled at -std=c++17: the public headers must not need anything newer.
#include <doctest/doctest.h>
#include <yt/yt.h>
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
TEST_CASE("Page and Cursor defaults") {
    yt::Page<yt::Video> p;
    CHECK(p.items.empty());
    CHECK_FALSE(p.next);                              // no next page
    CHECK_FALSE(p.disabled);
    CHECK(yt::SearchOptions{}.sort == yt::SearchSort::Relevance);
    yt::Rating r = yt::Rating::None;
    CHECK(r != yt::Rating::Like);
    CHECK(yt::ErrorCode::InvalidArgument != yt::ErrorCode::Canceled);
}
TEST_CASE("the umbrella header is enough") {
    std::optional<yt::Link> l = yt::Link::parse("dQw4w9WgXcQ");
    CHECK(l.has_value());
    yt::Page<yt::Video> p;
    CHECK_FALSE(p.next);
    yt::ClientOptions o;
    CHECK(o.ownThread);
}

// ---- choose / captionFor / cueAt: pure selection over plain structs ----
namespace {
yt::Format video(int itag, int w, int h, const char *codecs, int64_t bitrate = 1000, int fps = 30)
{
    yt::Format f;
    f.itag = itag; f.url = "https://v/" + std::to_string(itag);
    f.width = w; f.height = h; f.fps = fps; f.bitrate = bitrate; f.codecs = codecs;
    f.mimeType = "video/mp4"; f.hasVideo = true;
    return f;
}
yt::Format audio(int itag, const char *codecs, int64_t bitrate, const char *lang = "", bool isDefault = true,
                 bool autoDubbed = false)
{
    yt::Format f;
    f.itag = itag; f.url = "https://a/" + std::to_string(itag);
    f.codecs = codecs; f.bitrate = bitrate; f.mimeType = "audio/mp4"; f.hasAudio = true;
    if (*lang) {
        yt::AudioTrack t;
        t.id = std::string(lang) + ".x"; t.language = lang; t.isDefault = isDefault; t.autoDubbed = autoDubbed;
        f.audio = t;
    }
    return f;
}
yt::Format muxed(int itag, int w, int h)
{
    yt::Format f = video(itag, w, h, "avc1.42001E, mp4a.40.2");
    f.mimeType = "video/mp4"; f.hasAudio = true;
    return f;
}
// VISIONOS: adaptive + HLS; ANDROID_VR: the progressive itag 18.
yt::Player twoSources()
{
    yt::Player p;
    yt::Source vision;
    vision.client = yt::ClientId::VISIONOS; vision.userAgent = "UA-V"; vision.hlsUrl = "https://hls";
    vision.formats = {video(137, 1920, 1080, "avc1.640028", 4000), video(136, 1280, 720, "avc1.4d401f", 2000),
                      video(247, 1280, 720, "vp09.00.31.08", 1500), video(135, 854, 480, "avc1.4d401e", 900),
                      audio(140, "mp4a.40.2", 128000), audio(251, "opus", 160000)};
    yt::Source vr;
    vr.client = yt::ClientId::ANDROID_VR; vr.userAgent = "UA-VR";
    vr.formats = {muxed(18, 640, 360)};
    p.sources = {vision, vr};
    return p;
}
}

TEST_CASE("choose: the best separate pair within the preference, from one source") {
    const yt::Player p = twoSources();
    yt::Preference want;
    want.maxHeight = 720;
    want.videoCodecs = {"avc1"};
    want.audioCodecs = {"mp4a"};
    const yt::Choice c = yt::choose(p, want);
    REQUIRE(c.source == &p.sources[0]);
    REQUIRE(c.video);
    REQUIRE(c.audio);
    CHECK(c.video->itag == 136);
    CHECK(c.audio->itag == 140);
    CHECK_FALSE(c.progressive);
    CHECK(c.hls.empty());
}
TEST_CASE("choose: without limits the highest video and audio") {
    const yt::Player p = twoSources();
    const yt::Choice c = yt::choose(p);
    REQUIRE(c.video);
    REQUIRE(c.audio);
    CHECK(c.video->itag == 137);
    CHECK(c.audio->itag == 251);
}
// ANDROID_VR's itag 18 answered 403 live (2026-10-15): progressive is the last resort.
TEST_CASE("choose: separate, then HLS, then progressive") {
    const yt::Player p = twoSources();
    yt::Preference want;
    want.allowSeparate = false;
    yt::Choice c = yt::choose(p, want);
    CHECK(c.hls == "https://hls");
    CHECK(c.source == &p.sources[0]);
    CHECK_FALSE(c.video);
    CHECK_FALSE(c.progressive);
    want.allowHls = false;
    c = yt::choose(p, want);
    REQUIRE(c.progressive);
    CHECK(c.progressive->itag == 18);
    CHECK(c.source == &p.sources[1]);
    CHECK(c.source->userAgent == "UA-VR");
    CHECK(c.hls.empty());
}
TEST_CASE("choose: a live stream plays its HLS first") {
    yt::Player p = twoSources();
    p.details.isLive = true;
    const yt::Choice c = yt::choose(p);
    CHECK(c.hls == "https://hls");
    CHECK_FALSE(c.video);
}
TEST_CASE("choose: video and audio never mix sources") {
    yt::Player p;
    yt::Source a, b;
    a.client = yt::ClientId::VISIONOS;
    a.formats = {video(137, 1920, 1080, "avc1.640028")};                // no audio-only format
    b.client = yt::ClientId::IOS;
    b.formats = {video(136, 1280, 720, "avc1.4d401f"), audio(140, "mp4a.40.2", 128000)};
    p.sources = {a, b};
    const yt::Choice c = yt::choose(p);
    REQUIRE(c.source == &p.sources[1]);
    CHECK(c.video->itag == 136);
    CHECK(c.audio->itag == 140);
}
TEST_CASE("choose: formats without a usable url are skipped") {
    yt::Player p;
    yt::Source s;
    yt::Format sealedHi = video(137, 1920, 1080, "avc1.640028");
    sealedHi.sealed = true;
    yt::Format sabrOnly = video(136, 1280, 720, "avc1.4d401f");
    sabrOnly.url.clear();
    s.formats = {sealedHi, sabrOnly, video(135, 854, 480, "avc1.4d401e"), audio(140, "mp4a.40.2", 128000)};
    p.sources = {s};
    const yt::Choice c = yt::choose(p);
    REQUIRE(c.video);
    CHECK(c.video->itag == 135);
}
TEST_CASE("choose: maxHeight caps the short side, so portrait videos are not lost") {
    yt::Player p;
    yt::Source s;
    s.formats = {video(1, 1080, 1920, "avc1"), video(2, 720, 1280, "avc1"), audio(140, "mp4a.40.2", 128000)};
    p.sources = {s};
    yt::Preference want;
    want.maxHeight = 720;
    const yt::Choice c = yt::choose(p, want);
    REQUIRE(c.video);
    CHECK(c.video->itag == 2);
}
TEST_CASE("choose: the audio language picks a dubbed track, else the default one") {
    yt::Player p;
    yt::Source s;
    s.formats = {video(136, 1280, 720, "avc1"),
                 audio(1, "mp4a.40.2", 128000, "en", true),
                 audio(2, "mp4a.40.2", 128000, "ru", false, true),          // auto-dubbed
                 audio(3, "mp4a.40.2", 96000, "ru", false, false),          // a real dub
                 audio(4, "mp4a.40.2", 128000, "de", false, true)};
    p.sources = {s};
    yt::Preference want;
    want.audioLanguage = "ru-RU";
    CHECK(yt::choose(p, want).audio->itag == 3);
    want.audioLanguage = "DE";
    CHECK(yt::choose(p, want).audio->itag == 4);
    want.audioLanguage = "fr";                                               // no such track
    CHECK(yt::choose(p, want).audio->itag == 1);
    want.audioLanguage.clear();
    CHECK(yt::choose(p, want).audio->itag == 1);
}
TEST_CASE("choose: nothing playable is an empty choice") {
    yt::Player p;
    CHECK(yt::choose(p).source == nullptr);
    p = twoSources();
    yt::Preference want;
    want.videoCodecs = {"av01"};
    want.allowHls = false;
    const yt::Choice c = yt::choose(p, want);                               // itag 18 is avc1 too
    CHECK(c.source == nullptr);
    CHECK_FALSE(c.video);
    CHECK_FALSE(c.progressive);
}
TEST_CASE("captionFor: manual before auto-generated, exact before the base language") {
    yt::Player p;
    auto track = [](const char *lang, bool asr) {
        yt::CaptionTrack t; t.language = lang; t.url = std::string("https://t/") + lang + (asr ? "/asr" : ""); t.autoGenerated = asr;
        return t;
    };
    p.captions = {track("en", true), track("en-GB", false), track("en", false), track("ru", true)};
    REQUIRE(yt::captionFor(p, "en"));
    CHECK(yt::captionFor(p, "en")->url == "https://t/en");
    CHECK(yt::captionFor(p, "en-US")->url == "https://t/en");               // base language
    CHECK(yt::captionFor(p, "EN-gb")->url == "https://t/en-GB");
    CHECK(yt::captionFor(p, "ru")->url == "https://t/ru/asr");              // only ASR exists
    CHECK(yt::captionFor(p, "de") == nullptr);
    CHECK(yt::captionFor(p, "") == nullptr);                                 // subtitles off
}
TEST_CASE("cueAt: the cue on screen at a moment") {
    std::vector<yt::Cue> cues(3);
    cues[0].startMs = 1000; cues[0].endMs = 2000; cues[0].text = "a";
    cues[1].startMs = 2500; cues[1].endMs = 4000; cues[1].text = "b";
    cues[2].startMs = 3000; cues[2].endMs = 5000; cues[2].text = "c";      // overlaps b (ASR rolls)
    CHECK(yt::cueAt(cues, 0) == nullptr);
    CHECK(yt::cueAt(cues, 1000)->text == "a");
    CHECK(yt::cueAt(cues, 1999)->text == "a");
    CHECK(yt::cueAt(cues, 2000) == nullptr);                                 // end is exclusive
    CHECK(yt::cueAt(cues, 2700)->text == "b");
    CHECK(yt::cueAt(cues, 3500)->text == "c");                               // the later one wins
    CHECK(yt::cueAt(cues, 5000) == nullptr);
    CHECK(yt::cueAt(std::vector<yt::Cue>(), 10) == nullptr);
}
