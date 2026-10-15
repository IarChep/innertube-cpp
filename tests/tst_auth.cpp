// yt::Auth — OAuth device-code sign-in, proactive bearer refresh, one retry on 401.
#include <doctest/doctest.h>
#include <yt/client.h>
#include <yt/testing.h>
#include "innertube/catalog.h"
#include "fakehttp.h"
#include "testutil.h"
#include <chrono>
#include <thread>

using namespace std::chrono;

namespace {
const std::string *formValue(const yt::core::Form &f, const char *key) {
    for (const auto &kv : f) if (kv.first == key) return &kv.second;
    return nullptr;
}
struct Rig {
    std::shared_ptr<FakeHttp> f = std::make_shared<FakeHttp>();
    std::vector<yt::SessionState> states;
    steady_clock::time_point t = steady_clock::now();
    std::unique_ptr<yt::Client> c;
    explicit Rig(std::string refreshToken = "") {
        yt::ClientOptions o;
        o.initial.refreshToken = std::move(refreshToken);
        o.onStateChanged = [this](const yt::SessionState &s) { states.push_back(s); };
        c = yt::testing::makeClientForTesting(o, f);
        yt::testing::setClock(*c, [this] { return t; });
    }
    void pump(int rounds = 8) { for (int i = 0; i < rounds; ++i) { c->runOnce(0); f->flush(); } }
    void pumpFor(int ms, const std::function<bool()> &until) {
        auto end = steady_clock::now() + milliseconds(ms);
        while (!until() && steady_clock::now() < end) { c->runOnce(20); f->flush(); }
    }
};
}

