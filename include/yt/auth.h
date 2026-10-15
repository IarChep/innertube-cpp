// OAuth 2.0 TV "limited-input device" sign-in (the only headless-friendly YouTube
// auth). Only the refresh token is persisted (via ClientOptions::onStateChanged);
// access tokens are minted on demand, refreshed a minute before they expire and once
// more on an HTTP 401. C++17.
#ifndef YT_AUTH_H
#define YT_AUTH_H
#include <yt/result.h>
#include <functional>
#include <memory>
#include <string>
namespace yt {

class Job;
namespace detail { struct Engine; }

struct DeviceCode {
    std::string verificationUrl, userCode;
    int intervalSecs = 5;
    int expiresInSecs = 0;                 // the code's lifetime (0 = not reported)
    std::string qrPng;                     // YouTube's own QR (PNG bytes); empty if it could not be had
};

class Auth {
public:
    // Requests a device code and reports it via onCode (show the code and/or the QR
    // image; reported once, after the QR request), then polls at the server's interval
    // until the user authorizes on another device (onDone ok), denies / the code expires
    // (onDone error), or the Job is canceled (no further callbacks). Starting a new sign-in cancels a pending one.
    Job signIn(std::function<void(const DeviceCode &)> onCode, Callback<Done> onDone);
    void signOut();                        // drops the tokens; onStateChanged reports it
    bool signedIn() const;                 // a refresh token is held (any thread)
private:
    friend class Client;
    explicit Auth(detail::Engine *e) : m_e(e) {}
    detail::Engine *m_e;
};

}
#endif
