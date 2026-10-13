// The HTTP backend conformance suite: what the library relies on from ANY
// yt::HttpTransport, built-in or your own. Framework-free — call it from any test
// runner; an empty result means the backend is supported. See docs/backends.md.
#ifndef YT_CONFORMANCE_TRANSPORT_CONFORMANCE_H
#define YT_CONFORMANCE_TRANSPORT_CONFORMANCE_H
#include <yt/transport.h>
#include <string>
#include <vector>
namespace yt::conformance {
// Runs every check against a fresh transport from `make` (plain-HTTP loopback server,
// so no TLS setup is needed); returns "<check>: <why>" per failure. Drives
// transport->driver()->wait() on the calling thread when the backend has one.
std::vector<std::string> runTransportConformance(const TransportFactory &make);
}
#endif
