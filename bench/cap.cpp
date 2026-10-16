// cap <endpoint> <client> <json> : prints the raw InnerTube answer (corpus capture).
#include <yt/yt.h>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    const char *names[] = {"WEB","WEB_SAFARI","IOS","ANDROID","ANDROID_VR","TVHTML5","VISIONOS"};
    int id = -1;
    for (int i = 0; i < 7; ++i) if (!std::strcmp(argv[2], names[i])) id = i;
    if (id < 0) return 2;
    yt::Client c;
    auto r = c.raw().post(argv[1], (yt::ClientId) id, argv[3]).get();
    if (!r.ok()) { std::fprintf(stderr, "error %ld %s\n", r.error->httpStatus, r.error->message.c_str()); return 1; }
    std::fwrite(r.value.data(), 1, r.value.size(), stdout);
}
