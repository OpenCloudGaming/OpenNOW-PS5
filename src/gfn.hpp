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
    bool sessionSaved = false;
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
    explicit Login(Request request, void* context, const char* sessionPath = nullptr) noexcept
        : request_(request), context_(context), sessionPath_(sessionPath) {}
    bool restore(char (&deviceId)[37], std::uint64_t now) noexcept;
    void begin(const char* deviceId, std::uint64_t now) noexcept;
    void tick(std::uint64_t now) noexcept;
    void tickAsync(std::uint64_t now, Request refreshRequest, void* refreshContext) noexcept;
    void finishAsync(std::uint64_t now) noexcept;
    void stopAsync() noexcept;
    void cancel() noexcept;
    const View& view() const noexcept { return view_; }
    const char* userId() const noexcept { return view_.profileVerified ? session_.subject : ""; }
    const char* cloudToken() const noexcept;
    Login(const Login&) = delete;
    Login& operator=(const Login&) = delete;
    ~Login();
private:
    struct AsyncRefresh;
    AsyncRefresh* async_ = nullptr;
    void publishAsync(std::uint64_t now, bool wait = false) noexcept;
    void erase() noexcept;
    void fail(const char* message, State state = State::failed) noexcept;
    bool acceptTokens(const Response& response, bool refreshing, std::uint64_t now) noexcept;
    bool verify(std::uint64_t now, bool refreshing = false) noexcept;
    void refresh(std::uint64_t now) noexcept;
    bool save() noexcept;
    bool forget() noexcept;
    bool serviceValid(std::uint64_t now) const noexcept;
    void authenticated(std::uint64_t now) noexcept;
    Request request_;
    void* context_;
    View view_{};
    char deviceCode_[2048]{};
    struct Session {
        char magic[8] = "ONAUTH1";
        char deviceId[37]{};
        char accessToken[16384]{};
        char idToken[16384]{};
        char refreshToken[16384]{};
        char clientToken[16384]{};
        char subject[1024]{};
    } session_;
    const char* sessionPath_ = nullptr;
    std::uint64_t refreshAt_ = 0;
    std::uint64_t accessExpiresAt_ = 0, serviceExpiresAt_ = 0, serviceWallExpiry_ = 0, now_ = 0;
    bool serviceJwt_ = false;
    enum class Persistence { none, save, forget } persistence_ = Persistence::none;
    bool deferPersistence_ = false;
    bool restoring_ = false;
    std::uint64_t deadline_ = 0, nextPoll_ = 0;
    unsigned interval_ = 5;
};
bool encodeForm(const char* source, char* destination, std::size_t capacity) noexcept;
bool trustedVerificationUrl(const char* url) noexcept;
void secureErase(void* memory, std::size_t size) noexcept;
}
