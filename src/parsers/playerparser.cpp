// /player answers → the client walk's view (PlayerPart) and storyboard specs. A fixed
// root-level schema read by path; only the offline slate, whose path differs per client,
// is searched for.
#include "playerparser.h"
#include "json/ytjson.h"
#include <charconv>
#include <climits>
#include <cstdint>
#include <string>
namespace yt {

using gj::flexInt;
using gj::textOf;
using json::Value;

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

Format formatOf(Value f, bool muxed)
{
    Format o;
    o.itag = (int) flexInt(f["itag"]);
    o.url = f["url"].string();
    o.sealed = o.url.empty() && (f["signatureCipher"] || f["cipher"]);
    const std::string mime = f["mimeType"].string();   // video/mp4; codecs="avc1.640028"
    o.mimeType = mime.substr(0, mime.find(';'));
    const size_t q = mime.find("codecs=\"");
    if (q != std::string::npos) {
        const size_t b = q + 8, e = mime.find('"', b);
        o.codecs = mime.substr(b, e == std::string::npos ? std::string::npos : e - b);
    }
    o.qualityLabel = f["qualityLabel"].string();
    o.width = (int) flexInt(f["width"]);
    o.height = (int) flexInt(f["height"]);
    o.fps = (int) flexInt(f["fps"]);
    o.bitrate = flexInt(f["bitrate"]);
    o.averageBitrate = flexInt(f["averageBitrate"]);
    o.contentLength = flexInt(f["contentLength"]);
    o.durationMs = flexInt(f["approxDurationMs"]);
    o.lastModified = flexInt(f["lastModified"]);
    if (const Value r = f["initRange"]) o.initRange = ByteRange{flexInt(r["start"]), flexInt(r["end"])};
    if (const Value r = f["indexRange"]) o.indexRange = ByteRange{flexInt(r["start"]), flexInt(r["end"])};
    o.xtags = f["xtags"].string();
    o.hasVideo = o.mimeType.rfind("video/", 0) == 0;
    o.hasAudio = muxed || o.mimeType.rfind("audio/", 0) == 0;
    const bool drcFlag = f["isDrc"].boolean();
    const Value track = f["audioTrack"];
    if (track || drcFlag) {
        // xtags is a small protobuf whose pairs ("acont" → "original" / "dubbed" /
        // "dubbed-auto", "drc" → "1") are in clear text; displayName is localized and
        // useless as a marker.
        std::string marks;
        gj::base64Decode(o.xtags, &marks);
        AudioTrack a;
        if (track) {
            a.id = track["id"].string();
            a.name = track["displayName"].string();
            a.isDefault = track["audioIsDefault"].boolean();
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

PlayerPart parsePlayerResponse(Value root)
{
    PlayerPart p;
    const Value st = root["playabilityStatus"];
    p.status.status = st["status"].string();
    p.status.reason = st["reason"].string();
    p.status.kind = kindOf(p.status.status, (bool) st["desktopLegacyAgeGateReason"]);
    if (p.status.status == "LIVE_STREAM_OFFLINE") {
        // The offline slate lies under playabilityStatus on WEB-family answers and
        // elsewhere on others: find the renderer anywhere.
        if (const Value sl = gj::first(root, "liveStreamOfflineSlateRenderer")) {
            p.status.scheduledStartSecs = flexInt(sl["scheduledStartTime"]);
            const std::string main = textOf(sl["mainText"]), sub = textOf(sl["subtitleText"]);
            p.status.slateText = (!main.empty() && !sub.empty()) ? main + "\n" + sub : main + sub;
        }
        if (p.status.scheduledStartSecs <= 0)
            p.status.scheduledStartSecs = unixFromIso8601(
                root.path("microformat", "playerMicroformatRenderer", "liveBroadcastDetails", "startTimestamp").str());
        if (p.status.scheduledStartSecs > 0) p.status.kind = Playability::Upcoming;
    }
    p.final = p.status.status == "ERROR" || p.status.kind == Playability::Upcoming
           || p.status.kind == Playability::LiveOffline;
    const Value sd = root["streamingData"];
    for (Value f : sd["formats"].items()) p.formats.push_back(formatOf(f, true));
    for (Value f : sd["adaptiveFormats"].items()) p.formats.push_back(formatOf(f, false));
    p.hlsUrl = sd["hlsManifestUrl"].string();
    p.dashUrl = sd["dashManifestUrl"].string();
    p.sabrUrl = sd["serverAbrStreamingUrl"].string();
    p.expiresInSecs = flexInt(sd["expiresInSeconds"]);
    p.sabrConfig = root.path("playerConfig", "mediaCommonConfig", "mediaUstreamerRequestConfig",
                             "videoPlaybackUstreamerConfig").string();
    if (const Value d = root["videoDetails"]) {
        p.details.id = d["videoId"].string();
        p.details.title = d["title"].string();
        p.details.author = d["author"].string();
        p.details.channelId = d["channelId"].string();
        p.details.description = d["shortDescription"].string();
        p.details.lengthSecs = flexInt(d["lengthSeconds"]);
        p.details.viewCount = flexInt(d["viewCount"]);
        p.details.isLive = d["isLive"].boolean();
        p.details.isLiveContent = d["isLiveContent"].boolean();
        p.details.isUpcoming = d["isUpcoming"].boolean();
        p.details.thumbnailUrl = gj::lastThumbUrl(d["thumbnail"]);
        if (p.details.thumbnailUrl.empty() && !p.details.id.empty())
            p.details.thumbnailUrl = "https://i.ytimg.com/vi/" + p.details.id + "/hqdefault.jpg";
    }
    for (Value t : root.path("captions", "playerCaptionsTracklistRenderer", "captionTracks").items()) {
        CaptionTrack c;
        c.url = t["baseUrl"].string();
        c.language = t["languageCode"].string();
        c.name = textOf(t["name"]);
        c.autoGenerated = t["kind"].str() == "asr";
        c.translatable = t["isTranslatable"].boolean();
        if (!c.url.empty()) p.captions.push_back(std::move(c));
    }
    p.storyboardSpec = root.path("storyboards", "playerStoryboardSpecRenderer", "spec").string();
    p.watchtimeUrl = root.path("playbackTracking", "videostatsWatchtimeUrl", "baseUrl").string();
    p.playbackUrl = root.path("playbackTracking", "videostatsPlaybackUrl", "baseUrl").string();
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
