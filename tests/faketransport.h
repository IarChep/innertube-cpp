// FakeTransport — a scripted, socket-free yt::HttpTransport for the policy tests.
//
// Inline mode: start() only records; complete() delivers every started exchange
// that is not hanging (so "never re-entrant" stays observable). Threaded mode:
// each start() delivers from its own std::thread, joined in the destructor.
// A `hang` exchange never completes on its own; cancel() ends it with
// onDone(Canceled). Unscripted URLs answer 404 with an empty body.
#ifndef IT_FAKETRANSPORT_H
#define IT_FAKETRANSPORT_H
#include <yt/transport.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct FakeExchange {                      // one scripted response
    int status = 200;
    std::vector<yt::HttpHeader> headers;
    std::vector<std::string> chunks;       // delivered as separate onBody calls
    yt::TransportStatus end;               // default: None
    bool hang = false;                     // never complete (timeouts, cancel)
};

class FakeTransport : public yt::HttpTransport {
public:
    enum class Mode { Inline, Threaded };
    explicit FakeTransport(Mode m = Mode::Inline) : m_mode(m) {}
    ~FakeTransport() override {
        { std::lock_guard<std::mutex> l(m_mx); m_closing = true; }
        m_cv.notify_all();
        for (std::thread &t : m_threads) t.join();
    }

    // FIFO per prefix; the longest matching prefix wins.
    void script(const std::string &urlPrefix, FakeExchange x) {
        std::lock_guard<std::mutex> l(m_mx);
        m_scripts[urlPrefix].push_back(std::move(x));
    }

    uint64_t start(yt::HttpRequest r, std::shared_ptr<yt::HttpResponseSink> s) override {
        std::unique_lock<std::mutex> l(m_mx);
        const uint64_t id = ++m_next;
        Pending p{id, std::move(s), pick(r.url)};
        started.push_back(std::move(r));
        if (m_mode == Mode::Inline) {
            m_pending.push_back(std::move(p));
        } else {
            m_threads.emplace_back([this, p]() { runThreaded(p); });
        }
        return id;
    }

    void cancel(uint64_t id) override {
        { std::lock_guard<std::mutex> l(m_mx); canceled.push_back(id); m_canceledSet.insert(id); }
        m_cv.notify_all();
    }

    // Inline mode: deliver everything started so far (and whatever those deliveries
    // start in turn), leaving only live hanging exchanges pending.
    void complete() {
        for (;;) {
            std::vector<Pending> ready;
            {
                std::lock_guard<std::mutex> l(m_mx);
                std::vector<Pending> keep;
                for (Pending &p : m_pending) {
                    if (p.x.hang && !m_canceledSet.count(p.id)) keep.push_back(std::move(p));
                    else ready.push_back(std::move(p));
                }
                m_pending.swap(keep);
            }
            if (ready.empty()) return;
            for (Pending &p : ready) deliver(p);
        }
    }

    std::vector<yt::HttpRequest> started;  // every request, in start order
    std::vector<uint64_t> canceled;

private:
    struct Pending { uint64_t id; std::shared_ptr<yt::HttpResponseSink> sink; FakeExchange x; };

    FakeExchange pick(const std::string &url) {         // m_mx held
        std::string best;
        bool found = false;
        for (auto &kv : m_scripts)
            if (!kv.second.empty() && url.compare(0, kv.first.size(), kv.first) == 0
                && (!found || kv.first.size() > best.size())) { best = kv.first; found = true; }
        if (!found) { FakeExchange nf; nf.status = 404; return nf; }
        FakeExchange x = m_scripts[best].front();
        m_scripts[best].erase(m_scripts[best].begin());
        return x;
    }
    bool isCanceled(uint64_t id) { std::lock_guard<std::mutex> l(m_mx); return m_canceledSet.count(id) != 0; }

    void deliver(const Pending &p) {
        const yt::TransportStatus canceledSt{yt::TransportError::Canceled, "canceled"};
        if (isCanceled(p.id)) { p.sink->onDone(canceledSt); return; }
        p.sink->onHead(p.x.status, p.x.headers);
        for (const std::string &c : p.x.chunks) {
            if (isCanceled(p.id)) { p.sink->onDone(canceledSt); return; }
            if (!p.sink->onBody(c.data(), c.size())) { p.sink->onDone(canceledSt); return; }
        }
        p.sink->onDone(p.x.end);
    }

    void runThreaded(const Pending &p) {
        if (p.x.hang) {
            std::unique_lock<std::mutex> l(m_mx);
            m_cv.wait(l, [&] { return m_closing || m_canceledSet.count(p.id) != 0; });
            if (m_closing) return;                       // destructor: no sink call
            l.unlock();
            p.sink->onDone({yt::TransportError::Canceled, "canceled"});
            return;
        }
        { std::lock_guard<std::mutex> l(m_mx); if (m_closing) return; }
        deliver(p);
    }

    Mode m_mode;
    std::mutex m_mx;
    std::condition_variable m_cv;
    std::map<std::string, std::vector<FakeExchange>> m_scripts;
    std::vector<Pending> m_pending;
    std::set<uint64_t> m_canceledSet;
    std::vector<std::thread> m_threads;
    uint64_t m_next = 0;
    bool m_closing = false;
};
#endif
