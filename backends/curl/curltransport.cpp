#include "backends/curl/curltransport.h"
#include "core/log.h"
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

namespace yt::backends {

struct CurlTransport::Req {
    CurlTransport *owner = nullptr;
    uint64_t id = 0;
    CURL *easy = nullptr;
    curl_slist *hdrs = nullptr;
    std::string body;                          // owned: CURLOPT_POSTFIELDS does not copy
    std::shared_ptr<HttpResponseSink> sink;
    int status = 0;
    std::vector<HttpHeader> headers;
    bool headDelivered = false;
    bool refused = false;                      // the sink returned false from onBody
};

namespace {

TransportStatus statusFor(CURLcode rc)
{
    TransportStatus st;
    switch (rc) {
    case CURLE_OK: return st;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY: st.error = TransportError::Resolve; break;
    case CURLE_COULDNT_CONNECT: st.error = TransportError::Connect; break;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE: st.error = TransportError::Tls; break;
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_PARTIAL_FILE:
    case CURLE_GOT_NOTHING: st.error = TransportError::Reset; break;
    case CURLE_WRITE_ERROR:
    case CURLE_ABORTED_BY_CALLBACK: st.error = TransportError::Canceled; break;
    case CURLE_UNSUPPORTED_PROTOCOL:
    case CURLE_URL_MALFORMAT:
    case CURLE_WEIRD_SERVER_REPLY: st.error = TransportError::Protocol; break;
    default: st.error = TransportError::Other; break;
    }
    st.message = curl_easy_strerror(rc);
    return st;
}

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

void deliverHead(CurlTransport::Req *r)
{
    if (r->headDelivered) return;
    if (r->status == 0) {
        long code = 0;
        curl_easy_getinfo(r->easy, CURLINFO_RESPONSE_CODE, &code);
        r->status = (int) code;
    }
    r->headDelivered = true;
    r->sink->onHead(r->status, std::move(r->headers));
    r->headers.clear();
}

size_t headerCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    CurlTransport::Req *r = static_cast<CurlTransport::Req *>(userp);
    const std::string line = trim(std::string(ptr, n));
    if (line.rfind("HTTP/", 0) == 0) {                     // a new status line (1xx, then final)
        const size_t sp = line.find(' ');
        r->status = (sp == std::string::npos) ? 0 : std::atoi(line.c_str() + sp + 1);
        r->headers.clear();
        return n;
    }
    if (line.empty()) {                                    // end of one response's headers
        if (r->status >= 200 && !r->headDelivered) deliverHead(r);
        else if (r->status < 200) r->headers.clear();      // interim response: wait for the final one
        return n;
    }
    const size_t colon = line.find(':');
    if (colon != std::string::npos)
        r->headers.push_back({trim(line.substr(0, colon)), trim(line.substr(colon + 1))});
    return n;
}

size_t writeCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    CurlTransport::Req *r = static_cast<CurlTransport::Req *>(userp);
    if (r->refused) return 0;
    deliverHead(r);
    if (!r->sink->onBody(ptr, n)) { r->refused = true; return 0; }   // -> CURLE_WRITE_ERROR
    return n;
}

// Header lines that carry credentials keep their name and lose their value in traces:
// the OAuth bearer, the consent / session cookies.
bool isCredentialHeader(const std::string &line)
{
    static const char *const kNames[] = {"authorization:", "proxy-authorization:", "cookie:", "set-cookie:"};
    for (const char *name : kNames) {
        const size_t n = std::strlen(name);
        if (line.size() < n) continue;
        bool match = true;
        for (size_t i = 0; i < n && match; ++i)
            match = (char) std::tolower((unsigned char) line[i]) == name[i];
        if (match) return true;
    }
    return false;
}

int debugCb(CURL *, curl_infotype type, char *data, size_t size, void *userp)
{
    if (type != CURLINFO_TEXT && type != CURLINFO_HEADER_IN && type != CURLINFO_HEADER_OUT) return 0;
    const char *tag = (type == CURLINFO_TEXT) ? "*" : (type == CURLINFO_HEADER_OUT ? ">" : "<");
    const Logger &log = *static_cast<const Logger *>(userp);
    // HEADER_OUT arrives as one block of CRLF-separated lines: trace (and redact) per line.
    const std::string block(data, size);
    size_t pos = 0;
    while (pos < block.size()) {
        size_t eol = block.find('\n', pos);
        if (eol == std::string::npos) eol = block.size();
        std::string line = block.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        if (type != CURLINFO_TEXT && isCredentialHeader(line)) line = line.substr(0, line.find(':') + 1) + " <redacted>";
        YTLOG(log, "curl") << tag << line;
    }
    return 0;                                              // non-zero would abort the transfer
}

} // namespace

CurlTransport::CurlTransport(const TransportConfig &cfg) : m_cfg(cfg)
{
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    m_multi = curl_multi_init();
}

CurlTransport::~CurlTransport()
{
    for (auto &kv : m_reqs) {
        Req *r = kv.second.get();
        curl_multi_remove_handle(m_multi, r->easy);
        curl_easy_cleanup(r->easy);
        curl_slist_free_all(r->hdrs);
    }
    m_reqs.clear();
    m_deferred.clear();                                    // no sink calls from the destructor
    curl_multi_cleanup(m_multi);
}

