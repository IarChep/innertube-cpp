// A self-driven reference HTTP backend: every request runs one blocking
// curl_easy_perform on its own std::thread and calls the sink FROM THAT THREAD.
// It has no LoopDriver. Copy it as the template for wrapping a blocking or
// callback-based native API (WinHTTP, NSURLSession completion handlers, JNI).
#ifndef EXAMPLES_THREADEDTRANSPORT_H
#define EXAMPLES_THREADEDTRANSPORT_H
#include <yt/transport.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
namespace examples {

class ThreadedTransport final : public yt::HttpTransport {
public:
    explicit ThreadedTransport(const yt::TransportConfig &cfg);
    ~ThreadedTransport() override;                 // cancels everything, joins every thread
    uint64_t start(yt::HttpRequest request, std::shared_ptr<yt::HttpResponseSink> sink) override;
    void cancel(uint64_t id) override;             // checked by the transfer's progress callback

    struct Job {
        std::atomic<bool> canceled{false};
        std::atomic<bool> finished{false};
        std::thread thread;
    };
private:
    void reapFinished();                           // join threads whose transfer ended

    yt::TransportConfig m_cfg;
    std::mutex m_mx;
    std::map<uint64_t, std::shared_ptr<Job>> m_jobs;
    uint64_t m_next = 0;
};

}
#endif
