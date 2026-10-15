// Auth: the OAuth device-code flow + bearer lifecycle, all on the engine thread.
#include "engine.h"
#include "core/chains.h"

namespace yt {
namespace detail {

namespace {
// Google access tokens live ~1 h; when the grant omits expires_in, assume that.
constexpr int kDefaultTokenSecs = 3600;
// Refresh this long before the token expires, so an in-flight call never carries a
// bearer that dies mid-request.
constexpr int kRefreshSlackSecs = 60;
// Device-code lifetime when the grant omits expires_in (Google's is 30 min).
constexpr int kDefaultDeviceCodeSecs = 1800;
}

void Engine::ensureBearer(std::function<void()> run)
{
    if (!signedIn() || !http) { run(); return; }
    if (!http->session().bearer.empty() && now() + std::chrono::seconds(kRefreshSlackSecs) < bearerExpiry) {
        run();
        return;
    }
    afterRefresh.push_back(std::move(run));
    if (!refreshing) refreshBearer();
}

void Engine::refreshBearer()
{
    refreshing = true;
    const uint64_t gen = authGen;                    // the identity this refresh is for
    core::oauthRefresh(*http, state.refreshToken, core::newJob(), [this, gen](const core::TokenGrant &g) {
        refreshing = false;
        if (gen == authGen && g.error == "invalid_grant") {
            // Revoked refresh token (password change, "remove access"): drop the session,
            // or every authed call would refresh again and fail the same way. A sign-in in
            // progress is a new identity and carries on.
            dropIdentity();
            revokedGen = authGen;
        }
        // A sign-out / new sign-in happened meanwhile: the grant belongs to the old
        // identity and must not be installed.
        if (gen == authGen && !g.accessToken.empty() && http) {
            http->setBearer(g.accessToken);
            ++bearerGen;
            bearerExpiry = now() + std::chrono::seconds(g.expiresInSecs > 0 ? g.expiresInSecs : kDefaultTokenSecs);
        }
        // Run the waiting calls even when the refresh failed: they go out with the old
        // (or no) bearer and report the server's answer instead of hanging.
        std::vector<std::function<void()>> waiting;
        waiting.swap(afterRefresh);
        for (std::function<void()> &f : waiting) f();
    });
}

void Engine::startSignIn(const std::shared_ptr<JobHandle> &h, std::function<void(const DeviceCode &)> onCode,
                         Callback<Done> onDone)
{
    if (signInJob) signInJob->token->canceled = true;   // a new sign-in supersedes a pending one
    if (pollTimer) { loop.cancelTimer(pollTimer); pollTimer = 0; }
    signInJob = h;
    if (!http) return;
    core::oauthDeviceCode(*http, h->token, [this, h, onCode, onDone](const Result<core::DeviceCodeGrant> &r) {
        if (!r.ok()) {
            Result<Done> out; out.error = r.error;
            deliver(h, [onDone, out]() { onDone(out); });
            return;
        }
        deviceCode = r.value.deviceCode;
        pollIntervalSecs = r.value.intervalSecs > 0 ? r.value.intervalSecs : 5;
        signInDeadline = now() + std::chrono::seconds(r.value.expiresInSecs > 0 ? r.value.expiresInSecs
                                                                                 : kDefaultDeviceCodeSecs);
        DeviceCode dc;
        dc.verificationUrl = r.value.verificationUrl;
        dc.userCode = r.value.userCode;
        dc.intervalSecs = pollIntervalSecs;
        dc.expiresInSecs = r.value.expiresInSecs;
        // Polling starts now: the QR request must not eat into the code's lifetime.
        pollTimer = loop.after(pollIntervalSecs * 1000, [this, h, onDone]() { pollTimer = 0; pollToken(h, onDone); });
        core::fetchSignInQr(*http, dc.userCode, h->token, [this, h, onCode, dc](const Result<std::string> &qr) {
            if (signInJob != h) return;              // finished or superseded meanwhile
            DeviceCode withQr = dc;
            if (qr.ok()) withQr.qrPng = qr.value;
            deliver(h, [onCode, withQr]() { onCode(withQr); }, /*final*/ false);
        });
    });
}

void Engine::pollToken(const std::shared_ptr<JobHandle> &h, Callback<Done> onDone)
{
    if (!core::live(h->token) || !http || signInJob != h) return;
    core::oauthPollToken(*http, deviceCode, h->token, [this, h, onDone](const core::TokenGrant &g) {
        if (!core::live(h->token) || signInJob != h) return;
        if (!g.accessToken.empty() && g.refreshToken.empty()) {
            // Without a refresh token the new identity cannot be kept; installing its
            // bearer next to the previous account's refresh token would mix identities.
            deviceCode.clear();
            signInJob.reset();
            Result<Done> out;
            out.error = Error{ErrorCode::Api, "token grant carried no refresh token", g.httpStatus};
            deliver(h, [onDone, out]() { onDone(out); });
            return;
        }
        if (!g.accessToken.empty()) {
            ++authGen;
            ++bearerGen;
            http->setBearer(g.accessToken);
            bearerExpiry = now() + std::chrono::seconds(g.expiresInSecs > 0 ? g.expiresInSecs : kDefaultTokenSecs);
            state.refreshToken = g.refreshToken;
            // The real identity comes from Client::account(); this is the placeholder
            // single-account record the refresh token is filed under.
            Account placeholder;
            placeholder.id = "default";
            placeholder.name = "YouTube";
            state.account = placeholder;
            signedInFlag = signedIn();
            http->clearCache();                      // cached payloads belong to the previous identity
            deviceCode.clear();
            signInJob.reset();
            notifyState();
            deliver(h, [onDone]() { onDone(Result<Done>{}); });
            return;
        }
        // No OAuth verdict (network down, 5xx): keep polling while the code is valid —
        // the user may be authorizing on the other device right now.
        const bool transient = g.error.empty() && !g.transportOk && now() < signInDeadline;
        if (g.error == "authorization_pending" || g.error == "slow_down" || transient) {
            if (g.error == "slow_down") pollIntervalSecs += 5;   // RFC 8628 §3.5
            pollTimer = loop.after(pollIntervalSecs * 1000, [this, h, onDone]() { pollTimer = 0; pollToken(h, onDone); });
            return;
        }
        deviceCode.clear();
        signInJob.reset();
        Result<Done> out;
        out.error = Error{ErrorCode::Api,
                          !g.error.empty() ? g.error : (g.transportOk ? std::string("authorization failed") : g.transportError),
                          g.httpStatus};
        deliver(h, [onDone, out]() { onDone(out); });
    });
}

void Engine::signOut()
{
    if (signInJob) { signInJob->token->canceled = true; signInJob.reset(); }
    if (pollTimer) { loop.cancelTimer(pollTimer); pollTimer = 0; }
    dropIdentity();
}

void Engine::dropIdentity()
{
    ++authGen;                                       // an in-flight refresh is now stale
    ++bearerGen;
    state.refreshToken.clear();
    state.account.reset();
    signedInFlag = false;
    bearerExpiry = Clock::time_point{};
    if (http) {
        http->setBearer(std::string());
        http->clearCache();
    }
    notifyState();
}

} // namespace detail

Job Auth::signIn(std::function<void(const DeviceCode &)> onCode, Callback<Done> onDone)
{
    std::shared_ptr<detail::JobHandle> h = m_e->newHandle();
    detail::Engine *e = m_e;
    e->loop.post([e, h, onCode, onDone]() { e->startSignIn(h, onCode, onDone); });
    return Job(h);
}

void Auth::signOut()
{
    detail::Engine *e = m_e;
    e->loop.post([e]() { e->signOut(); });
}

bool Auth::signedIn() const { return m_e->signedInFlag.load(); }

}
