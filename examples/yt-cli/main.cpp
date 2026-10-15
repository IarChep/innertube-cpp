// yt-cli — innertube-cpp with no framework at all: a main thread that waits on a
// queue the Client delivers into.
//
//   yt-cli feed <browseId>     e.g. FEnews_destination, UC-9-kyTW8ZkZNDHQJ6FgpwQ
//   yt-cli search <query>
//   yt-cli streams <videoId>
//   yt-cli captions <videoId> [lang]
//   yt-cli signin              device-code login; state saved to ./yt-cli-state.json
//
// The saved state (visitorData, refresh token, player build) is loaded on every run, so feeds that
// need a session (FEsubscriptions, FEhistory) work after `signin`.
#include <yt/client.h>
#include <glaze/glaze.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>

namespace {

// Callbacks are posted here and run on the main thread.
struct MainQueue {
    std::mutex mx;
    std::condition_variable cv;
    std::deque<std::function<void()>> q;
    bool done = false;
    yt::Executor executor()
    {
        return [this](std::function<void()> fn) {
            { std::lock_guard<std::mutex> l(mx); q.push_back(std::move(fn)); }
            cv.notify_one();
        };
    }
    void finish() { done = true; }
    void run()
    {
        while (!done) {
            std::function<void()> fn;
            {
                std::unique_lock<std::mutex> l(mx);
                cv.wait(l, [&] { return !q.empty(); });
                fn = std::move(q.front());
                q.pop_front();
            }
            fn();
        }
    }
};

struct SavedState { std::string hl = "en", gl = "US", visitorData, refreshToken, playerId; int signatureTimestamp = 0; };
const char *kStateFile = "yt-cli-state.json";

yt::SessionState load()
{
    yt::SessionState s;
    std::ifstream f(kStateFile);
    if (!f) return s;
    std::stringstream ss; ss << f.rdbuf();
    SavedState saved;
    if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(saved, ss.str())) {
        s.hl = saved.hl; s.gl = saved.gl; s.visitorData = saved.visitorData; s.refreshToken = saved.refreshToken;
        s.playerId = saved.playerId; s.signatureTimestamp = saved.signatureTimestamp;
    }
    return s;
}

// The file holds the OAuth refresh token: owner-only, written to a temp file and
// renamed so a crash never leaves a half-written state behind.
void save(const yt::SessionState &s)
{
    SavedState out{s.hl, s.gl, s.visitorData, s.refreshToken, s.playerId, s.signatureTimestamp};
    const std::string json = glz::write_json(out).value_or("{}");
    const std::string tmp = std::string(kStateFile) + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    ::fchmod(fd, 0600);                              // also when the file already existed
    const bool ok = ::write(fd, json.data(), json.size()) == (ssize_t) json.size();
    ::close(fd);
    if (ok) std::rename(tmp.c_str(), kStateFile); else std::remove(tmp.c_str());
}

int fail(const yt::Error &e)
{
    std::fprintf(stderr, "error: %s\n", e.message.c_str());
    return 1;
}

void printVideos(const yt::VideoPage &p)
{
    for (const yt::Video &v : p.items)
        std::printf("%-12s %-8s %s — %s\n", v.id.c_str(), v.duration.c_str(), v.title.c_str(), v.channelName.c_str());
    if (!p.next.empty()) std::printf("(more: continuation %zu bytes)\n", p.next.size());
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s feed <browseId> | search <query> | streams <videoId> | captions <videoId> [lang] | signin\n", argv[0]);
        return 2;
    }
    const std::string cmd = argv[1];
    const std::string arg = argc > 2 ? argv[2] : "";

    MainQueue mq;
    yt::ClientOptions o;
    o.deliver = mq.executor();
    o.initial = load();
    o.onStateChanged = save;
    yt::Client client(o);
    int rc = 0;

    if (cmd == "feed") {
        client.browse({arg, "", ""}, [&](const yt::Result<yt::VideoPage> &r) {
            if (r.ok()) printVideos(r.value); else rc = fail(*r.error);
            mq.finish();
        });
    } else if (cmd == "search") {
        client.search({arg, "relevance"}, [&](const yt::Result<yt::VideoPage> &r) {
            if (r.ok()) printVideos(r.value); else rc = fail(*r.error);
            mq.finish();
        });
    } else if (cmd == "streams") {
        client.player({arg}, [&](const yt::Result<yt::Player> &r) {
            if (!r.ok()) { rc = fail(*r.error); mq.finish(); return; }
            const yt::Player &p = r.value;
            std::printf("%s — %s [%s%s%s]\n", p.details.title.c_str(), p.details.author.c_str(), p.status.status.c_str(),
                        p.status.reason.empty() ? "" : ": ", p.status.reason.c_str());
            for (const yt::Source &s : p.sources) {
                std::printf("source %s  hls=%d sabr=%d  UA %s\n", yt::defaultClientInfo(s.client).name.c_str(),
                            (int) !s.hlsUrl.empty(), (int) !s.sabrUrl.empty(), s.userAgent.c_str());
                for (const yt::Format &f : s.formats)
                    std::printf("  %-4d %-3s %-8s %-28s %-8s %s\n", f.itag,
                                f.hasVideo && f.hasAudio ? "av" : f.hasVideo ? "v" : "a",
                                f.qualityLabel.c_str(), f.codecs.c_str(),
                                f.audio ? f.audio->id.c_str() : "", f.url.empty() ? "(no url)" : f.url.c_str());
            }
            for (const yt::CaptionTrack &c : p.captions)
                std::printf("caption %s %s%s\n", c.language.c_str(), c.name.c_str(), c.autoGenerated ? " (asr)" : "");
            std::printf("storyboard levels: %zu\n", p.storyboard.size());
            mq.finish();
        });
    } else if (cmd == "captions") {
        const std::string lang = argc > 3 ? argv[3] : "";
        client.player({arg, 0}, [&](const yt::Result<yt::Player> &r) {
            if (!r.ok() || r.value.captions.empty()) {
                if (r.ok()) std::printf("no captions\n"); else rc = fail(*r.error);
                mq.finish();
                return;
            }
            yt::CaptionTrack pick = r.value.captions.front();
            for (const yt::CaptionTrack &c : r.value.captions) if (c.language == lang) { pick = c; break; }
            client.captions(pick, [&](const yt::Result<std::vector<yt::Cue>> &cues) {
                if (!cues.ok()) rc = fail(*cues.error);
                else for (size_t i = 0; i < cues.value.size() && i < 8; ++i)
                    std::printf("%7.2f %s\n", cues.value[i].startMs / 1000.0, cues.value[i].text.c_str());
                mq.finish();
            });
        });
    } else if (cmd == "signin") {
        client.auth().signIn(
            [](const yt::DeviceCode &dc) {
                std::printf("Open %s and enter the code %s\n", dc.verificationUrl.c_str(), dc.userCode.c_str());
                if (!dc.qrPng.empty()) {
                    std::ofstream("qr.png", std::ios::binary) << dc.qrPng;
                    std::printf("or scan ./qr.png\n");
                }
                std::fflush(stdout);
            },
            [&](const yt::Result<yt::Done> &r) {
                if (r.ok()) std::printf("signed in; state saved to %s\n", kStateFile); else rc = fail(*r.error);
                mq.finish();
            });
    } else {
        std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
        return 2;
    }
    mq.run();
    return rc;
}
