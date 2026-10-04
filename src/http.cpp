// SPDX-License-Identifier: GPL-3.0-or-later
#include "http.hpp"
#include "cloud.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef OPENNOW_PS5
#include "platform/console_curl.h"
extern "C" void opennow_network_note(const char*);
#endif
namespace opennow {
Http::Http() noexcept {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)
        buffer_ = static_cast<char*>(std::calloc(limit+1,1));
}
Http::~Http() {
    if (buffer_) { secureErase(buffer_,limit+1); std::free(buffer_); }
    curl_global_cleanup();
}
std::size_t Http::write(char* bytes,std::size_t size,std::size_t count,void* ptr) noexcept {
    auto& self = *static_cast<Http*>(ptr);
    if (size && count > (limit-self.size_)/size) return 0;
    const auto n = size*count;
    if (self.cancelled.load()) return 0;
    std::memcpy(self.buffer_+self.size_,bytes,n); self.size_+=n; self.buffer_[self.size_]=0;
    return n;
}
int Http::progress(void* ptr,curl_off_t,curl_off_t,curl_off_t,curl_off_t) noexcept {
    return static_cast<Http*>(ptr)->cancelled.load() ? 1 : 0;
}
Response Http::request(void* ptr,const char* method,const char* url,const char* body,const char* bearer,const char* device) noexcept {
    auto& self = *static_cast<Http*>(ptr);
    if (!self.buffer_) return {0,nullptr,0,"Could not initialize HTTPS"};
    secureErase(self.buffer_,self.size_); self.size_=0;
    if (self.cancelled.load()) return {0,nullptr,0,"Request cancelled"};
    const bool cloud=trustedCloudUrl(url);
    if (!cloud && std::strncmp(url,"https://login.nvidia.com/",25) != 0)
        return {0,nullptr,0,"Endpoint not permitted"};
    self.error_[0]=0;
    auto* easy = curl_easy_init();
    if (!easy) return {0,nullptr,0,"Could not create HTTPS request"};
    curl_slist* headers = nullptr;
    bool headersOk = true;
    auto append = [&](const char* h) {
        auto* next = curl_slist_append(headers,h);
        if (next) headers=next; else headersOk=false;
    };
    append("Origin: https://play.geforcenow.com");
    append("Referer: https://play.geforcenow.com/");
    append("Accept: application/json, text/plain, */*");
    append(cloud ? "Content-Type: application/json" : "Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
    char authorization[16512]{};
    if (bearer) { std::snprintf(authorization,sizeof(authorization),"Authorization: %s %s",cloud ? "GFNJWT" : "Bearer",bearer); append(authorization); }
    if (cloud) {
        append("nv-client-id: ec7e38d4-03af-4b58-b131-cfb0495903ab");append("nv-client-type: NATIVE");
        append("nv-client-streamer: NVIDIA-CLASSIC");append("nv-client-version: 2.0.80.173");
        append("nv-device-os: WINDOWS");append("nv-device-type: DESKTOP");append("nv-device-make: UNKNOWN");append("nv-device-model: UNKNOWN");append("nv-browser-type: CHROME");
        if(device){char id[256];std::snprintf(id,sizeof(id),"x-device-id: %s",device);append(id);}
    }
    if (device && !cloud) {
        char id[256]; std::snprintf(id,sizeof(id),"x-device-id: %s",device); append(id);
        append("nv-client-id: q61ddeJrVt7O90Nl-P-N7I36yctih4Ml6FyXLrb6j-U");
        append("nv-client-streamer: WEBRTC"); append("nv-client-type: BROWSER");
        append("nv-client-platform-name: browser"); append("nv-browser-type: CHROME");
        append("nv-device-os: STEAMOS"); append("nv-device-type: CONSOLE");
        append("nv-device-model: STEAMDECK"); append("nv-device-make: VALVE");
    }
    curl_easy_setopt(easy,CURLOPT_ERRORBUFFER,self.error_);
    curl_easy_setopt(easy,CURLOPT_URL,url);
    curl_easy_setopt(easy,CURLOPT_USERAGENT,"Mozilla/5.0 (X11; Linux x86_64; Steam Deck) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
    curl_easy_setopt(easy,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(easy,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(easy,CURLOPT_PROXY,"");
    curl_easy_setopt(easy,CURLOPT_CONNECTTIMEOUT,10L);
    curl_easy_setopt(easy,CURLOPT_TIMEOUT,20L);
    curl_easy_setopt(easy,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,&Http::write);
    curl_easy_setopt(easy,CURLOPT_WRITEDATA,&self);
    curl_easy_setopt(easy,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(easy,CURLOPT_XFERINFOFUNCTION,&Http::progress);
    curl_easy_setopt(easy,CURLOPT_XFERINFODATA,&self);
    curl_easy_setopt(easy,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(easy,CURLOPT_SSL_VERIFYHOST,2L);
#ifdef OPENNOW_PS5
    console_curl_setup(easy);
    curl_easy_setopt(easy,CURLOPT_CAINFO,"/app0/assets/cacert.pem");
#endif
    if (std::strcmp(method,"POST")==0) {
        curl_easy_setopt(easy,CURLOPT_POST,1L);
        curl_easy_setopt(easy,CURLOPT_POSTFIELDS,body ? body : "");
        curl_easy_setopt(easy,CURLOPT_POSTFIELDSIZE,static_cast<long>(body ? std::strlen(body) : 0));
    }
    if (!std::strcmp(method,"DELETE")) curl_easy_setopt(easy,CURLOPT_CUSTOMREQUEST,"DELETE");
    const CURLcode result = headersOk ? curl_easy_perform(easy) : CURLE_OUT_OF_MEMORY;
    long status=0; curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&status);
#ifdef OPENNOW_PS5
    if (result!=CURLE_OK) {
        long osError=0;curl_easy_getinfo(easy,CURLINFO_OS_ERRNO,&osError);
        char note[384];
        std::snprintf(note,sizeof(note),"curl=%d os_errno=%ld %s",static_cast<int>(result),osError,self.error_);
        opennow_network_note(note);
    }
#endif
    curl_easy_cleanup(easy);
    // curl duplicates header strings; clear the bearer before freeing its list.
    for (auto* h=headers; h; h=h->next) if (h->data) secureErase(h->data,std::strlen(h->data));
    curl_slist_free_all(headers); secureErase(authorization,sizeof(authorization));
    return {status,self.buffer_,self.size_,result == CURLE_OK ? nullptr : (self.error_[0] ? self.error_ : curl_easy_strerror(result))};
}
}
