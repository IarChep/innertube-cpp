// /player answers → the client walk's view (PlayerPart) and storyboard specs. A fixed
// root-level schema (json/shapes/player.h): one typed partial read (unknown keys
// skipped); only the offline slate, whose path differs per client, is a bounded scan.
#include "playerparser.h"
#include "json/jsonscan.h"
#include "json/shapes/player.h"
#include <charconv>
#include <climits>
#include <cstdint>
#include <string>
namespace yt {

using namespace gj;
static const std::string &str(const std::string &s) { return s; }
static std::string str(const std::optional<std::string> &s) { return s ? str(*s) : std::string(); }

namespace {

// The verdict from the status word alone: the reason is localized by hl.
Playability kindOf(const std::string &status, bool ageMarker)
{
    if (status == "OK") return Playability::Ok;
    if (status.empty()) return Playability::Unknown;
    if (ageMarker || status.rfind("AGE_", 0) == 0 || status == "CONTENT_CHECK_REQUIRED")
        return Playability::AgeRestricted;
    // The walk renames it LoginRequired when the bearer's TV client got it.
    if (status == "LOGIN_REQUIRED") return Playability::BotCheck;
    // Becomes Upcoming once a start time is known.
    if (status == "LIVE_STREAM_OFFLINE") return Playability::LiveOffline;
    return Playability::Unplayable;              // ERROR, UNPLAYABLE, …
}

// "2026-09-09T20:00:00+03:00" / "…Z" / "….123Z" → unix seconds; 0 when malformed.
int64_t unixFromIso8601(std::string_view s)
{
    auto num = [s](size_t at, size_t n, int &out) {
        if (at + n > s.size()) return false;
        const char *b = s.data() + at, *e = b + n;
        const std::from_chars_result r = std::from_chars(b, e, out);
        return r.ec == std::errc() && r.ptr == e;
    };
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (s.size() < 19 || !num(0, 4, y) || s[4] != '-' || !num(5, 2, mo) || s[7] != '-' || !num(8, 2, d)
        || (s[10] != 'T' && s[10] != ' ') || !num(11, 2, h) || s[13] != ':' || !num(14, 2, mi)
        || s[16] != ':' || !num(17, 2, se) || mo < 1 || mo > 12)
        return 0;
    // days_from_civil (Howard Hinnant's public-domain algorithm).
    y -= mo <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned) (y - era * 400);
    const unsigned doy = (unsigned) ((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t t = ((int64_t) era * 146097 + (int64_t) doe - 719468) * 86400 + h * 3600 + mi * 60 + se;
    size_t z = 19;
    while (z < s.size() && (s[z] == '.' || (s[z] >= '0' && s[z] <= '9'))) ++z;   // fractional seconds
    if (z < s.size() && (s[z] == '+' || s[z] == '-')) {
        int oh = 0, om = 0;
        if (!num(z + 1, 2, oh) || z + 3 >= s.size() || s[z + 3] != ':' || !num(z + 4, 2, om)) return 0;
        const int64_t off = oh * 3600 + om * 60;
        t += s[z] == '+' ? -off : off;
    }
    return t;
}

// The offline slate lies under playabilityStatus on WEB-family answers and elsewhere on
// others: find the renderer anywhere, bounded by kScanMaxDepth like every scan.
struct SlateFinder {
    std::string_view found;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view k, int)
    {
        if (!found.empty()) return scan::Action::Skip;
        return k == "liveStreamOfflineSlateRenderer" ? scan::Action::Capture : scan::Action::Descend;
    }
    void capture(std::string_view, std::string_view v, int) { found = v; }
};

Format formatOf(const pj::FormatJ &f, bool muxed)
{
    Format o;
    o.itag = (int) toInt64(f.itag);
    o.url = str(f.url);
    o.sealed = o.url.empty() && (f.signatureCipher.has_value() || f.cipher.has_value());
    const std::string mime = str(f.mimeType);         // video/mp4; codecs="avc1.640028"
    o.mimeType = mime.substr(0, mime.find(';'));
    const size_t q = mime.find("codecs=\"");
    if (q != std::string::npos) {
        const size_t b = q + 8, e = mime.find('"', b);
        o.codecs = mime.substr(b, e == std::string::npos ? std::string::npos : e - b);
    }
    o.qualityLabel = str(f.qualityLabel);
    o.width = (int) toInt64(f.width);
    o.height = (int) toInt64(f.height);
    o.fps = (int) toInt64(f.fps);
    o.bitrate = toInt64(f.bitrate);
    o.averageBitrate = toInt64(f.averageBitrate);
    o.contentLength = toInt64(f.contentLength);
    o.durationMs = toInt64(f.approxDurationMs);
    o.lastModified = toInt64(f.lastModified);
    if (f.initRange) o.initRange = ByteRange{toInt64(f.initRange->start), toInt64(f.initRange->end)};
    if (f.indexRange) o.indexRange = ByteRange{toInt64(f.indexRange->start), toInt64(f.indexRange->end)};
    o.xtags = str(f.xtags);
    o.hasVideo = o.mimeType.rfind("video/", 0) == 0;
    o.hasAudio = muxed || o.mimeType.rfind("audio/", 0) == 0;
    const bool drcFlag = f.isDrc.value_or(false);
    if (f.audioTrack || drcFlag) {
        // xtags is a small protobuf whose pairs ("acont" → "original" / "dubbed" /
        // "dubbed-auto", "drc" → "1") are in clear text; displayName is localized and
        // useless as a marker.
        std::string marks;
        base64Decode(o.xtags, &marks);
        AudioTrack a;
        if (f.audioTrack) {
            a.id = str(f.audioTrack->id);
            a.name = str(f.audioTrack->displayName);
            a.isDefault = f.audioTrack->audioIsDefault.value_or(false);
            a.language = a.id.substr(0, a.id.find('.'));
        }
        a.original = marks.find("original") != std::string::npos;
        a.autoDubbed = marks.find("dubbed-auto") != std::string::npos;
        a.drc = drcFlag || marks.find("drc") != std::string::npos;
        o.audio = a;
    }
    return o;
}

void splitInto(std::string_view s, char sep, std::vector<std::string_view> &out)
{
    out.clear();
    size_t b = 0;
    while (true) {
        const size_t e = s.find(sep, b);
        out.push_back(s.substr(b, e == std::string_view::npos ? std::string_view::npos : e - b));
        if (e == std::string_view::npos) return;
        b = e + 1;
    }
}

bool numberOf(std::string_view s, int64_t &out)
{
    const std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size();
}

void replaceAll(std::string &s, std::string_view what, std::string_view with)
{
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size()))
        s.replace(at, what.size(), with);
}

} // namespace

