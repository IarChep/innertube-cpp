#include "threadedtransport.h"
#include <curl/curl.h>
#include <string>
#include <vector>

namespace examples {
namespace {

struct Transfer {
    std::shared_ptr<yt::HttpResponseSink> sink;
    std::shared_ptr<ThreadedTransport::Job> job;
    int status = 0;
    std::vector<yt::HttpHeader> headers;
    bool headDelivered = false;
    bool refused = false;
};

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

void deliverHead(Transfer *t)
{
    if (t->headDelivered) return;
    t->headDelivered = true;
    t->sink->onHead(t->status, std::move(t->headers));
}

size_t headerCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    Transfer *t = static_cast<Transfer *>(userp);
    const std::string line = trim(std::string(ptr, n));
    if (line.rfind("HTTP/", 0) == 0) {
        const size_t sp = line.find(' ');
        t->status = (sp == std::string::npos) ? 0 : std::atoi(line.c_str() + sp + 1);
        t->headers.clear();
    } else if (line.empty()) {
        if (t->status >= 200) deliverHead(t); else t->headers.clear();
    } else {
        const size_t colon = line.find(':');
        if (colon != std::string::npos) t->headers.push_back({trim(line.substr(0, colon)), trim(line.substr(colon + 1))});
    }
    return n;
}

size_t writeCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    Transfer *t = static_cast<Transfer *>(userp);
    if (t->refused || t->job->canceled) return 0;
    deliverHead(t);
    if (!t->sink->onBody(ptr, n)) { t->refused = true; return 0; }
    return n;
}

int progressCb(void *userp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    return static_cast<Transfer *>(userp)->job->canceled ? 1 : 0;   // 1 = abort the transfer
}

yt::TransportStatus statusFor(CURLcode rc)
{
    yt::TransportStatus st;
    switch (rc) {
    case CURLE_OK: return st;
    case CURLE_COULDNT_RESOLVE_HOST: case CURLE_COULDNT_RESOLVE_PROXY: st.error = yt::TransportError::Resolve; break;
    case CURLE_COULDNT_CONNECT: st.error = yt::TransportError::Connect; break;
    case CURLE_SSL_CONNECT_ERROR: case CURLE_PEER_FAILED_VERIFICATION: st.error = yt::TransportError::Tls; break;
    case CURLE_RECV_ERROR: case CURLE_SEND_ERROR: case CURLE_PARTIAL_FILE: case CURLE_GOT_NOTHING:
        st.error = yt::TransportError::Reset; break;
    case CURLE_WRITE_ERROR: case CURLE_ABORTED_BY_CALLBACK: st.error = yt::TransportError::Canceled; break;
    default: st.error = yt::TransportError::Other; break;
    }
    st.message = curl_easy_strerror(rc);
    return st;
}

void run(const yt::TransportConfig &cfg, yt::HttpRequest req, Transfer t)
{
    CURL *e = curl_easy_init();
    if (!e) { t.sink->onDone({yt::TransportError::Other, "curl_easy_init failed"}); return; }
    curl_slist *hdrs = nullptr;
    curl_easy_setopt(e, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, &headerCb);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, &t);
    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &writeCb);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(e, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(e, CURLOPT_XFERINFOFUNCTION, &progressCb);
    curl_easy_setopt(e, CURLOPT_XFERINFODATA, &t);
    curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 0L);      // the library owns redirects
    curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "http,https");
    if (cfg.ipResolve == yt::IpResolve::V4) curl_easy_setopt(e, CURLOPT_IPRESOLVE, (long) CURL_IPRESOLVE_V4);
    if (cfg.ipResolve == yt::IpResolve::V6) curl_easy_setopt(e, CURLOPT_IPRESOLVE, (long) CURL_IPRESOLVE_V6);
    if (!cfg.caBundlePath.empty()) curl_easy_setopt(e, CURLOPT_CAINFO, cfg.caBundlePath.c_str());
    if (cfg.noProxy) curl_easy_setopt(e, CURLOPT_PROXY, "");
    else if (!cfg.proxy.empty()) curl_easy_setopt(e, CURLOPT_PROXY, cfg.proxy.c_str());
    for (const yt::HttpHeader &h : req.headers) hdrs = curl_slist_append(hdrs, (h.name + ": " + h.value).c_str());
    if (req.method == yt::HttpRequest::Method::Post) {
        hdrs = curl_slist_append(hdrs, "Expect:");
        curl_easy_setopt(e, CURLOPT_POST, 1L);
        curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) req.body.size());
        curl_easy_setopt(e, CURLOPT_POSTFIELDS, req.body.data());
    }
    if (hdrs) curl_easy_setopt(e, CURLOPT_HTTPHEADER, hdrs);
    const CURLcode rc = curl_easy_perform(e);
    if (rc == CURLE_OK) deliverHead(&t);
    yt::TransportStatus st = statusFor(rc);
    if (t.refused || t.job->canceled) st = {yt::TransportError::Canceled, "canceled"};
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(e);
    t.sink->onDone(std::move(st));
}

} // namespace

ThreadedTransport::ThreadedTransport(const yt::TransportConfig &cfg) : m_cfg(cfg)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);   // refcounted by libcurl
}

ThreadedTransport::~ThreadedTransport()
{
    std::map<uint64_t, std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> l(m_mx);
        jobs.swap(m_jobs);
    }
    for (auto &kv : jobs) kv.second->canceled = true;
    for (auto &kv : jobs) if (kv.second->thread.joinable()) kv.second->thread.join();
    curl_global_cleanup();
}

void ThreadedTransport::reapFinished()
{
    std::vector<std::shared_ptr<Job>> done;
    {
        std::lock_guard<std::mutex> l(m_mx);
        for (auto it = m_jobs.begin(); it != m_jobs.end();) {
            if (it->second->finished) { done.push_back(it->second); it = m_jobs.erase(it); }
            else ++it;
        }
    }
    for (auto &j : done) if (j->thread.joinable()) j->thread.join();
}

uint64_t ThreadedTransport::start(yt::HttpRequest request, std::shared_ptr<yt::HttpResponseSink> sink)
{
    reapFinished();
    std::shared_ptr<Job> job = std::make_shared<Job>();
    uint64_t id;
    {
        std::lock_guard<std::mutex> l(m_mx);
        id = ++m_next;
        m_jobs[id] = job;
    }
    Transfer t;
    t.sink = std::move(sink);
    t.job = job;
    job->thread = std::thread([cfg = m_cfg, request = std::move(request), t = std::move(t)]() mutable {
        std::shared_ptr<Job> j = t.job;
        run(cfg, std::move(request), std::move(t));
        j->finished = true;
    });
    return id;
}

void ThreadedTransport::cancel(uint64_t id)
{
    std::lock_guard<std::mutex> l(m_mx);
    auto it = m_jobs.find(id);
    if (it != m_jobs.end()) it->second->canceled = true;
}

}
