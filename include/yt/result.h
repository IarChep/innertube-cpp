// Result<T> / Error: every asynchronous operation reports one of these. C++17.
#ifndef YT_RESULT_H
#define YT_RESULT_H
#include <functional>
#include <optional>
#include <string>
namespace yt {
enum class ErrorCode {
    Transport,        // DNS/connect/TLS/reset/partial body/cap exceeded
    Timeout,          // request deadline
    InvalidResponse,  // not JSON (HTML consent page, empty)
    Api,              // InnerTube/OAuth error envelope
    Unavailable,      // channel/account/resolve/streams not available
    Unsupported,      // e.g. streams need signature decipher
    AuthRequired,     // authed call without a session
    Canceled,
    InvalidArgument   // a link or argument the library cannot use (checked before any request)
};
struct Error { ErrorCode code = ErrorCode::Transport; std::string message; long httpStatus = 0; };
template <class T> struct Result {
    std::optional<Error> error;   // nullopt = ok
    T value{};
    bool ok() const { return !error.has_value(); }
};
struct Done {};
template <class T> using Callback = std::function<void(const Result<T> &)>;
}
#endif
