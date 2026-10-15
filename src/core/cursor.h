// Builds and reads the opaque yt::Cursor<T> (yt/types.h). Only the chains know which
// request continues a list: the endpoint and the client that served page 1 (signed-in
// feeds are TVHTML5, and YouTube only continues a list for the client that started it).
#ifndef YT_CORE_CURSOR_H
#define YT_CORE_CURSOR_H
#include <yt/catalog.h>
#include <yt/types.h>
#include <string>
namespace yt::detail {

struct CursorAccess {
    // An empty token yields the false cursor: there is no next page.
    template <class T> static Cursor<T> make(std::string endpoint, ClientId client, std::string token)
    {
        Cursor<T> c;
        if (token.empty()) return c;
        c.m_endpoint = std::move(endpoint);
        c.m_client = (int) client;
        c.m_token = std::move(token);
        return c;
    }
    template <class T> static const std::string &endpoint(const Cursor<T> &c) { return c.m_endpoint; }
    template <class T> static ClientId client(const Cursor<T> &c) { return (ClientId) c.m_client; }
    template <class T> static const std::string &token(const Cursor<T> &c) { return c.m_token; }
};

}
#endif
