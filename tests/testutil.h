// Shared helpers for the doctest suites.
#ifndef IT_TESTUTIL_H
#define IT_TESTUTIL_H
#include <fstream>
#include <sstream>
#include <string>
// Loads tests/fixtures/<name> as raw bytes ("" when missing).
inline std::string loadFixtureRaw(const char *name) {
    std::ifstream f(std::string(IT_TEST_DATA_DIR) + "/fixtures/" + name, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
#endif