TEST_CASE("device flow: pending -> granted persists the refresh token") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl,
               "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1}");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"error\":\"authorization_pending\"}");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A\",\"refresh_token\":\"R\",\"expires_in\":3600}");
    yt::DeviceCode code; bool done = false; yt::Result<yt::Done> res;
    CHECK_FALSE(r.c->auth().signedIn());
    r.c->auth().signIn([&](const yt::DeviceCode &dc) { code = dc; },
                       [&](const yt::Result<yt::Done> &x) { res = x; done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK(res.ok());
    CHECK(code.verificationUrl == "V");
    CHECK(code.userCode == "U");
    CHECK(r.f->sentForm.size() == 3);                 // device code + 2 polls
    CHECK(r.f->session().bearer == "A");
    CHECK(r.c->auth().signedIn());
    REQUIRE_FALSE(r.states.empty());
    CHECK(r.states.back().refreshToken == "R");
    REQUIRE(r.states.back().account.has_value());
    CHECK(r.f->clearCount >= 1);                      // previous identity's cache dropped
}
TEST_CASE("device flow: a denied grant fails") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl, "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1}");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"error\":\"access_denied\"}");
    bool done = false; yt::Result<yt::Done> res;
    r.c->auth().signIn([](const yt::DeviceCode &) {}, [&](const yt::Result<yt::Done> &x) { res = x; done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    REQUIRE_FALSE(res.ok());
    CHECK(res.error->message == "access_denied");
    CHECK_FALSE(r.c->auth().signedIn());
}
TEST_CASE("refreshesExpiredBearerBeforeCall") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue("browse", "{}");
    bool called = false;
    r.c->browse({"FEsubscriptions", "", ""}, [&](const yt::Result<yt::VideoPage> &) { called = true; });
    r.c->runOnce(0);
    REQUIRE(r.f->sentForm.size() == 1);               // the refresh goes out first...
    CHECK(*formValue(r.f->sentForm[0].second, "grant_type") == "refresh_token");
    CHECK(*formValue(r.f->sentForm[0].second, "refresh_token") == "R");
    CHECK(r.f->sent.empty());                         // ...the browse waits for it
    r.pump();
    CHECK(r.f->sent.size() == 1);
    CHECK(r.f->session().bearer == "A1");
    CHECK(called);

    // Still fresh: no second refresh.
    r.f->queue("browse", "{}");
    r.c->browse({"FEhistory", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.pump();
    CHECK(r.f->sentForm.size() == 1);
    CHECK(r.f->sent.size() == 2);

    // An hour later the bearer is stale: refresh again before the call.
    r.t += seconds(3601);
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A2\",\"expires_in\":3600}");
    r.f->queue("browse", "{}");
    r.c->browse({"FEhistory", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.pump();
    CHECK(r.f->sentForm.size() == 2);
    CHECK(r.f->session().bearer == "A2");
}
TEST_CASE("refresh happens a minute before expiry") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue("browse", "{}");
    r.c->browse({"FEx", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.pump();
    r.t += seconds(3600 - 59);
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A2\",\"expires_in\":3600}");
    r.f->queue("browse", "{}");
    r.c->browse({"FEx", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.pump();
    CHECK(r.f->sentForm.size() == 2);
}
TEST_CASE("retries401Once") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue("browse", "{\"error\":{\"code\":401}}", 401);
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A2\",\"expires_in\":3600}");
    r.f->queue("browse", "{\"error\":{\"code\":401}}", 401);
    int calls = 0; yt::Result<yt::VideoPage> res;
    r.c->browse({"FEsubscriptions", "", ""}, [&](const yt::Result<yt::VideoPage> &x) { ++calls; res = x; });
    r.pump(20);
    CHECK(calls == 1);
    REQUIRE_FALSE(res.ok());
    CHECK(res.error->httpStatus == 401);
    CHECK(r.f->sentForm.size() == 2);                 // initial refresh + exactly one forced refresh
    CHECK(r.f->sent.size() == 2);                     // the call + exactly one retry
}
TEST_CASE("a 401 then success is delivered once, successfully") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue("browse", "{\"error\":{\"code\":401}}", 401);
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A2\",\"expires_in\":3600}");
    r.f->queue("browse", "{}");
    int calls = 0; yt::Result<yt::VideoPage> res;
    r.c->browse({"FEsubscriptions", "", ""}, [&](const yt::Result<yt::VideoPage> &x) { ++calls; res = x; });
    r.pump(20);
    CHECK(calls == 1);
    CHECK(res.ok());
    CHECK(r.f->session().bearer == "A2");
}
TEST_CASE("signed out: a 401 is delivered without refresh or retry") {
    Rig r;
    r.f->queue("browse", "{\"error\":{}}", 401);
    int calls = 0;
    r.c->browse({"FEx", "", ""}, [&](const yt::Result<yt::VideoPage> &) { ++calls; });
    r.pump();
    CHECK(calls == 1);
    CHECK(r.f->sentForm.empty());
    CHECK(r.f->sent.size() == 1);
}
TEST_CASE("concurrent authed calls share one refresh") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    for (int i = 0; i < 3; ++i) r.f->queue("browse", "{}");
    int calls = 0;
    for (int i = 0; i < 3; ++i)
        r.c->browse({"FE" + std::to_string(i), "", ""}, [&](const yt::Result<yt::VideoPage> &) { ++calls; });
    r.pump();
    CHECK(r.f->sentForm.size() == 1);
    CHECK(r.f->sent.size() == 3);
    CHECK(calls == 3);
}
TEST_CASE("signOut clears state") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue("browse", "{}");
    r.c->browse({"FEx", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.pump();
    CHECK(r.c->auth().signedIn());
    r.c->auth().signOut();
    r.pump();
    CHECK_FALSE(r.c->auth().signedIn());
    CHECK(r.f->session().bearer.empty());
    REQUIRE_FALSE(r.states.empty());
    CHECK(r.states.back().refreshToken.empty());
    CHECK_FALSE(r.states.back().account.has_value());
    r.f->queue("browse", "{}");
    r.c->browse({"FEx", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.pump();
    CHECK(r.f->sentForm.size() == 1);                 // signed out: no refresh any more
}
TEST_CASE("cancel signIn stops polling") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl, "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1}");
    bool gotCode = false, done = false;
    yt::Job j = r.c->auth().signIn([&](const yt::DeviceCode &) { gotCode = true; },
                                   [&](const yt::Result<yt::Done> &) { done = true; });
    r.pumpFor(2000, [&] { return gotCode; });
    REQUIRE(gotCode);
    CHECK(j.active());                                // the code is not the end of the flow
    j.cancel();
    r.pumpFor(1600, [] { return false; });
    CHECK(r.f->sentForm.size() == 1);                 // no token polls after cancel
    CHECK_FALSE(done);
}

// Security review + final review I3: a refresh still in flight when the identity
// changes must not reinstall the old account's bearer.
TEST_CASE("a refresh completing after signOut does not reinstall the old bearer") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue("browse", "{}");
    r.c->browse({"FEsubscriptions", "", ""}, [](const yt::Result<yt::VideoPage> &) {});
    r.c->runOnce(0);                                  // the refresh is now in flight
    REQUIRE(r.f->sentForm.size() == 1);
    r.c->auth().signOut();
    r.c->runOnce(0);                                  // sign-out lands first
    r.pump();                                         // then the stale grant arrives
    CHECK(r.f->session().bearer.empty());
    CHECK_FALSE(r.c->auth().signedIn());
}
// Security review: a grant without a refresh token used to keep the previous account's
// refresh token next to the new account's bearer — the next refresh flipped identity.
TEST_CASE("a grant without a refresh token fails and keeps the previous identity") {
    Rig r("R_A");
    r.f->queue(yt::Catalog::kDeviceCodeUrl, "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1}");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"B\",\"expires_in\":3600}");
    bool done = false; yt::Result<yt::Done> res;
    r.c->auth().signIn([](const yt::DeviceCode &) {}, [&](const yt::Result<yt::Done> &x) { res = x; done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK_FALSE(res.ok());
    CHECK(r.f->session().bearer != "B");
    CHECK(r.c->auth().signedIn());                    // still account A
    for (const yt::SessionState &s : r.states) CHECK(s.refreshToken == "R_A");
}

// Several calls rejected with the same stale bearer: only the first 401 forces a refresh;
// one that arrives after the new bearer is installed just retries with it.
TEST_CASE("a late 401 for an already-replaced bearer does not refresh again") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A2\",\"expires_in\":3600}");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A3\",\"expires_in\":3600}");
    r.f->queue("browse", "{\"error\":{}}", 401);
    r.f->queue("browse", "{\"error\":{}}", 401);
    for (int i = 0; i < 2; ++i) r.f->queue("browse", "{}");
    int ok = 0;
    for (int i = 0; i < 2; ++i)
        r.c->browse({"FEsubscriptions", "p" + std::to_string(i), ""},
                    [&](const yt::Result<yt::VideoPage> &x) { ok += x.ok(); });
    r.c->runOnce(0);
    REQUIRE(r.f->pending() == 1);                     // the initial refresh
    r.f->flushOne(0);                                 // A1 installed; both calls go out with it
    REQUIRE(r.f->pending() == 2);
    r.f->flushOne(0);                                 // call 0: 401 -> forced refresh in flight
    REQUIRE(r.f->pending() == 2);                     // [call 1's 401, the refresh]
    r.f->flushOne(1);                                 // A2 installed, call 0 retried
    r.f->flushOne(0);                                 // call 1's late 401 (sent with A1)
    r.pump();
    CHECK(ok == 2);
    CHECK(r.f->sentForm.size() == 2);                 // initial + one forced refresh, not two
    CHECK(r.f->session().bearer == "A2");
}

