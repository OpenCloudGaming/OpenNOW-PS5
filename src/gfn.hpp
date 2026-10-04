// SPDX-License-Identifier: GPL-3.0-or-later
// Device authorization flow adapted from OpenNOW-Switch (MIT, see licenses/).
#pragma once
#include <cstddef>
#include <cstdint>

namespace opennow {
enum class State { idle, requesting, waiting, authenticated, denied, expired, failed, cancelled };
struct View {
    State state = State::idle;
    char code[64]{};
    char url[512]{};
    char qrUrl[1024]{};
    char message[192] = "Press CROSS to sign in with NVIDIA";
    unsigned expiresIn = 0;
    bool profileVerified = false;
};
struct Response {
    long status = 0;
    char* body = nullptr;
    std::size_t length = 0;
    const char* error = nullptr;
};
using Request = Response (*)(void*, const char*, const char*, const char*, const char*, const char*);
// Request(method, URL, form body, bearer token, device ID). Response owned by transport.
class Login {
public:
    explicit Login(Request request, void* context) noexcept : request_(request), context_(context) {}
    void begin(const char* deviceId, std::uint64_t now) noexcept;
    void tick(std::uint64_t now) noexcept;
    void cancel() noexcept;
    const View& view() const noexcept { return view_; }
    const char* cloudToken() const noexcept { return view_.state==State::authenticated ? idToken_ : ""; }
    ~Login() { erase(); }
private:
    void erase() noexcept;
    void fail(const char* message, State state = State::failed) noexcept;
    Request request_;
    void* context_;
    View view_{};
    char deviceCode_[2048]{};
    char accessToken_[16384]{};
    char idToken_[16384]{};
    std::uint64_t deadline_ = 0, nextPoll_ = 0;
    unsigned interval_ = 5;
};
bool encodeForm(const char* source, char* destination, std::size_t capacity) noexcept;
bool trustedVerificationUrl(const char* url) noexcept;
void secureErase(void* memory, std::size_t size) noexcept;
}
