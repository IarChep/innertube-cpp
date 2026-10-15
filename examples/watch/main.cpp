// yt-watch — one thread, no event loop of your own: ownThread=false and the caller
// pumps Client::runOnce(). Three jobs run at once; callbacks arrive in runOnce.
//
//   yt-watch <link>     e.g. dQw4w9WgXcQ or https://youtu.be/dQw4w9WgXcQ
//
// Shows: watch (video + related), votes (returnyoutubedislike), comments and their
// paging (Page::next handed to Client::more).
#include <yt/client.h>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <link>\n", argv[0]);
        return 2;
    }
    const std::string id = argv[1];

    yt::ClientOptions o;
    o.ownThread = false;               // callbacks run inside runOnce(), on this thread
    yt::Client client(o);
    int pending = 0, rc = 0;
    auto fail = [&](const yt::Error &e) { std::fprintf(stderr, "error: %s\n", e.message.c_str()); rc = 1; };

    ++pending;
    client.videos().get(id, [&](const yt::Result<yt::Video> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        const yt::Video &v = r.value;
        std::printf("%s\n  by %s · %s · %s\n", v.title.c_str(), v.channel.name.c_str(), v.viewText.c_str(), v.publishedText.c_str());
        std::printf("  %zu related, first: %s\n", v.related.items.size(),
                    v.related.items.empty() ? "-" : v.related.items.front().title.c_str());
    });

    ++pending;
    client.videos().votes(id, [&](const yt::Result<yt::Votes> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        std::printf("votes: %lld up / %lld down\n", (long long) r.value.likes.value_or(-1),
                    (long long) r.value.dislikes.value_or(-1));
    });

    // Two pages of comments: the second request is started from the first's callback.
    int pagesLeft = 2;
    yt::Callback<yt::Page<yt::CommentThread>> onComments = [&](const yt::Result<yt::Page<yt::CommentThread>> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        if (r.value.disabled) { std::printf("comments are disabled\n"); return; }
        for (const yt::CommentThread &t : r.value.items)                      // first line of each
            std::printf("  [%s] %s\n", t.comment.author.name.c_str(), t.comment.text.substr(0, t.comment.text.find('\n')).c_str());
        if (--pagesLeft > 0 && r.value.next) {
            ++pending;
            client.more(r.value.next, onComments);
        }
    };
    ++pending;
    client.comments().list(id, onComments);

    while (pending > 0) client.runOnce(100);
    return rc;
}
