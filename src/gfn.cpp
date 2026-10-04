// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from OpenNOW-Switch gfn/authentication.cpp, MIT (upstream-lock.json).
#include "gfn.hpp"
#include "vendor/cJSON.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

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
    secureErase(accessToken_, sizeof(accessToken_));
    secureErase(idToken_, sizeof(idToken_));
}
void Login::fail(const char* message, State state) noexcept {
    erase(); view_ = {}; view_.state = state;
    std::snprintf(view_.message, sizeof(view_.message), "%s", message);
}
void Login::cancel() noexcept { fail("Signed out. Press CROSS to sign in", State::cancelled); }
void Login::begin(const char* deviceId, std::uint64_t now) noexcept {
    erase(); view_ = {}; view_.state = State::requesting;
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
            success = copy(accessToken_,string(json.value,"access_token"));
            const char* id=string(json.value,"id_token");
            if (*id && !copy(idToken_,id)) { fail("Oversized identity token"); return; }
            if (!*id) copy(idToken_,accessToken_);
            if (!success) { fail("Missing or oversized access token"); return; }
        } else {
            const auto* error = string(json.value,"error");
            if (std::strcmp(error,"authorization_pending") == 0) return;
            if (std::strcmp(error,"slow_down") == 0) { interval_ = std::min(interval_+5,300U); nextPoll_=now+interval_; return; }
            if (std::strcmp(error,"access_denied") == 0) { fail("Login denied. Press CROSS to retry",State::denied); return; }
            if (std::strcmp(error,"expired_token") == 0) { fail("Login code expired. Press CROSS to retry",State::expired); return; }
            fail("NVIDIA rejected the login request"); return;
        }
    }
    if (success) {
        // Verify the token against NVIDIA. No email, username or token is logged or persisted.
        r = request_(context_,"GET","https://login.nvidia.com/userinfo",nullptr,accessToken_,nullptr);
        if (r.error) { fail(r.error); return; }
        Json profile(r);
        if (r.status != 200 || !*string(profile.value,"sub")) { fail("Login token received; profile verification failed"); return; }
        secureErase(deviceCode_,sizeof(deviceCode_));secureErase(accessToken_,sizeof(accessToken_)); view_ = {}; view_.state = State::authenticated; view_.profileVerified = true;
        copy(view_.message,"NVIDIA account verified. Loading games...");
    }
}
}
