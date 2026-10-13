#include "engine.h"
namespace yt::detail {
// Signed-out engines (and, until the auth flow lands, every engine) run immediately.
void Engine::ensureBearer(std::function<void()> run) { run(); }
void Engine::refreshBearer() {}
}
