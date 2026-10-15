// The engine behind yt::Client: loop + transport + policy layer + session state, plus
// the dispatch/delivery machinery that implements the public threading contract.
#ifndef YT_ENGINE_H
#define YT_ENGINE_H
#include <yt/client.h>
#include "core/ihttp.h"
#include "core/job.h"
#include "net/loop.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace yt::detail {

// Serializes deliveries against ~Client: a delivery runs only while `closed` is false,
// and the destructor flips it under the same (recursive — a callback may destroy the
// Client) lock, so no callback runs after the destructor returns.
struct Gate {
    std::recursive_mutex mx;
    std::atomic<bool> closed{false};                 // written under mx; read lock-free by Job::active
};

struct JobHandle {
    core::JobToken token = core::newJob();
    std::weak_ptr<Engine> engine;
    std::shared_ptr<Gate> gate;                      // closed = the Client is gone: never delivered
    std::atomic<bool> finished{false};
};

struct Engine : std::enable_shared_from_this<Engine> {
    using Clock = std::chrono::steady_clock;

    ClientOptions opts;
    net::Loop loop;
    std::unique_ptr<HttpTransport> transport;
    std::shared_ptr<core::IHttp> http;               // the policy layer (or an injected fake)
    std::string baseUrlOverride;                     // testing: loopback youtubei base
    std::thread thread;
    std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    SessionState state;                              // engine-thread copy of what consumers persist
    std::function<Clock::time_point()> now = [] { return Clock::now(); };
    bool torn = false;
    int runDepth = 0;                                // manual mode: nesting of Client::runOnce
    bool teardownPending = false;                    // ~Client ran inside runOnce: tear down on exit

    // --- auth (engine thread; see auth.cpp) ---
    Clock::time_point bearerExpiry{};
    uint64_t authGen = 0;                            // bumped on every identity change (sign-in/out)
    uint64_t bearerGen = 0;                          // bumped whenever a bearer is installed or dropped
    uint64_t revokedGen = 0;                         // authGen right after an invalid_grant sign-out
    bool refreshing = false;
    std::vector<std::function<void()>> afterRefresh;
    std::shared_ptr<JobHandle> signInJob;            // the device-code flow in progress
    std::string deviceCode;
    int pollIntervalSecs = 5;
    Clock::time_point signInDeadline{};             // the device code expires; polling stops
    uint64_t pollTimer = 0;
    std::atomic<bool> signedInFlag{false};           // readable from any thread (Auth::signedIn)

    // Builds and starts an engine (own thread or inline); throws what setup() throws.
    static std::shared_ptr<Engine> boot(std::shared_ptr<Engine> e);
    static std::unique_ptr<Client> makeClient(std::shared_ptr<Engine> e) { return std::unique_ptr<Client>(new Client(std::move(e))); }
    static Engine *of(Client &c) { return c.m_engine.get(); }
    static Job makeJob(std::shared_ptr<JobHandle> h) { return Job(std::move(h)); }
    std::shared_ptr<JobHandle> newHandle()
    {
        auto h = std::make_shared<JobHandle>();
        h->engine = weak_from_this();
        h->gate = gate;
        return h;
    }

    void setup();                                    // engine thread: transport, policy layer, session seed
    void teardown();                                 // engine thread: policy layer, then transport

    // Run `fn` through the consumer's executor, gated on the Client still existing and
    // the job still being live (gate checked ON the delivery thread).
    // final=false keeps the Job active (an intermediate callback such as onCode).
    void deliver(const std::shared_ptr<JobHandle> &h, std::function<void()> fn, bool final = true);
    void notifyState();                              // onStateChanged(state) through the executor

    // Post `chain` onto the engine thread; deliver its result to `cb`. Auth-aware: waits
    // for a fresh bearer when signed in, retries once on HTTP 401 (auth.cpp).
    template <class Arg>
    Job start(std::function<void(core::IHttp &, const core::JobToken &, std::function<void(const Arg &)>)> chain,
              std::function<void(const Arg &)> cb);

    // auth.cpp
    void ensureBearer(std::function<void()> run);
    void refreshBearer();
    bool signedIn() const { return !state.refreshToken.empty(); }
    void startSignIn(const std::shared_ptr<JobHandle> &h, std::function<void(const DeviceCode &)> onCode,
                     Callback<Done> onDone);
    void pollToken(const std::shared_ptr<JobHandle> &h, Callback<Done> onDone);
    void signOut();                                  // cancels a pending sign-in, then dropIdentity()
    void dropIdentity();                             // tokens, account, caches; onStateChanged
};


// ---- dispatch (client.cpp and groups.cpp instantiate it) ----

template <class T> long httpStatusOf(const Result<T> &r) { return r.error ? r.error->httpStatus : 0; }

template <class Arg>
using ChainOf = std::function<void(core::IHttp &, const core::JobToken &, std::function<void(const Arg &)>)>;

// One attempt of a chain. A signed-in 401 forces a bearer refresh and re-runs the
// chain exactly once (retried=true); every other result is delivered. The refresh is
// forced only while the bearer the attempt went out with is still installed: 401s of
// calls that raced an already-finished refresh just retry with the new one. Plain recursion
// through captures — no self-referencing closure that would leak when a canceled
// chain never calls back. `gen` is the authGen the call started under.
template <class Arg>
void runAttempt(Engine *e, std::shared_ptr<JobHandle> h, ChainOf<Arg> chain,
                std::function<void(const Arg &)> cb, bool retried, uint64_t gen)
{
    if (!core::live(h->token) || !e->http) return;
    // The refresh this attempt waited for found the refresh token revoked (invalid_grant):
    // fail it rather than run it signed out. Calls started after the revocation run as usual.
    if (e->revokedGen > gen) {
        Arg out;
        out.error = Error{ErrorCode::AuthRequired, "session revoked", 0};
        e->deliver(h, [cb, out]() { cb(out); });
        return;
    }
    const uint64_t sentWith = e->bearerGen;
    chain(*e->http, h->token, [e, h, chain, cb, retried, sentWith, gen](const Arg &r) {
        if (!retried && httpStatusOf(r) == 401 && e->signedIn() && core::live(h->token)) {
            if (e->bearerGen == sentWith) e->bearerExpiry = Engine::Clock::time_point{};
            e->ensureBearer([e, h, chain, cb, gen]() { runAttempt<Arg>(e, h, chain, cb, true, gen); });
            return;
        }
        e->deliver(h, [cb, r]() { cb(r); });
    });
}

template <class Arg>
Job Engine::start(std::function<void(core::IHttp &, const core::JobToken &, std::function<void(const Arg &)>)> chain,
                  std::function<void(const Arg &)> cb)
{
    std::shared_ptr<JobHandle> h = newHandle();
    Engine *e = this;
    loop.post([e, h, chain, cb]() {
        const uint64_t gen = e->authGen;
        e->ensureBearer([e, h, chain, cb, gen]() { runAttempt<Arg>(e, h, chain, cb, false, gen); });
    });
    return makeJob(h);
}

}
#endif
