// yt-channel — chained requests: resolve a channel URL, then list its uploads.
//
//   yt-channel <url>     e.g. https://www.youtube.com/@NASA
//
// A browse of the channel id (UC…) returns its uploads as a VideoPage.
#include <yt/client.h>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <channel url>\n", argv[0]);
        return 2;
    }
    yt::ClientOptions o;
    o.ownThread = false;
    yt::Client client(o);
    bool done = false;
    int rc = 0;
    auto fail = [&](const yt::Error &e) { std::fprintf(stderr, "error: %s\n", e.message.c_str()); rc = 1; done = true; };

    client.channelByUrl(argv[1], [&](const yt::Result<yt::Channel> &r) {
        if (!r.ok()) return fail(*r.error);
        const yt::Channel &c = r.value;
        std::printf("%s (%s) %s · %s · %s\n", c.name.c_str(), c.id.c_str(), c.handle.c_str(),
                    c.subscriberCount.c_str(), c.videoCount.c_str());
        client.browse({c.id, "", ""}, [&](const yt::Result<yt::VideoPage> &p) {
            if (!p.ok()) return fail(*p.error);
            for (const yt::Video &v : p.value.items)
                std::printf("  %-12s %-8s %s\n", v.id.c_str(), v.duration.c_str(), v.title.c_str());
            done = true;
        });
    });

    while (!done) client.runOnce(100);
    return rc;
}
