// What a consumer persists between runs: seeded via ClientOptions::initial and
// reported back through ClientOptions::onStateChanged. C++17.
#ifndef YT_SESSION_H
#define YT_SESSION_H
#include <optional>
#include <string>
#include <yt/types.h>
namespace yt {
struct SessionState {
    std::string hl = "en", gl = "US";     // InnerTube locale
    std::string visitorData;              // persisted anonymous identity
    std::string refreshToken;             // OAuth; empty = signed out
    std::string playerId;                 // YouTube's player build (rotates every few weeks)
    int signatureTimestamp = 0;           // read from that build; spares a 3 MB download per run
    std::optional<Account> account;       // identity of the signed-in user
};
}
#endif
