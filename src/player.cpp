// Pure helpers of the public player types (no I/O): storyboard frames, choosing what to
// play, the caption track for a language, the cue on screen.
#include <yt/player.h>
#include <algorithm>
#include <tuple>
namespace yt {

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c; }
bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
bool istarts(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}
// "pt-BR" / "pt_BR" -> "pt".
std::string_view baseLanguage(std::string_view tag) { return tag.substr(0, tag.find_first_of("-_")); }
// 3 = the same tag; 2 = one is the other's base ("en" for "en-US"); 1 = siblings ("en-GB"
// for "en-US"); 0 = another language (or none asked).
int languageMatch(std::string_view have, std::string_view want)
{
    if (want.empty() || have.empty()) return 0;
    if (iequals(have, want)) return 3;
    if (iequals(have, baseLanguage(want)) || iequals(baseLanguage(have), want)) return 2;
    return iequals(baseLanguage(have), baseLanguage(want)) ? 1 : 0;
}

// The rank of the first codec in "avc1.4d401f, mp4a.40.2" that matches `wanted` (lower is
// better); -1 when none does. An empty list accepts anything at rank 0.
int codecRank(std::string_view codecs, const std::vector<std::string> &wanted)
{
    if (wanted.empty()) return 0;
    for (size_t i = 0; i < wanted.size(); ++i) {
        std::string_view rest = codecs;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            std::string_view part = rest.substr(0, comma);
            while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
            if (istarts(part, wanted[i])) return (int) i;
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    return -1;
}

bool usable(const Format &f) { return !f.url.empty() && !f.sealed; }
int shortSide(const Format &f) { return (f.width > 0 && f.height > 0) ? std::min(f.width, f.height) : f.height; }
bool fits(const Format &f, const Preference &want) { return want.maxHeight <= 0 || shortSide(f) <= want.maxHeight; }

// Higher is better: size, then the earlier wanted codec, then frame rate and bitrate.
using VideoKey = std::tuple<int, int, int, int64_t>;
VideoKey videoKey(const Format &f, int rank) { return VideoKey(shortSide(f), -rank, f.fps, f.bitrate); }

// Higher is better: the asked language, then the server's default track (a format without
// track info is the only track), a real voice over a synthetic one, no DRC, bitrate.
using AudioKey = std::tuple<int, bool, bool, bool, int64_t>;
AudioKey audioKey(const Format &f, const Preference &want)
{
    if (!f.audio) return AudioKey(0, true, true, true, f.bitrate);
    const AudioTrack &t = *f.audio;
    return AudioKey(languageMatch(t.language, want.audioLanguage), t.isDefault, !t.autoDubbed, !t.drc, f.bitrate);
}

const Format *bestVideo(const Source &s, const Preference &want)
{
    const Format *best = nullptr;
    VideoKey bestKey;
    for (const Format &f : s.formats) {
        if (!f.hasVideo || f.hasAudio || !usable(f) || !fits(f, want)) continue;
        const int rank = codecRank(f.codecs, want.videoCodecs);
        if (rank < 0) continue;
        const VideoKey k = videoKey(f, rank);
        if (!best || k > bestKey) { best = &f; bestKey = k; }
    }
    return best;
}

const Format *bestAudio(const Source &s, const Preference &want)
{
    const Format *best = nullptr;
    AudioKey bestKey;
    for (const Format &f : s.formats) {
        if (!f.hasAudio || f.hasVideo || !usable(f) || codecRank(f.codecs, want.audioCodecs) < 0) continue;
        const AudioKey k = audioKey(f, want);
        if (!best || k > bestKey) { best = &f; bestKey = k; }
    }
    return best;
}

const Format *bestProgressive(const Source &s, const Preference &want)
{
    const Format *best = nullptr;
    VideoKey bestKey;
    for (const Format &f : s.formats) {
        if (!f.hasVideo || !f.hasAudio || !usable(f) || !fits(f, want)) continue;
        const int rank = codecRank(f.codecs, want.videoCodecs);
        if (rank < 0 || codecRank(f.codecs, want.audioCodecs) < 0) continue;
        const VideoKey k = videoKey(f, rank);
        if (!best || k > bestKey) { best = &f; bestKey = k; }
    }
    return best;
}

Choice separate(const Player &p, const Preference &want)
{
    Choice c;
    VideoKey bestKey;
    for (const Source &s : p.sources) {                   // walk order breaks ties
        const Format *v = bestVideo(s, want);
        const Format *a = v ? bestAudio(s, want) : nullptr;
        if (!a) continue;                                 // video and audio share one source's UA and expiry
        const VideoKey k = videoKey(*v, codecRank(v->codecs, want.videoCodecs));
        if (!c.source || k > bestKey) { c.source = &s; c.video = v; c.audio = a; bestKey = k; }
    }
    return c;
}

Choice hls(const Player &p)
{
    Choice c;
    for (const Source &s : p.sources)
        if (!s.hlsUrl.empty()) { c.source = &s; c.hls = s.hlsUrl; break; }
    return c;
}

Choice progressive(const Player &p, const Preference &want)
{
    Choice c;
    VideoKey bestKey;
    for (const Source &s : p.sources) {
        const Format *f = bestProgressive(s, want);
        if (!f) continue;
        const VideoKey k = videoKey(*f, codecRank(f->codecs, want.videoCodecs));
        if (!c.source || k > bestKey) { c.source = &s; c.progressive = f; bestKey = k; }
    }
    return c;
}

} // namespace

