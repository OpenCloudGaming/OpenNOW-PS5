// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from OpenNOW-Switch gfn/authentication.cpp, MIT (upstream-lock.json).
#include "gfn.hpp"
#include "account_file.hpp"
#include "vendor/cJSON.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
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
    refreshAt_ = 0; restoring_ = false;
}
void Login::fail(const char* message, State state) noexcept {
    erase(); view_ = {}; view_.state = state;
    std::snprintf(view_.message, sizeof(view_.message), "%s", message);
}
void Login::cancel() noexcept {
    const bool removed = forget();
    fail(removed ? "Signed out. Press CROSS to sign in" :
        "Could not remove saved login. Try signing out again", State::cancelled);
}
void Login::begin(const char* deviceId, std::uint64_t now) noexcept {
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
    if (!*access || std::strlen(access) >= sizeof(session_.accessToken) ||
        std::strlen(id) >= sizeof(session_.idToken) ||
        std::strlen(refresh) >= sizeof(session_.refreshToken) ||
        std::strlen(client) >= sizeof(session_.clientToken)) return false;
    copy(session_.accessToken,access);
    if (*id) copy(session_.idToken,id);
    else if (!refreshing) copy(session_.idToken,access);
    if (*refresh) copy(session_.refreshToken,refresh);
    if (*client) copy(session_.clientToken,client);
    const unsigned lifetime = number(json.value,"expires_in",3600,604800);
    refreshAt_ = now + (lifetime > 120 ? lifetime - 60 : std::max(1U,lifetime / 2));
    return true;
}

bool Login::verify(std::uint64_t now) noexcept {
    auto r = request_(context_,"GET","https://login.nvidia.com/userinfo",nullptr,session_.accessToken,nullptr);
    if (r.error || r.status != 200) {
        // Only a definitive credential rejection discards a saved session.
        if (r.status == 401 && !r.error) {
            forget(); fail("Saved NVIDIA login expired. Press CROSS to sign in");
        } else {
            view_.state = State::requesting; restoring_ = true; nextPoll_ = now + 30;
            copy(view_.message,"Could not verify saved login. Retrying; CIRCLE to sign out");
        }
        return false;
    }
    {
        Json profile(r);
        const char* subject = string(profile.value,"sub");
        if (!*subject || (*session_.subject && std::strcmp(subject,session_.subject)) ||
            !copy(session_.subject,subject)) {
            fail("NVIDIA profile verification failed"); return false;
        }
    }
    // tk_client supplies NVIDIA's longer-lived renewal credential.
    r = request_(context_,"GET","https://login.nvidia.com/client_token",nullptr,session_.accessToken,nullptr);
    if (!r.error && r.status == 200) {
        Json client(r);
        const char* token = string(client.value,"client_token");
        if (*token && std::strlen(token) < sizeof(session_.clientToken)) copy(session_.clientToken,token);
    }
    secureErase(deviceCode_,sizeof(deviceCode_));
    restoring_ = false; view_ = {}; view_.state = State::authenticated;
    view_.profileVerified = true; view_.sessionSaved = save();
    copy(view_.message,view_.sessionSaved ? "NVIDIA login saved. Loading games..." :
        "NVIDIA verified; login could not be saved. Loading games...");
    return true;
}

void Login::refresh(std::uint64_t now) noexcept {
    if ((!*session_.clientToken || !*session_.subject) && !*session_.refreshToken) { verify(now); refreshAt_ = now + 300; return; }
    char encoded[16384*3], subject[sizeof(session_.subject)*3], body[16384*3+4096];
    Response r;
    bool rejected = false;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        const bool client = attempt == 0 && *session_.clientToken && *session_.subject;
        if (!client && !*session_.refreshToken) continue;
        encodeForm(client ? session_.clientToken : session_.refreshToken,encoded,sizeof(encoded));
        encodeForm(session_.subject,subject,sizeof(subject));
        if (client) std::snprintf(body,sizeof(body),
            "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Aclient_token&client_token=%s&client_id=%s&sub=%s",encoded,clientId,subject);
        else std::snprintf(body,sizeof(body),"grant_type=refresh_token&refresh_token=%s&client_id=%s",encoded,clientId);
        r = request_(context_,"POST","https://login.nvidia.com/token",body,nullptr,nullptr);
        secureErase(encoded,sizeof(encoded)); secureErase(subject,sizeof(subject)); secureErase(body,sizeof(body));
        rejected = false;
        if (r.error || r.status == 429 || r.status >= 500) break;
        if (r.status == 200) {
            if (!acceptTokens(r,true,now)) break;
            // Persist rotation before another request: a network interruption must not lose it.
            save(); verify(now); return;
        }
        Json error(r);
        const char* code = string(error.value,"error");
        rejected = (r.status == 400 || r.status == 401) &&
            (!std::strcmp(code,"invalid_grant") || !std::strcmp(code,"invalid_token") ||
             !std::strcmp(code,"expired_token"));
        if (!rejected) break;
        if (!client) break;
    }
    if (rejected && !r.error) {
        forget(); fail("Saved NVIDIA login expired. Press CROSS to sign in"); return;
    }
    // Keep disk credentials on outages, malformed replies and unexpected server errors.
    nextPoll_ = now + 30; refreshAt_ = now + 30;
    if (view_.state != State::authenticated) {
        restoring_ = true; view_.state = State::requesting;
        copy(view_.message,"Could not restore NVIDIA login. Retrying; CIRCLE to sign out");
    }
}

bool Login::save() noexcept {
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
    if (!sessionPath_) return true;
    char temporary[1024];
    std::snprintf(temporary,sizeof(temporary),"%s.tmp",sessionPath_);
    accountFile::remove(temporary);
    if (accountFile::remove(sessionPath_) != 0) return errno == ENOENT;
    return accountFile::syncParent(sessionPath_);
}

bool Login::restore(char (&deviceId)[37], std::uint64_t now) noexcept {
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
    view_ = {}; view_.state = State::requesting; restoring_ = true;
    copy(view_.message,"Restoring saved NVIDIA login...");
    refresh(now);
    return true;
}
}
