// yt-channel — one link, two requests in flight: the channel header and its Videos tab.
//
//   yt-channel <link>     e.g. https://www.youtube.com/@NASA, @NASA or UC…
#include <yt/client.h>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <channel link>\n", argv[0]);
        return 2;
    }
    yt::ClientOptions o;
    o.ownThread = false;
    yt::Client client(o);
    int pending = 2, rc = 0;
    auto fail = [&](const yt::Error &e) { std::fprintf(stderr, "error: %s\n", e.message.c_str()); rc = 1; };

    // Two independent requests: the header and the Videos tab both accept the same link.
    client.channels().get(argv[1], [&](const yt::Result<yt::Channel> &r) {
        --pending;
        if (!r.ok()) return fail(*r.error);
        const yt::Channel &c = r.value;
        std::printf("%s (%s) %s · %s · %s\n", c.name.c_str(), c.id.c_str(), c.handle.c_str(),
                    c.subscriberCount.c_str(), c.videoCount.c_str());
    });
    client.channels().videos(argv[1], [&](const yt::Result<yt::Page<yt::Video>> &p) {
        --pending;
        if (!p.ok()) return fail(*p.error);
        for (const yt::Video &v : p.value.items)
            std::printf("  %-12s %-8s %s\n", v.id.c_str(), v.duration.c_str(), v.title.c_str());
    });

    while (pending > 0) client.runOnce(100);
    return rc;
}
