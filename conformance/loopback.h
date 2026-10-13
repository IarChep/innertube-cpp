// Scripted HTTP/1.1 server on 127.0.0.1:<ephemeral> for transport tests (POSIX).
// One thread per connection, Connection: close. Not a general HTTP server: it reads a
// request line + headers + Content-Length body and answers from a route table.
#ifndef YT_CONFORMANCE_LOOPBACK_H
#define YT_CONFORMANCE_LOOPBACK_H
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yt::conformance {

struct Canned {
    int status = 200;
    std::string body;
    std::vector<std::string> headers;      // extra "Name: value" lines
    int delayMs = 0;                       // before the response is written
    long declaredLength = -1;              // > body.size() => truncated transfer
};

class Loopback {
public:
    Loopback() {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(m_fd, (sockaddr *)&a, sizeof a);
        ::listen(m_fd, 64);
        socklen_t len = sizeof a;
        ::getsockname(m_fd, (sockaddr *)&a, &len);
        m_port = ntohs(a.sin_port);
        m_acceptor = std::thread([this] { serve(); });
    }
    ~Loopback() {
        m_stop = true;
        ::shutdown(m_fd, SHUT_RDWR);
        ::close(m_fd);
        m_acceptor.join();
        std::vector<std::thread> ws;
        { std::lock_guard<std::mutex> l(m_mx); ws.swap(m_workers); }
        for (std::thread &t : ws) t.join();
    }
    Loopback(const Loopback &) = delete;
    Loopback &operator=(const Loopback &) = delete;

    int port() const { return m_port; }
    std::string url(const std::string &path) const { return "http://127.0.0.1:" + std::to_string(m_port) + path; }
    void route(const std::string &path, Canned c) { std::lock_guard<std::mutex> l(m_mx); m_routes[path] = std::move(c); }
    int hits(const std::string &path) { std::lock_guard<std::mutex> l(m_mx); return m_hits[path]; }
    std::string lastRequest(const std::string &path) { std::lock_guard<std::mutex> l(m_mx); return m_last[path]; }

    // A port nobody listens on (bound, noted, closed).
    static int closedPort() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, (sockaddr *)&a, sizeof a);
        socklen_t len = sizeof a;
        ::getsockname(fd, (sockaddr *)&a, &len);
        ::close(fd);
        return ntohs(a.sin_port);
    }

private:
    void serve() {
        while (!m_stop) {
            int c = ::accept(m_fd, nullptr, nullptr);
            if (c < 0) break;
            std::lock_guard<std::mutex> l(m_mx);
            m_workers.emplace_back([this, c] { handle(c); });
        }
    }
    static void sendAll(int c, const std::string &out) {
        size_t off = 0;
        while (off < out.size()) {
            const ssize_t n = ::send(c, out.data() + off, out.size() - off, MSG_NOSIGNAL);
            if (n <= 0) return;
            off += (size_t)n;
        }
    }
    void handle(int c) {
        std::string req;
        char buf[8192];
        size_t hdrEnd = std::string::npos;
        long need = -1;
        for (;;) {
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            req.append(buf, (size_t)n);
            if (hdrEnd == std::string::npos && (hdrEnd = req.find("\r\n\r\n")) != std::string::npos) {
                need = 0;
                for (const char *key : {"Content-Length: ", "content-length: "}) {
                    const size_t p = req.find(key);
                    if (p != std::string::npos && p < hdrEnd) need = std::stol(req.substr(p + 16));
                }
            }
            if (hdrEnd != std::string::npos && (long)(req.size() - hdrEnd - 4) >= need) break;
        }
        const size_t sp1 = req.find(' ');
        const size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : req.find(' ', sp1 + 1);
        std::string path = (sp2 == std::string::npos) ? std::string("/") : req.substr(sp1 + 1, sp2 - sp1 - 1);
        path = path.substr(0, path.find('?'));
        Canned k;
        {
            std::lock_guard<std::mutex> l(m_mx);
            ++m_hits[path];
            m_last[path] = req;
            if (m_routes.count(path)) k = m_routes[path];
            else k.status = 404;
        }
        if (k.delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(k.delayMs));
        std::string out = "HTTP/1.1 " + std::to_string(k.status) + " X\r\nConnection: close\r\nContent-Length: "
            + std::to_string(k.declaredLength >= 0 ? k.declaredLength : (long)k.body.size()) + "\r\n";
        for (const std::string &h : k.headers) out += h + "\r\n";
        out += "\r\n";
        out += k.body;
        sendAll(c, out);
        ::shutdown(c, SHUT_RDWR);
        ::close(c);
    }

    int m_fd = -1;
    int m_port = 0;
    std::atomic<bool> m_stop{false};
    std::thread m_acceptor;
    std::vector<std::thread> m_workers;
    std::mutex m_mx;
    std::map<std::string, Canned> m_routes;
    std::map<std::string, int> m_hits;
    std::map<std::string, std::string> m_last;
};

}
#endif