Choice choose(const Player &p, const Preference &want)
{
    // A live stream's adaptive URLs are segment streams, not files: its manifest comes first.
    if (p.details.isLive && want.allowHls) {
        Choice c = hls(p);
        if (c.source) return c;
    }
    if (want.allowSeparate) {
        Choice c = separate(p, want);
        if (c.source) return c;
    }
    if (want.allowHls) {
        Choice c = hls(p);
        if (c.source) return c;
    }
    return progressive(p, want);
}

const CaptionTrack *captionFor(const Player &p, std::string_view language)
{
    const CaptionTrack *best = nullptr;
    std::tuple<bool, int> bestKey;                        // (manual, language match)
    for (const CaptionTrack &t : p.captions) {
        const int match = languageMatch(t.language, language);
        if (match == 0) continue;
        const std::tuple<bool, int> k(!t.autoGenerated, match);
        if (!best || k > bestKey) { best = &t; bestKey = k; }
    }
    return best;
}

const Cue *cueAt(const std::vector<Cue> &cues, int64_t ms)
{
    // The first cue that starts after `ms`; the one before it is the latest started.
    auto after = std::upper_bound(cues.begin(), cues.end(), ms,
                                  [](int64_t t, const Cue &c) { return t < c.startMs; });
    if (after == cues.begin()) return nullptr;
    const Cue &c = *(after - 1);
    return ms < c.endMs ? &c : nullptr;
}

StoryboardFrame storyboardFrame(const StoryboardLevel &l, int64_t ms)
{
    StoryboardFrame f;
    if (l.count <= 0 || l.cols <= 0 || l.rows <= 0) return f;
    int64_t idx = (l.intervalMs > 0 && ms > 0) ? ms / l.intervalMs : 0;
    if (idx >= l.count) idx = l.count - 1;
    const int64_t perSheet = (int64_t) l.cols * l.rows;
    const int64_t sheet = idx / perSheet, cell = idx % perSheet;
    f.width = l.width;
    f.height = l.height;
    f.x = (int) ((cell % l.cols) * l.width);         // 64-bit: a hand-built level may be huge
    f.y = (int) ((cell / l.cols) * l.height);
    f.url = l.urlTemplate;
    const size_t at = f.url.find("$M");
    if (at != std::string::npos) f.url.replace(at, 2, std::to_string(sheet));
    return f;
}

}
