// Test support for consumers' unit tests. Not a stable API.
#ifndef YT_TESTING_H
#define YT_TESTING_H
#include <yt/client.h>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
namespace yt::core { class IHttp; }
namespace yt::testing {
// A Client whose transport layer is `http` (e.g. a FakeHttp). Forces ownThread=false:
// drive it with runOnce() on the test thread.
std::unique_ptr<Client> makeClientForTesting(ClientOptions options, std::shared_ptr<core::IHttp> http);
// A Client over the real policy layer + the configured backend, with its youtubei base
// URL pointed at `baseUrl` (a loopback server). ownThread is kept as given.
std::unique_ptr<Client> makeClientForTesting(ClientOptions options, std::string baseUrl);
// Replace the engine's monotonic clock (token-expiry tests). Call before any request.
void setClock(Client &client, std::function<std::chrono::steady_clock::time_point()> now);
}
#endif
