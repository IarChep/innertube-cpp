// FakeHttp — a core::IHttp for the chain tests (std port of MeeTube's testutil.h).
// Every call records what it sent and defers its reply; flush() delivers to
// completion (a callback that posts the next chain step is drained by the same
// flush()), skipping waiters whose JobToken died — the gate the real HttpClient
// applies. Queued replies are raw bodies (JSON, or HTML/JS for fetch()); a queued status >= 400 fails the reply
// (code Api, httpStatus kept, body kept); status 0 is a network failure (no body).
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
    // `visitor`: the visitorData the reply carries; adopted on delivery when the session
    // has none (what HttpClient::complete does with responseContext.visitorData).
    void queue(const std::string &key, std::string body, long status = 200, std::string visitor = "")
    {
        m_q[key].push_back({std::move(body), status, std::move(visitor)});
    }
    std::vector<std::string> sent;                                   // raw JSON bodies posted
    std::vector<std::pair<std::string, yt::core::Form>> sentForm;    // (url, fields) of postForm
    std::vector<int> sentClients;                                    // ClientId of every post, in order
    struct Fetched { std::string url; std::vector<yt::Header> headers; };
    std::vector<Fetched> fetched;                                    // every fetch(), in order
    int visitorResets = 0, playerJsSets = 0;
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
        sentClients.push_back((int) client);
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
    void fetch(std::string url, std::vector<yt::Header> headers, const yt::core::JobToken &job,
               yt::core::HttpFn done) override
    {
        fetched.push_back({url, std::move(headers)});
        enqueue(url, job, std::move(done));
    }
    void resetVisitorData() override { m_session.visitorData.clear(); ++visitorResets; }
    void setPlayerJs(std::string id, int sts) override
    {
        m_session.playerId = std::move(id);
        m_session.signatureTimestamp = sts;
        ++playerJsSets;
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
            for (Waiter &w : batch) deliver(w);
        }
    }
    // Delivers only the i-th pending reply: lets a test reorder replies in flight.
    void flushOne(size_t i)
    {
        Waiter w = std::move(m_pending.at(i));
        m_pending.erase(m_pending.begin() + (long) i);
        deliver(w);
    }
    size_t pending() const { return m_pending.size(); }
    bool idle() const { return m_pending.empty(); }
    int clearCount = 0;

private:
    struct Queued { std::string body; long status; std::string visitor; };
    struct Waiter { yt::core::JobToken job; yt::core::HttpFn fn; yt::core::Reply reply; };
    void deliver(Waiter &w)
    {
        if (!yt::core::live(w.job)) return;
        if (w.reply.ok && m_session.visitorData.empty() && !w.reply.visitorData.empty())
            m_session.visitorData = w.reply.visitorData;
        w.fn(w.reply);
    }
    void enqueue(const std::string &key, const yt::core::JobToken &job, yt::core::HttpFn done)
    {
        yt::core::Reply r;
        auto it = m_q.find(key);
        if (it != m_q.end() && !it->second.empty()) {
            Queued q = std::move(it->second.front());
            it->second.pop_front();
            r.httpStatus = q.status;
            r.visitorData = q.visitor;
            r.body = std::make_shared<const std::string>(std::move(q.body));
            if (q.status == 0) { r.code = yt::ErrorCode::Transport; r.error = "network down"; r.body = std::make_shared<const std::string>(); }
            else if (q.status >= 400) { r.code = yt::ErrorCode::Api; r.error = "HTTP " + std::to_string(q.status); }
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
