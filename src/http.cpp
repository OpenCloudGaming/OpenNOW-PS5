// SPDX-License-Identifier: GPL-3.0-or-later
#include "http.hpp"
#include "app_storage.h"
#include "cloud.hpp"
#include "cloud_endpoint.hpp"
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
bool Http::allowCloudEndpoint(void* ptr,const char* url) noexcept {
    auto& self=*static_cast<Http*>(ptr);
    char origin[272];if(!cloudControlOrigin(url,origin,sizeof(origin)))return false;
    for(unsigned i=0;i<self.cloudOriginCount_;++i)if(!std::strcmp(self.cloudOrigins_[i],origin))return true;
    if(self.cloudOriginCount_==64)return false;
    std::memcpy(self.cloudOrigins_[self.cloudOriginCount_++],origin,std::strlen(origin)+1);
    return true;
}
Response Http::request(void* ptr,const char* method,const char* url,const char* body,const char* bearer,const char* device) noexcept {
    auto& self = *static_cast<Http*>(ptr);
    if (!self.buffer_) return {0,nullptr,0,"Could not initialize HTTPS"};
    secureErase(self.buffer_,self.size_); self.size_=0;
    if (self.cancelled.load()) return {0,nullptr,0,"Request cancelled"};
    bool cloud=trustedCloudUrl(url);
    if(!cloud)for(unsigned i=0;i<self.cloudOriginCount_;++i)
        if(sameCloudControlOrigin(url,self.cloudOrigins_[i])){cloud=true;break;}
    if (!cloud && std::strncmp(url,"https://login.nvidia.com/",25) != 0)
        return {0,nullptr,0,"Endpoint not permitted"};
    for(const char* value:{bearer,device})if(value) {
        for(const auto* p=reinterpret_cast<const unsigned char*>(value);*p;++p)
            if(*p<32||*p==127)return {0,nullptr,0,"Invalid HTTP header value"};
    }
    if((bearer&&std::strlen(bearer)>16384)||(device&&std::strlen(device)>240))
        return {0,nullptr,0,"Oversized HTTP header value"};
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
    const auto* path=std::strchr(url+8,'/');
    const bool cloudmatch=cloud&&path&&std::strncmp(path,"/v2/",4)==0;
    const bool nettest=cloudmatch&&std::strncmp(path,"/v2/nettestsession",18)==0;
    append(cloudmatch&&!nettest?"Content-Type: text/plain":cloud?"Content-Type: application/json":"Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
    char authorization[16512]{};
    if (bearer) { std::snprintf(authorization,sizeof(authorization),"Authorization: %s %s",cloud ? "GFNJWT" : "Bearer",bearer); append(authorization); }
    constexpr const char* nativeAgent="GFN-PC/30.0 (Windows NT 10.0) BifrostClientSDK/4.9 (38495286)";
    constexpr const char* browserAgent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36";
    if (cloud) {
        // Since 2026-10-27 CloudMatch only assigns a WebRTC streamer (and sends an SDP offer) to a browser identity.
        append("nv-client-id: ec7e38d4-03af-4b58-b131-cfb0495903ab");
        if(cloudmatch){append("nv-client-type: BROWSER");append("nv-client-streamer: WEBRTC");append("nv-client-version: 2.0.89.154");append("nv-browser-type: CHROME");}
        else {append("nv-client-type: NATIVE");append("nv-client-streamer: NVIDIA-CLASSIC");append("nv-client-version: 2.0.87.131");}
        append("nv-device-os: WINDOWS");append("nv-device-type: DESKTOP");append("nv-device-make: UNKNOWN");append("nv-device-model: UNKNOWN");
        if(!cloudmatch){char identity[128];std::snprintf(identity,sizeof(identity),"x-nv-client-identity: %s",nativeAgent);append(identity);}
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
    curl_easy_setopt(easy,CURLOPT_USERAGENT,cloudmatch?browserAgent:cloud?nativeAgent:"Mozilla/5.0 (X11; Linux x86_64; Steam Deck) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
    curl_easy_setopt(easy,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(easy,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(easy,CURLOPT_PROXY,"");
    curl_easy_setopt(easy,CURLOPT_CONNECTTIMEOUT,10L);
    const bool discovery=cloudmatch&&!std::strcmp(method,"GET")&&
        (!std::strncmp(path,"/v2/serverInfo",14)||!std::strcmp(path,"/v2/session"));
    curl_easy_setopt(easy,CURLOPT_TIMEOUT,!cloud?5L:nettest||!std::strcmp(method,"DELETE")?8L:discovery?3L:20L);
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
    curl_easy_setopt(easy,CURLOPT_CAINFO,OPENNOW_CACERT_PATH);
#endif
    if (!std::strcmp(method,"POST")||!std::strcmp(method,"PUT")||!std::strcmp(method,"PATCH")) {
        if(!std::strcmp(method,"POST"))curl_easy_setopt(easy,CURLOPT_POST,1L);
        else curl_easy_setopt(easy,CURLOPT_CUSTOMREQUEST,method);
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