// The signed-in /player walk moves past a 401 on TVHTML5 to IOS; when nothing yields a
// source, the 401 must still reach the refresh-and-retry logic.
TEST_CASE("player: a 401 on the bearer's client refreshes and retries") {
    Rig r("R");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A1\",\"expires_in\":3600}");
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A2\",\"expires_in\":3600}");
    for (int run = 0; run < 2; ++run) {
        r.f->queue("player", "{}");                      // WEB: the visitorData seed (none comes)
        r.f->queue("player", "{}");                      // VISIONOS: nothing
        r.f->queue("player", "{}");                      // ANDROID_VR: nothing
        r.f->queue("player", "{\"error\":{}}", 401);   // TVHTML5 (no sts: the /tv fetch fails)
        r.f->queue("player", "{}");                      // IOS: nothing
    }
    int calls = 0; yt::Result<yt::Player> res;
    r.c->player({"vid"}, [&](const yt::Result<yt::Player> &p) { ++calls; res = p; });
    r.pump(30);
    CHECK(calls == 1);
    CHECK(r.f->sentForm.size() == 2);                 // initial + the forced refresh
    CHECK(r.f->sent.size() == 10);                    // the whole walk, retried once
    REQUIRE_FALSE(res.ok());
    CHECK(res.error->httpStatus == 401);
}