uint64_t CurlTransport::start(HttpRequest request, std::shared_ptr<HttpResponseSink> sink)
{
    std::unique_ptr<Req> r = std::make_unique<Req>();
    r->owner = this;
    r->id = ++m_next;
    r->sink = sink;
    r->body = std::move(request.body);
    const uint64_t id = r->id;
    r->easy = curl_easy_init();
    if (!r->easy) {
        m_deferred.push_back({sink, {TransportError::Other, "curl_easy_init failed"}});
        return id;
    }
    CURL *e = r->easy;
    curl_easy_setopt(e, CURLOPT_URL, request.url.c_str());   // copied by libcurl
    curl_easy_setopt(e, CURLOPT_PRIVATE, r.get());
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, &headerCb);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, r.get());
    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &writeCb);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, r.get());
    curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");      // every encoding curl can decode
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    // Wait for an in-progress connection to the same host instead of opening another:
    // once it negotiates h2 the transfer multiplexes onto it (one TLS handshake).
    curl_easy_setopt(e, CURLOPT_PIPEWAIT, 1L);
    // Redirects are library policy: a backend delivers the 3xx as is.
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(e, CURLOPT_UNRESTRICTED_AUTH, 0L);
    curl_easy_setopt(e, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS, (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    switch (m_cfg.ipResolve) {
    case IpResolve::Any: break;
    case IpResolve::V4: curl_easy_setopt(e, CURLOPT_IPRESOLVE, (long) CURL_IPRESOLVE_V4); break;
    case IpResolve::V6: curl_easy_setopt(e, CURLOPT_IPRESOLVE, (long) CURL_IPRESOLVE_V6); break;
    }
    if (!m_cfg.caBundlePath.empty()) curl_easy_setopt(e, CURLOPT_CAINFO, m_cfg.caBundlePath.c_str());
    if (!m_cfg.proxy.empty()) curl_easy_setopt(e, CURLOPT_PROXY, m_cfg.proxy.c_str());
    if (m_cfg.log) {
        curl_easy_setopt(e, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(e, CURLOPT_DEBUGFUNCTION, &debugCb);
        curl_easy_setopt(e, CURLOPT_DEBUGDATA, &m_cfg.log);
    }
    for (const HttpHeader &h : request.headers)
        r->hdrs = curl_slist_append(r->hdrs, (h.name + ": " + h.value).c_str());
    if (request.method == HttpRequest::Method::Post) {
        // No "Expect: 100-continue" on >1 KB bodies: an extra RTT (or 1 s stall) per call.
        r->hdrs = curl_slist_append(r->hdrs, "Expect:");
        curl_easy_setopt(e, CURLOPT_POST, 1L);
        curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) r->body.size());
        curl_easy_setopt(e, CURLOPT_POSTFIELDS, r->body.data());
    } else {
        curl_easy_setopt(e, CURLOPT_HTTPGET, 1L);
    }
    if (r->hdrs) curl_easy_setopt(e, CURLOPT_HTTPHEADER, r->hdrs);
    if (curl_multi_add_handle(m_multi, e) != CURLM_OK) {
        curl_easy_cleanup(e);
        curl_slist_free_all(r->hdrs);
        m_deferred.push_back({sink, {TransportError::Other, "curl_multi_add_handle failed"}});
        return id;
    }
    m_reqs[id] = std::move(r);
    return id;
}

void CurlTransport::cancel(uint64_t id)
{
    auto it = m_reqs.find(id);
    if (it == m_reqs.end()) return;
    Req *r = it->second.get();
    curl_multi_remove_handle(m_multi, r->easy);
    curl_easy_cleanup(r->easy);
    curl_slist_free_all(r->hdrs);
    m_deferred.push_back({r->sink, {TransportError::Canceled, "canceled"}});
    m_reqs.erase(it);
}

void CurlTransport::finish(Req *r, TransportStatus st)
{
    std::shared_ptr<HttpResponseSink> sink = r->sink;
    curl_multi_remove_handle(m_multi, r->easy);
    curl_easy_cleanup(r->easy);
    curl_slist_free_all(r->hdrs);
    m_reqs.erase(r->id);                                   // r is gone from here on
    sink->onDone(std::move(st));
}

void CurlTransport::drive()
{
    int running = 0;
    curl_multi_perform(m_multi, &running);
    int left = 0;
    while (CURLMsg *m = curl_multi_info_read(m_multi, &left)) {
        if (m->msg != CURLMSG_DONE) continue;
        CURL *e = m->easy_handle;
        const CURLcode rc = m->data.result;                // copy before remove invalidates m
        Req *r = nullptr;
        curl_easy_getinfo(e, CURLINFO_PRIVATE, &r);
        if (!r) continue;
        if (rc == CURLE_OK) deliverHead(r);                // a body-less response with no blank line seen
        TransportStatus st = statusFor(rc);
        if (r->refused) st = {TransportError::Canceled, "refused by the receiver"};
        finish(r, std::move(st));
    }
}

void CurlTransport::flushDeferred()
{
    std::vector<std::pair<std::shared_ptr<HttpResponseSink>, TransportStatus>> batch;
    batch.swap(m_deferred);
    for (auto &d : batch) d.first->onDone(std::move(d.second));
}

void CurlTransport::wait(int maxWaitMs)
{
    flushDeferred();
    drive();
    curl_multi_poll(m_multi, nullptr, 0, m_deferred.empty() ? maxWaitMs : 0, nullptr);
    drive();
    flushDeferred();
}

void CurlTransport::wake() { curl_multi_wakeup(m_multi); }

}
