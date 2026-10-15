// yt-suggest — typeahead with cancellation. Each "keystroke" starts a suggestions
// request and cancels the previous one: a canceled Job's callback never runs, so only
// the last prefix prints, however the responses race.
//
//   yt-suggest <query>     e.g. кот
#include <yt/client.h>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <query>\n", argv[0]);
        return 2;
    }
    const std::string query = argv[1];

    yt::ClientOptions o;
    o.ownThread = false;
    yt::Client client(o);
    yt::Job inFlight;
    bool done = false;
    int rc = 0;

    // One prefix per UTF-8 code point (skip continuation bytes 10xxxxxx).
    for (size_t n = 1; n <= query.size(); ++n) {
        if (n < query.size() && (query[n] & 0xC0) == 0x80) continue;
        const std::string prefix = query.substr(0, n);
        inFlight.cancel();
        inFlight = client.search().suggestions(prefix, [&, prefix](const yt::Result<std::vector<std::string>> &r) {
            done = true;
            if (!r.ok()) { std::fprintf(stderr, "error: %s\n", r.error->message.c_str()); rc = 1; return; }
            std::printf("\"%s\":\n", prefix.c_str());
            for (const std::string &s : r.value) std::printf("  %s\n", s.c_str());
        });
        client.runOnce(30);   // let the request get going, as a typing user would
    }

    while (!done) client.runOnce(100);
    return rc;
}
