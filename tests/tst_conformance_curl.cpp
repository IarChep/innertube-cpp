#include <doctest/doctest.h>
#include "conformance/transport_conformance.h"
TEST_CASE("curl backend passes the transport conformance suite") {
    const auto failures = yt::conformance::runTransportConformance(
        [](const yt::TransportConfig &c) { return yt::makeDefaultTransport(c); });
    for (const auto &f : failures) MESSAGE(f);
    CHECK(failures.empty());
}
