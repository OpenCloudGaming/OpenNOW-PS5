// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from OpenNOW-Switch gfn/authentication.cpp, MIT (upstream-lock.json).
#include "gfn.hpp"
#include "account_file.hpp"
#include "vendor/cJSON.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <optional>
#include <pthread.h>
#include <utility>

namespace opennow {
namespace {
constexpr const char* clientId = "q61ddeJrVt7O90Nl-P-N7I36yctih4Ml6FyXLrb6j-U";
constexpr const char* providerId = "PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg";
struct Json {
    cJSON* value;
    explicit Json(const Response& r) : value(r.body ? cJSON_ParseWithLengthOpts(r.body, r.length + 1, nullptr, true) : nullptr) {}
    static void wipe(cJSON* node) {
        for (; node; node=node->next) {
            if (node->valuestring) secureErase(node->valuestring,std::strlen(node->valuestring));
            wipe(node->child);
        }
    }
    ~Json() { wipe(value); cJSON_Delete(value); }
};
const char* string(cJSON* root, const char* key) {
    auto* v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
template<std::size_t N> bool copy(char (&out)[N], const char* in) {
    const auto size = std::strlen(in);
    if (size >= N) return false;
    secureErase(out,sizeof(out));
    std::memcpy(out, in, size + 1);
    return size != 0;
}
unsigned number(cJSON* root, const char* key, unsigned fallback, unsigned limit) {
    auto* v = cJSON_GetObjectItemCaseSensitive(root,key);
    if (!cJSON_IsNumber(v)) return fallback;
    if (v->valuedouble < 1 || v->valuedouble > limit) return 0;
    return static_cast<unsigned>(v->valuedouble);
}
std::uint64_t wallTime() noexcept {
    const auto now = std::time(nullptr);
    return now > 0 ? static_cast<std::uint64_t>(now) : 0;
}
std::optional<std::uint64_t> jwtExpiry(const char* token) noexcept {
    const char* payload = std::strchr(token,'.');
    if (!payload) return std::nullopt;
    const char* end = std::strchr(++payload,'.');
    if (!end || std::strchr(end+1,'.')) return std::nullopt;
    const auto length = static_cast<std::size_t>(end-payload);
    if (length % 4 == 1 || length >= 16384) return std::nullopt;
    char decoded[16384]{};
    unsigned bits = 0, count = 0;
    std::size_t size = 0;
    constexpr const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (auto* p = payload; p != end; ++p) {
        const char* digit = *p ? std::strchr(alphabet,*p) : nullptr;
        if (!digit) { secureErase(decoded,sizeof(decoded)); return std::nullopt; }
        bits = (bits << 6) | static_cast<unsigned>(digit-alphabet);
        count += 6;
        if (count >= 8) { count -= 8; decoded[size++] = static_cast<char>(bits >> count); }
    }
    if (count && (bits & ((1U << count)-1))) { secureErase(decoded,sizeof(decoded)); return std::nullopt; }
    Response response{200,decoded,size,nullptr};
    Json json(response);
    const auto* expiry = cJSON_GetObjectItemCaseSensitive(json.value,"exp");
    const double value = cJSON_IsNumber(expiry) ? expiry->valuedouble : -1;
    std::optional<std::uint64_t> result;
    if (value >= 0 && value <= 9007199254740991.0) {
        const auto integer = static_cast<std::uint64_t>(value);
        if (value == static_cast<double>(integer)) result = integer;
    }
    secureErase(decoded,sizeof(decoded));
    return result;
}
}
struct Login::AsyncRefresh {
    pthread_t thread{};
    std::atomic_bool complete{false}, cancelled{false};
    Request request;
    void* context;
    std::uint64_t now;
    Login snapshot;
    AsyncRefresh(const Login& owner, Request transport, void* transportContext, std::uint64_t time) noexcept
        : request(transport), context(transportContext), now(time), snapshot(perform,this) {
        snapshot.session_ = owner.session_;
        snapshot.view_ = owner.view_;
        snapshot.refreshAt_ = owner.refreshAt_;
        snapshot.accessExpiresAt_ = owner.accessExpiresAt_;
        snapshot.serviceExpiresAt_ = owner.serviceExpiresAt_;
        snapshot.serviceWallExpiry_ = owner.serviceWallExpiry_;
        snapshot.serviceJwt_ = owner.serviceJwt_;
        snapshot.nextPoll_ = owner.nextPoll_;
        snapshot.restoring_ = owner.restoring_;
        snapshot.now_ = time;
        snapshot.deferPersistence_ = true;
    }
    static Response perform(void* context, const char* method, const char* url,
        const char* body, const char* bearer, const char* device) noexcept {
        auto& job = *static_cast<AsyncRefresh*>(context);
        if (job.cancelled.load()) return {0,nullptr,0,"Request cancelled"};
        return job.request(job.context,method,url,body,bearer,device);
    }
    static void* run(void* context) noexcept {
        auto& job = *static_cast<AsyncRefresh*>(context);
        job.snapshot.refresh(job.now);
        job.complete.store(true,std::memory_order_release);
        return nullptr;
    }
};
Login::~Login() { stopAsync(); erase(); }
void Login::stopAsync() noexcept {
    if (!async_) return;
    async_->cancelled.store(true);
    pthread_join(async_->thread,nullptr);
    delete async_; async_ = nullptr;
}
void Login::finishAsync(std::uint64_t now) noexcept {
    now_ = now;
    if (!async_) return;
    async_->cancelled.store(true);
    publishAsync(now,true);
}
void Login::publishAsync(std::uint64_t now, bool wait) noexcept {
    if (!wait && !async_->complete.load(std::memory_order_acquire)) return;
    pthread_join(async_->thread,nullptr);
    auto& result = async_->snapshot;
    session_ = result.session_; view_ = result.view_;
    refreshAt_ = result.refreshAt_; nextPoll_ = result.nextPoll_;
    accessExpiresAt_ = result.accessExpiresAt_;
    serviceExpiresAt_ = result.serviceExpiresAt_;
    serviceWallExpiry_ = result.serviceWallExpiry_;
    serviceJwt_ = result.serviceJwt_;
    restoring_ = result.restoring_;
    if (result.persistence_ == Persistence::forget) forget();
    if (result.persistence_ == Persistence::save) view_.sessionSaved = save();
    if (view_.state == State::authenticated) authenticated(now);
    delete async_; async_ = nullptr;
}
void Login::tickAsync(std::uint64_t now, Request request, void* context) noexcept {
    now_ = now;
    if (async_) { publishAsync(now); return; }
    if (view_.state != State::authenticated && !restoring_) { tick(now); return; }
    if (view_.state == State::authenticated ? !refreshAt_ || now < refreshAt_ : now < nextPoll_) return;
    if (!request) return;
    auto* job = new (std::nothrow) AsyncRefresh(*this,request,context,now);
    if (!job || pthread_create(&job->thread,nullptr,AsyncRefresh::run,job) != 0) {
        delete job; refreshAt_ = nextPoll_ = now + 30; return;
    }
    async_ = job;
}
bool Login::serviceValid(std::uint64_t now) const noexcept {
    return (serviceJwt_ ? now < serviceExpiresAt_ && wallTime() < serviceWallExpiry_ :
        !serviceExpiresAt_ || now < serviceExpiresAt_);
}
const char* Login::cloudToken() const noexcept {
    return view_.state == State::authenticated && serviceValid(now_) ? session_.idToken : "";
}
void secureErase(void* memory, std::size_t size) noexcept {
    auto* p = static_cast<volatile unsigned char*>(memory);
    while (size--) *p++ = 0;
}
bool encodeForm(const char* source, char* out, std::size_t capacity) noexcept {
    const char hex[] = "0123456789ABCDEF";
    std::size_t n = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*>(source); *p; ++p) {
        const bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        const std::size_t need = plain ? 1 : 3;
        if (n + need >= capacity) return false;
        if (plain) out[n++] = static_cast<char>(*p);
        else { out[n++] = '%'; out[n++] = hex[*p >> 4]; out[n++] = hex[*p & 15]; }
    }
    if (n >= capacity) return false;
    out[n] = 0;
    return true;
}
bool trustedVerificationUrl(const char* url) noexcept {
    // Only display NVIDIA's login destination, never an arbitrary server-provided link.
    constexpr const char* prefixes[] = {"https://login.nvidia.com/", "https://static-login.nvidia.com/", "https://www.nvidia.com/", "https://nvidia.com/"};
    for (auto prefix : prefixes) {
        if (std::strncmp(url, prefix, std::strlen(prefix)) != 0) continue;
        for (auto* p = url; *p; ++p) if (static_cast<unsigned char>(*p) <= 32 || *p == '\\') return false;
        return true;
    }
    return false;
}
void Login::erase() noexcept {
    secureErase(deviceCode_, sizeof(deviceCode_));
    secureErase(session_.accessToken, sizeof(session_.accessToken));
    secureErase(session_.idToken, sizeof(session_.idToken));
    secureErase(session_.refreshToken, sizeof(session_.refreshToken));
    secureErase(session_.clientToken, sizeof(session_.clientToken));
    secureErase(session_.subject, sizeof(session_.subject));
    refreshAt_ = accessExpiresAt_ = serviceExpiresAt_ = serviceWallExpiry_ = 0;
    serviceJwt_ = false; restoring_ = false;
}
void Login::fail(const char* message, State state) noexcept {
    erase(); view_ = {}; view_.state = state;
    std::snprintf(view_.message, sizeof(view_.message), "%s", message);
}
void Login::cancel() noexcept {
    stopAsync();
    const bool removed = forget();
    fail(removed ? "Signed out. Press CROSS to sign in" :
        "Could not remove saved login. Try signing out again", State::cancelled);
}
void Login::begin(const char* deviceId, std::uint64_t now) noexcept {
    stopAsync(); now_ = now;
    erase(); view_ = {}; view_.state = State::requesting;
    if (!copy(session_.deviceId,deviceId)) { fail("Invalid device identity"); return; }
    char id[256];
    if (!encodeForm(deviceId,id,sizeof(id)) || !deviceId[0]) { fail("Invalid device identity"); return; }
    char body[1024];
    std::snprintf(body,sizeof(body),"client_id=%s&scope=openid%%20consent%%20email%%20tk_client%%20age&device_id=%s&display_name=OpenNOW%%20PS5&idp_id=%s",clientId,id,providerId);
    auto r = request_(context_,"POST","https://login.nvidia.com/device/authorize",body,nullptr,deviceId);
    if (r.error) { fail(r.error); return; }
    if (r.status != 200) { char msg[128]; std::snprintf(msg,sizeof(msg),"NVIDIA authorization returned HTTP %ld",r.status); fail(msg); return; }
    Json json(r);
    interval_ = number(json.value,"interval",5,300);
    const unsigned lifetime = number(json.value,"expires_in",300,3600);
    if (!cJSON_IsObject(json.value) || !interval_ || !lifetime ||
        !copy(deviceCode_,string(json.value,"device_code")) ||
        !copy(view_.code,string(json.value,"user_code")) ||
        !copy(view_.url,string(json.value,"verification_uri")) ||
        !trustedVerificationUrl(view_.url)) { fail("Invalid NVIDIA authorization response"); return; }
    const char* complete = string(json.value,"verification_uri_complete");
    if (!*complete) complete = view_.url;
    if (!copy(view_.qrUrl,complete) || !trustedVerificationUrl(view_.qrUrl)) { fail("Invalid NVIDIA verification URL"); return; }
    deadline_ = now + lifetime; nextPoll_ = now + interval_;
    view_.state = State::waiting; view_.expiresIn = lifetime;
    copy(view_.message,"Scan the QR code with your phone and sign in");
}
void Login::tick(std::uint64_t now) noexcept {
    now_ = now;
    if (async_) { publishAsync(now); return; }
    if (view_.state == State::authenticated) {
        if (refreshAt_ && now >= refreshAt_) refresh(now);
        return;
    }
    if (restoring_) { if (now >= nextPoll_) refresh(now); return; }
    if (view_.state != State::waiting) return;
    if (now >= deadline_) { fail("Login code expired. Press CROSS to retry",State::expired); return; }
    view_.expiresIn = static_cast<unsigned>(deadline_ - now);
    if (now < nextPoll_) return;
    char encoded[sizeof(deviceCode_)*3], body[sizeof(deviceCode_)*3+256];
    if (!encodeForm(deviceCode_,encoded,sizeof(encoded))) { fail("Device code too long"); return; }
    std::snprintf(body,sizeof(body),"grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&device_code=%s&client_id=%s",encoded,clientId);
    auto r = request_(context_,"POST","https://login.nvidia.com/token",body,nullptr,nullptr);
    secureErase(encoded,sizeof(encoded)); secureErase(body,sizeof(body));
    nextPoll_ = now + interval_;
    if (r.error) { fail(r.error); return; }
    if (r.status == 429 || r.status >= 500) { interval_ = std::min(interval_ + 5,300U); nextPoll_ = now + interval_; return; }
    bool success = false;
    {
        Json json(r);
        if (!cJSON_IsObject(json.value)) { fail("Invalid NVIDIA token response"); return; }
        if (r.status == 200) {
            success = acceptTokens(r,false,now);
            if (!success) { fail("Missing or oversized NVIDIA tokens"); return; }
        } else {
            const auto* error = string(json.value,"error");
            if (std::strcmp(error,"authorization_pending") == 0) return;
            if (std::strcmp(error,"slow_down") == 0) { interval_ = std::min(interval_+5,300U); nextPoll_=now+interval_; return; }
            if (std::strcmp(error,"access_denied") == 0) { fail("Login denied. Press CROSS to retry",State::denied); return; }
            if (std::strcmp(error,"expired_token") == 0) { fail("Login code expired. Press CROSS to retry",State::expired); return; }
            fail("NVIDIA rejected the login request"); return;
        }
    }
    if (success) verify(now);
}

bool Login::acceptTokens(const Response& response, bool refreshing, std::uint64_t now) noexcept {
    Json json(response);
    if (!cJSON_IsObject(json.value)) return false;
    const char* access = string(json.value,"access_token");
    const char* id = string(json.value,"id_token");
    const char* refresh = string(json.value,"refresh_token");
    const char* client = string(json.value,"client_token");
    const unsigned lifetime = number(json.value,"expires_in",3600,604800);
    if (!*access || std::strlen(access) >= sizeof(session_.accessToken) ||
        std::strlen(id) >= sizeof(session_.idToken) ||
        std::strlen(refresh) >= sizeof(session_.refreshToken) ||
        std::strlen(client) >= sizeof(session_.clientToken) || !lifetime) return false;
    copy(session_.accessToken,access);
    if (*id) copy(session_.idToken,id);
    else if (!refreshing) copy(session_.idToken,access);
    if (*refresh) copy(session_.refreshToken,refresh);
    if (*client) copy(session_.clientToken,client);
    accessExpiresAt_ = now + lifetime;
    if (*id || !refreshing || !serviceJwt_) {
        const auto expiry = jwtExpiry(session_.idToken);
        serviceJwt_ = expiry.has_value();
        serviceWallExpiry_ = expiry.value_or(0);
        const auto wall = wallTime();
        serviceExpiresAt_ = serviceJwt_ ? now + (serviceWallExpiry_ > wall ? serviceWallExpiry_-wall : 0) : accessExpiresAt_;
    }
    refreshAt_ = now + (lifetime > 120 ? lifetime - 60 : std::max(1U,lifetime / 2));
    if (serviceJwt_) refreshAt_ = std::min(refreshAt_,serviceExpiresAt_ > now + 60 ? serviceExpiresAt_-60 : now + 30);
    return true;
}

void Login::authenticated(std::uint64_t now) noexcept {
    secureErase(deviceCode_,sizeof(deviceCode_));
    const bool saved = view_.sessionSaved;
    view_ = {}; view_.profileVerified = true; view_.sessionSaved = saved;
    if (!serviceValid(now)) {
        restoring_ = true; view_.state = State::requesting; refreshAt_ = nextPoll_ = now + 30;
        copy(view_.message,"NVIDIA cloud token expired. Retrying; CIRCLE to sign in again");
        return;
    }
    restoring_ = false; view_.state = State::authenticated;
    copy(view_.message,saved ? "NVIDIA login saved. Loading games..." :
        "NVIDIA verified; login could not be saved. Loading games...");
}
bool Login::verify(std::uint64_t now, bool refreshing) noexcept {
    const bool previousIdentity = view_.profileVerified && *session_.subject;
    auto r = request_(context_,"GET","https://login.nvidia.com/userinfo",nullptr,session_.accessToken,nullptr);
    bool receivedIdentity = false;
    if (!r.error && r.status == 200) {
        Json profile(r);
        const char* subject = string(profile.value,"sub");
        if (*subject && *session_.subject && std::strcmp(subject,session_.subject)) {
            forget(); fail("NVIDIA profile verification failed"); return false;
        }
        if (*subject && std::strlen(subject) < sizeof(session_.subject)) {
            copy(session_.subject,subject); receivedIdentity = true;
        }
    }
    if (!receivedIdentity && previousIdentity) {
        if (refreshing) view_.sessionSaved = save();
        authenticated(now); return view_.state == State::authenticated;
    }
    if (!receivedIdentity) {
        if (r.status == 401 && !r.error) {
            forget(); fail("Saved NVIDIA login expired. Press CROSS to sign in");
        } else {
            view_.state = State::requesting; restoring_ = true; nextPoll_ = now + 30;
            if (refreshing) view_.sessionSaved = save();
            copy(view_.message,"Could not verify saved login. Retrying; CIRCLE to sign out");
        }
        return false;
    }
    // tk_client supplies NVIDIA's longer-lived renewal credential.
    r = request_(context_,"GET","https://login.nvidia.com/client_token",nullptr,session_.accessToken,nullptr);
    if (!r.error && r.status == 200) {
        Json client(r);
        const char* token = string(client.value,"client_token");
        if (*token && std::strlen(token) < sizeof(session_.clientToken)) copy(session_.clientToken,token);
    }
    view_.sessionSaved = save(); authenticated(now);
    return view_.state == State::authenticated;
}

void Login::refresh(std::uint64_t now) noexcept {
    if ((!*session_.clientToken || !*session_.subject) && !*session_.refreshToken) { verify(now); refreshAt_ = now + 300; return; }
    char encoded[16384*3], subject[sizeof(session_.subject)*3], body[16384*3+4096];
    unsigned attempted = 0;
    bool allRejected = true;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        const bool client = attempt == 0;
        if (client ? !*session_.clientToken || !*session_.subject : !*session_.refreshToken) continue;
        encodeForm(client ? session_.clientToken : session_.refreshToken,encoded,sizeof(encoded));
        encodeForm(session_.subject,subject,sizeof(subject));
        if (client) std::snprintf(body,sizeof(body),
            "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Aclient_token&client_token=%s&client_id=%s&sub=%s",encoded,clientId,subject);
        else std::snprintf(body,sizeof(body),"grant_type=refresh_token&refresh_token=%s&client_id=%s",encoded,clientId);
        const auto r = request_(context_,"POST","https://login.nvidia.com/token",body,nullptr,nullptr);
        secureErase(encoded,sizeof(encoded)); secureErase(subject,sizeof(subject)); secureErase(body,sizeof(body));
        ++attempted;
        if (!r.error && r.status == 200 && acceptTokens(r,true,now)) {
            verify(now,true); return;
        }
        Json error(r);
        const char* code = string(error.value,"error");
        const bool rejected = !r.error && (r.status == 400 || r.status == 401) &&
            (!std::strcmp(code,"invalid_grant") || !std::strcmp(code,"invalid_token") ||
             !std::strcmp(code,"expired_token") || !std::strcmp(code,"token_revoked") ||
             !std::strcmp(code,"revoked"));
        allRejected = allRejected && rejected;
    }
    if (attempted && allRejected) {
        forget(); fail("Saved NVIDIA login expired. Press CROSS to sign in"); return;
    }
    // Keep disk credentials on outages, malformed replies and unexpected server errors.
    nextPoll_ = now + 30; refreshAt_ = now + 30;
    if (view_.state != State::authenticated || !serviceValid(now)) {
        restoring_ = true; view_.state = State::requesting;
        copy(view_.message,"Could not restore NVIDIA login. Retrying; CIRCLE to sign out");
    }
}

