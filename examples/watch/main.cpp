// yt-watch — one thread, no event loop of your own: ownThread=false and the caller
// pumps Client::runOnce(). Three jobs run at once; callbacks arrive in runOnce.
//
//   yt-watch <videoId>     e.g. dQw4w9WgXcQ
//
// Shows: watch (video + related), votes (returnyoutubedislike), comments and their
// paging (Page::next handed to Client::more).
#include <yt/client.h>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <videoId>\n", argv[0]);
        return 2;
    }
    const std::string id = argv[1];

    yt::ClientOptions o;
    o.ownThread = false;               // callbacks run inside runOnce(), on this thread
    yt::Client client(o);
    int pending = 0, rc = 0;
    auto fail = [&](const yt::Error &e) { std::fprintf(stderr, "error: %s\n", e.message.c_str()); rc = 1; };

    ++pending;
    client.watch(id, [&](const yt::Result<yt::WatchResult> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        const yt::Video &v = r.value.primary;
        std::printf("%s\n  by %s · %s · %s\n", v.title.c_str(), v.channelName.c_str(), v.viewText.c_str(), v.date.c_str());
        std::printf("  %zu related, first: %s\n", r.value.related.size(),
                    r.value.related.empty() ? "-" : r.value.related.front().title.c_str());
    });

    ++pending;
    client.votes(id, [&](const yt::Result<yt::Votes> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        std::printf("votes: %lld up / %lld down\n", (long long) r.value.likes.value_or(-1),
                    (long long) r.value.dislikes.value_or(-1));
    });

    // Two pages of comments: the second request is started from the first's callback.
    int pagesLeft = 2;
    yt::Callback<yt::Page<yt::Comment>> onComments = [&](const yt::Result<yt::Page<yt::Comment>> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        if (r.value.disabled) { std::printf("comments are disabled\n"); return; }
        for (const yt::Comment &c : r.value.items) std::printf("  [%s] %s\n", c.author.c_str(), c.body.substr(0, c.body.find('\n')).c_str());   // first line
        if (--pagesLeft > 0 && r.value.next) {
            ++pending;
            client.more(r.value.next, onComments);
        }
    };
    ++pending;
    client.comments(id, onComments);

    while (pending > 0) client.runOnce(100);
    return rc;
}
