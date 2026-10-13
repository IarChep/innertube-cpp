// The default HTTP backend: one libcurl multi handle driven on the engine thread.
// Its LoopDriver::wait() = curl_multi_perform + curl_multi_poll + completion dispatch,
// wake() = curl_multi_wakeup, so every sink call happens on the engine thread.
// The only place in the library that includes <curl/curl.h>.
#ifndef YT_BACKENDS_CURLTRANSPORT_H
#define YT_BACKENDS_CURLTRANSPORT_H
#include <yt/transport.h>
#include <curl/curl.h>
#include <memory>
#include <unordered_map>
#include <vector>
namespace yt::backends {

class CurlTransport final : public HttpTransport, public LoopDriver {
public:
    explicit CurlTransport(const TransportConfig &cfg);   // std::call_once(curl_global_init)
    ~CurlTransport() override;                             // removes + frees every easy; no sink calls
    CurlTransport(const CurlTransport &) = delete;
    CurlTransport &operator=(const CurlTransport &) = delete;

    uint64_t start(HttpRequest request, std::shared_ptr<HttpResponseSink> sink) override;
    void cancel(uint64_t id) override;                     // detach → sink->onDone(Canceled), async
    LoopDriver *driver() override { return this; }
    void wait(int maxWaitMs) override;
    void wake() override;

    struct Req;                                            // one easy handle + its sink
private:
    void drive();                                          // perform + dispatch completions
    void finish(Req *r, TransportStatus st);               // free the easy, then onDone
    void flushDeferred();

    TransportConfig m_cfg;
    CURLM *m_multi;
    uint64_t m_next = 0;
    std::unordered_map<uint64_t, std::unique_ptr<Req>> m_reqs;
    // Completions that must not run inside start()/cancel() (never re-entrant).
    std::vector<std::pair<std::shared_ptr<HttpResponseSink>, TransportStatus>> m_deferred;
};

}
#endif
