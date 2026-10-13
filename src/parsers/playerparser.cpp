#include "playerparser.h"
#include "ytjson.h"
#include <string>
namespace yt {

using namespace gj;

// The /player response is a fixed root-level schema — no tree walking needed,
// one typed partial read per entry point (unknown keys skipped).
namespace pj {

struct PlayabilityStatus {
    std::optional<std::string> status;
    std::optional<std::string> reason;
};
struct Format {
    std::optional<std::string> url;
    std::optional<std::string> qualityLabel;
    std::optional<std::string> mimeType;
    std::optional<std::string> audioQuality;
    std::optional<FlexInt> itag;
    std::optional<FlexInt> width;
    std::optional<FlexInt> height;
    std::optional<FlexInt> bitrate;
};
struct StreamingData {
    std::optional<std::string> hlsManifestUrl;
    std::optional<std::string> serverAbrStreamingUrl;   // SABR offered; may coexist with fetchable urls
    std::optional<std::vector<Format>> formats;
    std::optional<std::vector<Format>> adaptiveFormats;
};
struct VideoDetails {
    std::optional<std::string> videoId;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> channelId;
    std::optional<std::string> shortDescription;
    std::optional<FlexInt> viewCount;                   // arrives as a quoted string
    std::optional<ThumbSet> thumbnail;
};
struct CaptionTrack {
    std::optional<std::string> baseUrl;
    std::optional<std::string> languageCode;
    std::optional<Text> name;
};
struct TracklistR {
    std::optional<std::vector<CaptionTrack>> captionTracks;
};
struct Captions {
    std::optional<TracklistR> playerCaptionsTracklistRenderer;
};
struct PlayerRoot {
    std::optional<PlayabilityStatus> playabilityStatus;
    std::optional<StreamingData> streamingData;
    std::optional<VideoDetails> videoDetails;
    std::optional<Captions> captions;
};

} // namespace pj

static const std::string &str(const std::string &s) { return s; }
static std::string str(const std::optional<std::string> &s) { return s ? str(*s) : std::string(); }

// ---------------------------------------------------------------------------
// Static helpers — each takes an already-parsed root, contains verbatim logic
// ---------------------------------------------------------------------------

static bool playableOf(const pj::PlayerRoot &root, std::string *reason)
{
    if (root.playabilityStatus) {
        const std::string st = str(root.playabilityStatus->status);
        if (!st.empty() && st != "OK") {
            if (reason) *reason = st + ": " + str(root.playabilityStatus->reason);
            return false;
        }
    }
    return true;
}

// Build one Stream from a parsed format (muxed or adaptive). `muxed` = came
// from streamingData.formats (video+audio in one file). Returns false if the
// format has no fetchable url (ciphered / SABR-withheld).
static bool streamOf(const pj::Format &f, bool muxed, Stream &s)
{
    const std::string url = str(f.url);
    if (url.empty()) return false;
    const std::string mime = str(f.mimeType);
    s.id = std::to_string(toInt64(f.itag));
    s.url = url;
    s.mimeType = mime;
    s.width = (int)toInt64(f.width);
    s.height = (int)toInt64(f.height);
    s.bitrate = (int)toInt64(f.bitrate);
    s.hasAudio = muxed || mime.starts_with("audio/");
    // Human label: "360p" for video, else the audio quality tier / mime subtype.
    if (!str(f.qualityLabel).empty())      s.description = str(f.qualityLabel);
    else if (s.width > 0)                     s.description = std::to_string(s.height) + "p";
    else if (!str(f.audioQuality).empty()) s.description = str(f.audioQuality);
    else                                      s.description = mime.substr(0, mime.find(';'));
    return true;
}

// The FULL stream catalog: HLS manifest + every url-ful muxed / video-only /
// audio-only format, each tagged (see Stream). StreamSet slices this into
// its video/audio lists and default picks; consumers can select any entry.
static std::vector<Stream> streamsOf(const pj::PlayerRoot &root, bool *sawCipheredOnly)
{
    std::vector<Stream> out;
    if (sawCipheredOnly) *sawCipheredOnly = false;
    if (!root.streamingData) return out;
    const pj::StreamingData &sd = *root.streamingData;
    const std::string hls = str(sd.hlsManifestUrl);
    if (!hls.empty()) {
        Stream s; s.id = "hls"; s.description = "HLS (adaptive)"; s.url = hls; s.hasAudio = true;
        out.push_back(s);
    }
    int formatsSeen = 0;
    if (sd.formats)         // muxed video+audio (itag 18/22)
        for (const pj::Format &f : *sd.formats) {
            ++formatsSeen;
            Stream s; if (streamOf(f, true, s)) out.push_back(s);
        }
    if (sd.adaptiveFormats) // separate video-only + audio-only tracks
        for (const pj::Format &f : *sd.adaptiveFormats) {
            ++formatsSeen;
            Stream s; if (streamOf(f, false, s)) out.push_back(s);
        }
    // Nothing playable but formats were present → every format was ciphered.
    if (sawCipheredOnly) *sawCipheredOnly = out.empty() && formatsSeen > 0;
    return out;
}

static Video detailsOf(const pj::PlayerRoot &root)
{
    Video v;
    if (!root.videoDetails) return v;
    const pj::VideoDetails &d = *root.videoDetails;
    v.id = str(d.videoId);
    v.title = str(d.title);
    v.channelName = str(d.author);
    v.channelId = str(d.channelId);
    v.description = str(d.shortDescription);
    v.viewCount = toInt64(d.viewCount);
    if (d.thumbnail) v.thumbnailUrl = str(lastThumbUrl(*d.thumbnail));
    if (v.thumbnailUrl.empty() && !v.id.empty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    return v;
}

static std::vector<Subtitle> captionsOf(const pj::PlayerRoot &root)
{
    std::vector<Subtitle> out;
    if (!root.captions || !root.captions->playerCaptionsTracklistRenderer) return out;
    const pj::TracklistR &tl = *root.captions->playerCaptionsTracklistRenderer;
    if (!tl.captionTracks) return out;
    for (const pj::CaptionTrack &t : *tl.captionTracks) {
        Subtitle s;
        s.url = str(t.baseUrl);
        s.language = str(t.languageCode);
        s.title = str(textOf(t.name));
        s.id = s.language;
        if (!s.url.empty()) out.push_back(s);
    }
    return out;
}

// ---------------------------------------------------------------------------
// parsePlayer — ONE typed read, all four sections
// ---------------------------------------------------------------------------

static PlayerResult parsePlayerRoot(const pj::PlayerRoot &root)
{
    PlayerResult r;
    r.playable = playableOf(root, &r.reason);
    r.streams  = streamsOf(root, &r.cipheredOnly);
    if (root.streamingData) {
        const pj::StreamingData &sd = *root.streamingData;
        r.sabr = sd.serverAbrStreamingUrl && !sd.serverAbrStreamingUrl->empty();
        r.formatsSeen  = sd.formats ? (int)sd.formats->size() : 0;
        r.adaptiveSeen = sd.adaptiveFormats ? (int)sd.adaptiveFormats->size() : 0;
    }
    r.details  = detailsOf(root);
    r.captions = captionsOf(root);
    return r;
}

PlayerResult parsePlayer(std::string_view p)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return parsePlayerRoot(root);
}

// Whole-document overload: *r.body is NUL-terminated, so read via the sentinel
// path (kInDoc). Behavior-identical to the string_view form (golden diff gate).
PlayerResult parsePlayer(const std::string &p)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return parsePlayerRoot(root);
}

// ---------------------------------------------------------------------------
// Public entry points — thin wrappers, each does its own single read
// ---------------------------------------------------------------------------

bool isPlayable(std::string_view p, std::string *reason)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return playableOf(root, reason);
}
bool isPlayable(const std::string &p, std::string *reason)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return playableOf(root, reason);
}

std::vector<Stream> parseStreams(std::string_view p, bool *sawCipheredOnly)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return streamsOf(root, sawCipheredOnly);
}
std::vector<Stream> parseStreams(const std::string &p, bool *sawCipheredOnly)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return streamsOf(root, sawCipheredOnly);
}

Video parseVideoDetails(std::string_view p)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return detailsOf(root);
}
Video parseVideoDetails(const std::string &p)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return detailsOf(root);
}

std::vector<Subtitle> parseCaptions(std::string_view p)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return captionsOf(root);
}
std::vector<Subtitle> parseCaptions(const std::string &p)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return captionsOf(root);
}
}