// A network blip while polling used to abort the whole sign-in.
TEST_CASE("device flow: a network error while polling keeps polling") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl,
               "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1,\"expires_in\":1800}");
    r.f->queue(yt::Catalog::kTokenUrl, "", 0);        // network down
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A\",\"refresh_token\":\"R\",\"expires_in\":3600}");
    bool done = false; yt::Result<yt::Done> res;
    r.c->auth().signIn([](const yt::DeviceCode &) {}, [&](const yt::Result<yt::Done> &x) { res = x; done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK(res.ok());
    CHECK(r.f->sentForm.size() == 3);                 // device code + failed poll + granted poll
    CHECK(r.c->auth().signedIn());
}
TEST_CASE("device flow: network errors past the code's expiry fail the sign-in") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl,
               "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1,\"expires_in\":60}");
    r.f->queue(yt::Catalog::kTokenUrl, "", 0);
    bool gotCode = false, done = false; yt::Result<yt::Done> res;
    r.c->auth().signIn([&](const yt::DeviceCode &) { gotCode = true; },
                       [&](const yt::Result<yt::Done> &x) { res = x; done = true; });
    r.pumpFor(2000, [&] { return gotCode; });
    r.t += minutes(2);                                // the device code has expired
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK_FALSE(res.ok());
    CHECK(res.error->message == "network down");
}

TEST_CASE("device flow delivers YouTube's QR image with the code") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl,
               "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1,\"expires_in\":1800}");
    r.f->queue("mdx/handoff", loadFixtureRaw("mdx_handoff.json"));
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A\",\"refresh_token\":\"R\",\"expires_in\":3600}");
    yt::DeviceCode code; int codes = 0; bool done = false;
    r.c->auth().signIn([&](const yt::DeviceCode &dc) { code = dc; ++codes; },
                       [&](const yt::Result<yt::Done> &) { done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK(codes == 1);
    CHECK(code.userCode == "U");
    CHECK(code.expiresInSecs == 1800);
    CHECK(code.qrPng.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
}
TEST_CASE("a failed QR request does not stop the sign-in") {
    Rig r;
    r.f->queue(yt::Catalog::kDeviceCodeUrl, "{\"device_code\":\"D\",\"user_code\":\"U\",\"verification_url\":\"V\",\"interval\":1}");
    r.f->queue("mdx/handoff", "{}", 500);
    r.f->queue(yt::Catalog::kTokenUrl, "{\"access_token\":\"A\",\"refresh_token\":\"R\"}");
    yt::DeviceCode code; int codes = 0; bool done = false; yt::Result<yt::Done> res;
    r.c->auth().signIn([&](const yt::DeviceCode &dc) { code = dc; ++codes; },
                       [&](const yt::Result<yt::Done> &x) { res = x; done = true; });
    r.pumpFor(5000, [&] { return done; });
    REQUIRE(done);
    CHECK(res.ok());
    CHECK(codes == 1);
    CHECK(code.qrPng.empty());
}