PlayerPart parsePlayerResponse(const std::string &body)
{
    pj::Root root{};
    readJsonDoc(root, body);
    PlayerPart p;
    if (root.playabilityStatus) {
        const pj::StatusJ &st = *root.playabilityStatus;
        p.status.status = str(st.status);
        p.status.reason = str(st.reason);
        p.status.kind = kindOf(p.status.status, st.desktopLegacyAgeGateReason.has_value());
    }
    if (p.status.status == "LIVE_STREAM_OFFLINE") {
        SlateFinder sf;
        scan::document(body, sf);
        if (!sf.found.empty()) {
            pj::SlateJ sl{};
            readJson(sl, sf.found);
            p.status.scheduledStartSecs = toInt64(sl.scheduledStartTime);
            const std::string main = textOf(sl.mainText), sub = textOf(sl.subtitleText);
            p.status.slateText = (!main.empty() && !sub.empty()) ? main + "\n" + sub : main + sub;
        }
        if (p.status.scheduledStartSecs <= 0 && root.microformat && root.microformat->playerMicroformatRenderer
            && root.microformat->playerMicroformatRenderer->liveBroadcastDetails)
            p.status.scheduledStartSecs =
                unixFromIso8601(str(root.microformat->playerMicroformatRenderer->liveBroadcastDetails->startTimestamp));
        if (p.status.scheduledStartSecs > 0) p.status.kind = Playability::Upcoming;
    }
    p.final = p.status.status == "ERROR" || p.status.kind == Playability::Upcoming
           || p.status.kind == Playability::LiveOffline;
    if (root.streamingData) {
        const pj::StreamingJ &sd = *root.streamingData;
        if (sd.formats) for (const pj::FormatJ &f : *sd.formats) p.formats.push_back(formatOf(f, true));
        if (sd.adaptiveFormats)
            for (const pj::FormatJ &f : *sd.adaptiveFormats) p.formats.push_back(formatOf(f, false));
        p.hlsUrl = str(sd.hlsManifestUrl);
        p.dashUrl = str(sd.dashManifestUrl);
        p.sabrUrl = str(sd.serverAbrStreamingUrl);
        p.expiresInSecs = toInt64(sd.expiresInSeconds);
    }
    if (root.playerConfig && root.playerConfig->mediaCommonConfig
        && root.playerConfig->mediaCommonConfig->mediaUstreamerRequestConfig)
        p.sabrConfig =
            str(root.playerConfig->mediaCommonConfig->mediaUstreamerRequestConfig->videoPlaybackUstreamerConfig);
    if (root.videoDetails) {
        const pj::DetailsJ &d = *root.videoDetails;
        p.details.id = str(d.videoId);
        p.details.title = str(d.title);
        p.details.author = str(d.author);
        p.details.channelId = str(d.channelId);
        p.details.description = str(d.shortDescription);
        p.details.lengthSecs = toInt64(d.lengthSeconds);
        p.details.viewCount = toInt64(d.viewCount);
        p.details.isLive = d.isLive.value_or(false);
        p.details.isLiveContent = d.isLiveContent.value_or(false);
        p.details.isUpcoming = d.isUpcoming.value_or(false);
        p.details.thumbnailUrl = lastThumbUrl(d.thumbnail);
        if (p.details.thumbnailUrl.empty() && !p.details.id.empty())
            p.details.thumbnailUrl = "https://i.ytimg.com/vi/" + p.details.id + "/hqdefault.jpg";
    }
    if (root.captions && root.captions->playerCaptionsTracklistRenderer
        && root.captions->playerCaptionsTracklistRenderer->captionTracks)
        for (const pj::TrackJ &t : *root.captions->playerCaptionsTracklistRenderer->captionTracks) {
            CaptionTrack c;
            c.url = str(t.baseUrl);
            c.language = str(t.languageCode);
            c.name = textOf(t.name);
            c.autoGenerated = str(t.kind) == "asr";
            c.translatable = t.isTranslatable.value_or(false);
            if (!c.url.empty()) p.captions.push_back(std::move(c));
        }
    if (root.storyboards && root.storyboards->playerStoryboardSpecRenderer)
        p.storyboardSpec = str(root.storyboards->playerStoryboardSpecRenderer->spec);
    if (root.playbackTracking) {
        const pj::TrackingJ &tr = *root.playbackTracking;
        if (tr.videostatsWatchtimeUrl) p.watchtimeUrl = str(tr.videostatsWatchtimeUrl->baseUrl);
        if (tr.videostatsPlaybackUrl) p.playbackUrl = str(tr.videostatsPlaybackUrl->baseUrl);
    }
    return p;
}