bool Login::save() noexcept {
    persistence_ = Persistence::save;
    if (deferPersistence_) return view_.sessionSaved;
    if (!sessionPath_) return false;
    char temporary[1024];
    if (std::snprintf(temporary,sizeof(temporary),"%s.tmp",sessionPath_) >= static_cast<int>(sizeof(temporary))) return false;
    const int fd = accountFile::open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if (fd < 0) return false;
    bool ok = accountFile::permissions(fd,0600) == 0;
    std::size_t offset = 0;
    while (ok && offset < sizeof(session_)) {
        const auto n = accountFile::write(fd,reinterpret_cast<const char*>(&session_)+offset,sizeof(session_)-offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        offset += static_cast<std::size_t>(n);
    }
    if (ok) ok = accountFile::sync(fd) == 0;
    if (accountFile::close(fd) != 0) ok = false;
    if (ok) ok = accountFile::rename(temporary,sessionPath_) == 0;
    if (!ok) accountFile::remove(temporary);
    return ok && accountFile::syncParent(sessionPath_);
}

bool Login::forget() noexcept {
    persistence_ = Persistence::forget;
    if (deferPersistence_) return true;
    if (!sessionPath_) return true;
    char temporary[1024];
    std::snprintf(temporary,sizeof(temporary),"%s.tmp",sessionPath_);
    accountFile::remove(temporary);
    if (accountFile::remove(sessionPath_) != 0) return errno == ENOENT;
    return accountFile::syncParent(sessionPath_);
}

bool Login::restore(char (&deviceId)[37], std::uint64_t now) noexcept {
    stopAsync(); now_ = now;
    if (!sessionPath_) return false;
    const int fd = accountFile::open(sessionPath_,O_RDONLY|O_NOFOLLOW);
    if (fd < 0) return false;
    erase();
    std::size_t offset = 0;
    while (offset < sizeof(session_)) {
        const auto n = accountFile::read(fd,reinterpret_cast<char*>(&session_)+offset,sizeof(session_)-offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        offset += static_cast<std::size_t>(n);
    }
    char extra; const bool exact = accountFile::read(fd,&extra,1) == 0;
    accountFile::close(fd);
    bool valid = offset == sizeof(session_) && exact && !std::memcmp(session_.magic,"ONAUTH1",8);
    for (const auto& field : {std::pair<const char*,std::size_t>{session_.deviceId,sizeof(session_.deviceId)},
        {session_.accessToken,sizeof(session_.accessToken)}, {session_.idToken,sizeof(session_.idToken)},
        {session_.refreshToken,sizeof(session_.refreshToken)}, {session_.clientToken,sizeof(session_.clientToken)},
        {session_.subject,sizeof(session_.subject)}}) valid = valid && std::memchr(field.first,0,field.second);
    if (!valid || std::strlen(session_.deviceId) != 36 || !*session_.accessToken || !*session_.idToken) {
        erase(); std::memcpy(session_.magic,"ONAUTH1",8); return false;
    }
    copy(deviceId,session_.deviceId);
    const auto expiry = jwtExpiry(session_.idToken);
    serviceJwt_ = expiry.has_value(); serviceWallExpiry_ = expiry.value_or(0);
    const auto wall = wallTime();
    serviceExpiresAt_ = serviceJwt_ ? now + (serviceWallExpiry_ > wall ? serviceWallExpiry_-wall : 0) : now + 3600;
    view_ = {}; view_.state = State::requesting; restoring_ = true;
    copy(view_.message,"Restoring saved NVIDIA login...");
    refresh(now);
    return true;
}
}
