// yt-cli — innertube-cpp with no framework at all: a main thread that waits on a
// queue the Client delivers into.
//
//   yt-cli feed <browseId>     e.g. FEnews_destination, UC-9-kyTW8ZkZNDHQJ6FgpwQ
//   yt-cli search <query>
//   yt-cli streams <videoId>
//   yt-cli signin              device-code login; state saved to ./yt-cli-state.json
//
// The saved state (visitorData, refresh token) is loaded on every run, so feeds that
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

struct SavedState { std::string hl = "en", gl = "US", visitorData, refreshToken; };
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
    }
    return s;
}

// The file holds the OAuth refresh token: owner-only, written to a temp file and
// renamed so a crash never leaves a half-written state behind.
void save(const yt::SessionState &s)
{
    SavedState out{s.hl, s.gl, s.visitorData, s.refreshToken};
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
        std::fprintf(stderr, "usage: %s feed <browseId> | search <query> | streams <videoId> | signin\n", argv[0]);
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
        client.player(arg, [&](const yt::PlayerInfo &p) {
            if (p.streams.ok()) {
                for (const yt::Stream &s : p.streams.value)
                    std::printf("%-6s %-10s %4dx%-4d audio=%d %s\n", s.id.c_str(), s.description.c_str(),
                                s.width, s.height, (int) s.hasAudio, s.mimeType.c_str());
            } else {
                rc = fail(*p.streams.error);
            }
            if (p.captions.ok())
                for (const yt::Subtitle &c : p.captions.value) std::printf("caption %s %s\n", c.language.c_str(), c.title.c_str());
            mq.finish();
        });
    } else if (cmd == "signin") {
        client.auth().signIn(
            [](const yt::DeviceCode &dc) {
                std::printf("Open %s and enter the code %s\n", dc.verificationUrl.c_str(), dc.userCode.c_str());
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
