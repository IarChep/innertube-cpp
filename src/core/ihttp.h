// The transport seam the request chains (and FakeHttp) program against.
#ifndef YT_CORE_IHTTP_H
#define YT_CORE_IHTTP_H
#include <yt/json.h>
#include <yt/log.h>
#include <yt/result.h>
#include "core/job.h"
#include "core/pct.h"
#include "core/session.h"
#include "innertube/clientconfig.h"
#include "innertube/contextbuilder.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace yt::core {

// One finished request. body is never null and stays populated on !ok (OAuth reads
// its error code from a failed reply); visitorData is pre-extracted by the envelope.
// doc is the body's JSON index, built once by the transport for every JSON reply (post,
// postForm, get) that is well-formed — also an "error" one; null for fetch() and for a
// body that is not JSON. Parsers read json(), never the body again.
struct Reply {
    bool ok = false;
    std::shared_ptr<const std::string> body = std::make_shared<std::string>();
    std::shared_ptr<const json::Doc> doc;
    ErrorCode code = ErrorCode::Transport;   // meaningful when !ok
    std::string error;
    long httpStatus = 0;
    std::string visitorData;
    json::Value json() const { return doc ? doc->root() : json::Value(); }
};
using HttpFn = std::function<void(const Reply &)>;

// Callbacks are invoked at most once, never re-entrantly from inside the call that
// started them, and on the implementation's (engine) thread. A canceled JobToken
// suppresses delivery.
class IHttp {
public:
    virtual ~IHttp() = default;
    virtual void post(std::string_view endpoint, ClientId, std::string bodyJson, const JobToken &, HttpFn) = 0;
    virtual void postForm(std::string url, Form fields, const JobToken &, HttpFn) = 0;
    virtual void get(std::string url, const JobToken &, HttpFn) = 0;
    // Raw GET for non-JSON resources (the /tv page, base.js, a QR image, json3): no
    // envelope check, but a non-2xx answer still fails. Identical (url, headers) requests
    // in flight are coalesced. `headers` must not carry credentials.
    virtual void fetch(std::string url, std::vector<Header> headers, const JobToken &, HttpFn) = 0;
    // Drops the visitorData a bot check flagged; the next youtubei reply seeds a fresh one.
    virtual void resetVisitorData() = 0;
    // Adopts the base.js identity (persisted by the engine like visitorData).
    virtual void setPlayerJs(std::string playerId, int signatureTimestamp) = 0;
    // Drop `job`'s waiters; cancel exchanges left without any. Call after canceling the token.
    virtual void abort(const JobToken &) = 0;
    // Read freely; mutate only hl/gl/visitorData and then clearCache() (memoized context).
    virtual Session &session() = 0;
    virtual void clearCache() = 0;                    // response cache + context/header memo
    virtual void setBearer(std::string bearer) = 0;   // header memo only (same-identity refresh)
    virtual const Logger &log() const = 0;
};

}
#endif
