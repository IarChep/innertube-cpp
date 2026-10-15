// Links against the installed library: the umbrella header, a pure helper and the Client.
#include <yt/yt.h>
#include <cstdio>

int main()
{
    const std::optional<yt::Link> l = yt::Link::parse("https://youtu.be/dQw4w9WgXcQ");
    yt::ClientOptions o;
    o.ownThread = false;
    o.transport = [](const yt::TransportConfig &) { return std::unique_ptr<yt::HttpTransport>(); };
    try {
        yt::Client client(o);                        // no transport: the constructor throws
    } catch (const std::exception &) {
    }
    std::printf("innertube %s, %s\n", yt::versionString(), l ? l->id.c_str() : "?");
    return l && l->id == "dQw4w9WgXcQ" ? 0 : 1;
}
