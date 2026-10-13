// FakeHttp — a core::IHttp for the chain tests (std port of MeeTube's testutil.h).
// Every call records what it sent and defers its reply; flush() delivers to
// completion (a callback that posts the next chain step is drained by the same
// flush()), skipping waiters whose JobToken died — the gate the real HttpClient
// applies. Queued replies are raw JSON; a queued status >= 400 fails the reply
// (code Api, httpStatus kept, body kept).
#ifndef IT_FAKEHTTP_H
#define IT_FAKEHTTP_H
#include "core/ihttp.h"
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

class FakeHttp : public yt::core::IHttp {
public:
    void queue(const std::string &key, std::string body, long status = 200)
    {
        m_q[key].push_back({std::move(body), status});
    }
    std::vector<std::string> sent;                                   // raw JSON bodies posted
    std::vector<std::pair<std::string, yt::core::Form>> sentForm;    // (url, fields) of postForm
    // The ClientId each endpoint was last posted with (recorded at post() time); -1 if never.
    int lastClientFor(const std::string &endpoint) const
    {
        auto it = m_lastClient.find(endpoint);
        return it == m_lastClient.end() ? -1 : it->second;
    }

    void post(std::string_view endpoint, yt::ClientId client, std::string body,
              const yt::core::JobToken &job, yt::core::HttpFn done) override
    {
        m_lastClient[std::string(endpoint)] = (int) client;
        sent.push_back(std::move(body));
        enqueue(std::string(endpoint), job, std::move(done));
    }
    void postForm(std::string url, yt::core::Form fields, const yt::core::JobToken &job, yt::core::HttpFn done) override
    {
        sentForm.push_back({url, std::move(fields)});
        enqueue(url, job, std::move(done));
    }
    // get(): records the URL; replies with the body armed by setGetBody(), else fails.
    void setGetBody(std::string b) { m_getBody = std::move(b); m_haveGetBody = true; }
    std::string lastGetUrl() const { return m_lastGetUrl; }
    void get(std::string url, const yt::core::JobToken &job, yt::core::HttpFn done) override
    {
        m_lastGetUrl = url;
        yt::core::Reply r;
        if (m_haveGetBody) {
            r.ok = true;
            r.httpStatus = 200;
            r.body = std::make_shared<const std::string>(m_getBody);
        } else {
            r.error = "no get fixture";
        }
        m_pending.push_back({job, std::move(done), r});
    }
    void abort(const yt::core::JobToken &) override {}              // flush() honours the token gate
    yt::Session &session() override { return m_session; }
    void clearCache() override { ++clearCount; }
    void setBearer(std::string bearer) override { m_session.bearer = std::move(bearer); }
    const yt::Logger &log() const override { return m_log; }

    void flush()
    {
        while (!m_pending.empty()) {
            std::vector<Waiter> batch;
            batch.swap(m_pending);
            for (Waiter &w : batch) {
                if (!yt::core::live(w.job)) continue;
                w.fn(w.reply);
            }
        }
    }
    bool idle() const { return m_pending.empty(); }
    int clearCount = 0;

private:
    struct Queued { std::string body; long status; };
    struct Waiter { yt::core::JobToken job; yt::core::HttpFn fn; yt::core::Reply reply; };
    void enqueue(const std::string &key, const yt::core::JobToken &job, yt::core::HttpFn done)
    {
        yt::core::Reply r;
        auto it = m_q.find(key);
        if (it != m_q.end() && !it->second.empty()) {
            Queued q = std::move(it->second.front());
            it->second.pop_front();
            r.httpStatus = q.status;
            r.body = std::make_shared<const std::string>(std::move(q.body));
            if (q.status >= 400) { r.code = yt::ErrorCode::Api; r.error = "HTTP " + std::to_string(q.status); }
            else r.ok = true;
        } else {
            r.error = "no fixture queued for " + key;
        }
        m_pending.push_back({job, std::move(done), r});
    }
    std::map<std::string, std::deque<Queued>> m_q;
    std::vector<Waiter> m_pending;
    std::map<std::string, int> m_lastClient;
    yt::Session m_session;
    yt::Logger m_log;
    std::string m_getBody;
    bool m_haveGetBody = false;
    std::string m_lastGetUrl;
};
#endif
