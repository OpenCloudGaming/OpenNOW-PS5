#include "websocket_handshake.hpp"

#include <cassert>
#include <string>

int main()
{
    using opennow::websocket::AcceptForKey;
    using opennow::websocket::ValidateUpgrade;
    const std::string expected = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    assert(AcceptForKey("dGhlIHNhbXBsZSBub25jZQ==") == expected);
    assert(AcceptForKey("a-different-challenge") != expected);

    const std::string status = "HTTP/1.1 101 Switching Protocols\r\n";
    const std::string headers = "Upgrade: websocket\r\nConnection: Upgrade\r\n";
    const std::string accept = "Sec-WebSocket-Accept: " + expected + "\r\n";
    assert(ValidateUpgrade(status + headers + accept + "\r\n", expected));
    assert(ValidateUpgrade(status + "uPgRaDe:\tWebSocket \t\r\n"
                           "CONNECTION: keep-alive, upGRADE\r\n" + accept + "\r\n", expected));
    assert(ValidateUpgrade(status + "Connection: keep-alive\r\n" + headers + accept + "\r\n", expected));
    assert(!ValidateUpgrade(status + "\r\n", expected));
    assert(!ValidateUpgrade(status + headers + "\r\n", expected));
    assert(!ValidateUpgrade(status + accept + "\r\n", expected));
    assert(!ValidateUpgrade(status + headers + accept + "\r\n", "wrong-challenge"));
    assert(!ValidateUpgrade(status + headers + accept + accept + "\r\n", expected));
    assert(!ValidateUpgrade(status + "Upgrade: websocket-other\r\nConnection: Upgrade\r\n" + accept + "\r\n", expected));
    assert(!ValidateUpgrade(status + "Upgrade: websocket\r\nConnection: xUpgrade\r\n" + accept + "\r\n", expected));
    assert(!ValidateUpgrade("HTTP/1.1 200 101 Fake\r\n" + headers + accept + "\r\n", expected));
    assert(!ValidateUpgrade(status + headers + accept, expected));
    assert(!ValidateUpgrade(status + headers + " " + accept + "\r\n", expected));
    assert(!ValidateUpgrade(status + headers + "Sec-WebSocket-Accept: \r\n\r\n", ""));
    std::string error;
    assert(!ValidateUpgrade("HTTP/1.1 403 sensitive-session-id\r\n\r\nsensitive-token", expected, &error));
    assert(error == "WebSocket upgrade rejected (HTTP 403)");
    assert(!ValidateUpgrade(status + headers + "Sec-WebSocket-Accept: sensitive-token\r\n\r\n", expected, &error));
    assert(error == "Invalid WebSocket accept challenge");
    assert(!ValidateUpgrade(status + "\r\n", expected, &error));
    assert(error == "WebSocket response missing Upgrade header");
    assert(ValidateUpgrade(status + headers + accept + "\r\n", expected, &error));
    assert(error.empty());
}
