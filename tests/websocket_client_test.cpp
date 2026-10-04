#include "stream/WebSocketClient.hpp"
#include "random.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

extern "C" void console_curl_setup(CURL*) {}

namespace opennow {
bool randomBytes(void* output, std::size_t length) noexcept {
    std::memset(output, 0xf1, length);
    return true;
}
}

int main(int argc, char** argv) {
    assert(argc == 3);
    WebSocketClient client(argv[1]);
    client.set_custom_headers({"Origin: https://play.geforcenow.com",
        "Sec-WebSocket-Protocol: x-nv-sessionid.fixture-session"});
    if (std::strcmp(argv[2], "success")) {
        assert(!client.connect());
        assert(client.get_last_error() == argv[2]);
        return 0;
    }
    assert(client.connect());
    std::vector<std::string> messages;
    client.set_on_message([&](const std::string& message) { messages.push_back(message); });
    client.send_message("{\"client\":1}");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (messages.size() < 2 && client.is_connected() && std::chrono::steady_clock::now() < deadline) {
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert((messages == std::vector<std::string>{"{\"text\":1}", "{\"binary\":1}"}));
    client.disconnect();
}