// A storyboard cell side or grid dimension: cols·width stays far inside int.
constexpr int64_t kMaxCell = 32767;

std::vector<StoryboardLevel> parseStoryboardSpec(std::string_view spec, int64_t lengthSecs)
{
    std::vector<StoryboardLevel> out;
    std::vector<std::string_view> parts, f;
    splitInto(spec, '|', parts);
    if (parts.size() < 2) return out;
    for (size_t i = 1; i < parts.size(); ++i) {
        splitInto(parts[i], '#', f);
        if (f.size() < 8) continue;
        int64_t v[6];
        bool numbers = true;
        for (int k = 0; k < 6; ++k) numbers = numbers && numberOf(f[k], v[k]);
        // Server-issued numbers: a count past INT_MAX used to truncate to 0 and divide by
        // it, and huge cells overflowed storyboardFrame's int math. Real levels are tiny.
        if (!numbers || v[0] < 0 || v[0] > kMaxCell || v[1] < 0 || v[1] > kMaxCell
            || v[2] <= 0 || v[2] > INT_MAX || v[3] <= 0 || v[3] > kMaxCell || v[4] <= 0 || v[4] > kMaxCell)
            continue;
        StoryboardLevel l;
        l.width = (int) v[0];
        l.height = (int) v[1];
        l.count = (int) v[2];
        l.cols = (int) v[3];
        l.rows = (int) v[4];
        l.intervalMs = v[5];
        if (l.intervalMs <= 0 && lengthSecs > 0 && lengthSecs <= INT64_MAX / 1000)
            l.intervalMs = lengthSecs * 1000 / l.count;
        std::string url(parts[0]);
        replaceAll(url, "$L", std::to_string(i - 1));
        replaceAll(url, "$N", f[6]);                 // a name like "M$M" keeps $M for the sheet
        url += url.find('?') == std::string::npos ? "?sigh=" : "&sigh=";
        url += f[7];
        l.urlTemplate = std::move(url);
        out.push_back(std::move(l));
    }
    return out;
}

}
