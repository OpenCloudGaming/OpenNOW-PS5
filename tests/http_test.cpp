// SPDX-License-Identifier: GPL-3.0-or-later
#include "http.hpp"
#include "cloud_endpoint.hpp"
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>

namespace {
std::map<CURLoption,std::string> strings;
curl_slist* requestHeaders=nullptr;
std::map<std::string,std::string> headers;
unsigned performed=0;
long bodyLength=-1;
void reset(){strings.clear();headers.clear();requestHeaders=nullptr;bodyLength=-1;}
}
namespace opennow {
bool trustedCloudUrl(const char* url) noexcept {
    return std::strncmp(url,"https://test.geforcenow.com/",26)==0||
           std::strncmp(url,"https://games.geforce.com/",25)==0;
}
void secureErase(void* data,std::size_t size) noexcept {volatile auto* p=static_cast<unsigned char*>(data);while(size--)*p++=0;}
}
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
#endif
extern "C" CURLcode curl_easy_setopt(CURL*,CURLoption option,...) {
    va_list args;va_start(args,option);
    if(option==CURLOPT_HTTPHEADER)requestHeaders=va_arg(args,curl_slist*);
    else if(option==CURLOPT_POSTFIELDSIZE)bodyLength=va_arg(args,long);
    else if(option==CURLOPT_URL||option==CURLOPT_USERAGENT||option==CURLOPT_CUSTOMREQUEST||option==CURLOPT_POSTFIELDS) {
        const auto* value=va_arg(args,const char*);strings[option]=value?value:"";
    }
    va_end(args);return CURLE_OK;
}
extern "C" CURLcode curl_easy_perform(CURL*) {
    ++performed;
    for(auto* item=requestHeaders;item;item=item->next) {
        const std::string line=item->data;const auto colon=line.find(": ");assert(colon!=std::string::npos);
        headers[line.substr(0,colon)]=line.substr(colon+2);
    }
    return CURLE_OK;
}
extern "C" CURLcode curl_easy_getinfo(CURL*,CURLINFO info,...) {
    va_list args;va_start(args,info);*va_arg(args,long*)=200;va_end(args);return CURLE_OK;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
int main() {
    opennow::Http http;assert(http.ready());
    const char* ua="GFN-PC/30.0 (Windows NT 10.0) BifrostClientSDK/4.9 (38495286)";
    const char* chrome="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36";
    auto browserIdentity=[&]{
        return headers["nv-client-id"]=="ec7e38d4-03af-4b58-b131-cfb0495903ab"&&headers["nv-client-type"]=="BROWSER"&&
            headers["nv-client-streamer"]=="WEBRTC"&&headers["nv-client-version"]=="2.0.89.154"&&headers["nv-browser-type"]=="CHROME"&&
            headers["nv-device-os"]=="WINDOWS"&&headers["nv-device-type"]=="DESKTOP"&&!headers.count("x-nv-client-identity")&&
            headers["x-device-id"]=="synthetic-device"&&strings[CURLOPT_USERAGENT]==chrome;
    };
    reset();auto result=opennow::Http::request(&http,"PUT","https://test.geforcenow.com/v2/session/test","{\"action\":2}","synthetic-token","synthetic-device");
    assert(!result.error);
    assert(strings[CURLOPT_CUSTOMREQUEST]=="PUT"&&strings[CURLOPT_POSTFIELDS]=="{\"action\":2}"&&bodyLength==12);
    assert(headers["Content-Type"]=="text/plain");
    assert(headers["Authorization"]=="GFNJWT synthetic-token");
    assert(browserIdentity());
    reset();result=opennow::Http::request(&http,"POST","https://test.geforcenow.com/v2/nettestsession","{}","synthetic-token","synthetic-device");
    assert(!result.error&&headers["Content-Type"]=="application/json"&&browserIdentity());
    reset();result=opennow::Http::request(&http,"POST","https://games.geforce.com/graphql","{}","synthetic-token","synthetic-device");
    assert(!result.error&&headers["Content-Type"]=="application/json");
    assert(headers["nv-client-type"]=="NATIVE"&&headers["nv-client-streamer"]=="NVIDIA-CLASSIC"&&headers["nv-client-version"]=="2.0.87.131");
    assert(!headers.count("nv-browser-type")&&headers["x-nv-client-identity"]==ua&&strings[CURLOPT_USERAGENT]==ua);
    reset();result=opennow::Http::request(&http,"POST","https://login.nvidia.com/token","grant_type=refresh_token",nullptr,nullptr);
    assert(!result.error&&headers["Content-Type"]=="application/x-www-form-urlencoded; charset=UTF-8");
    const auto before=performed;
    for(const char* token:{"bad\r\nInjected: value","bad\x7f", "bad\tvalue"}) {
        result=opennow::Http::request(&http,"GET","https://test.geforcenow.com/v2/session",nullptr,token,"synthetic-device");
        assert(result.error&&performed==before);
    }
    result=opennow::Http::request(&http,"GET","https://test.geforcenow.com/v2/session",nullptr,"synthetic-token","bad\nvalue");
    assert(result.error&&performed==before);
    result=opennow::Http::request(&http,"GET","https://127.0.0.1/v2/session",nullptr,"synthetic-token","synthetic-device");
    assert(result.error&&performed==before);
    for(const char* bad:{"http://8.8.8.8/","https://127.0.0.1/","https://10.0.0.1/","https://100.64.1.2/",
        "https://169.254.1.1/","https://172.31.1.1/","https://192.168.1.1/","https://224.0.0.1/","https://240.1.1.1/",
        "https://255.255.255.255/","https://0.0.0.0/","https://0x7f.0.0.1/","https://0177.1/","https://2130706433/",
        "https://[::1]/","https://[::]/","https://[fe80::1]/","https://[fc00::1]/","https://[ff02::1]/",
        "https://[::ffff:127.0.0.1]/","https://test.local/","https://test.LOCAL./","https://localhost/",
        "https://public.example:80/","https://public.example:444/","https://user@public.example/",
        "https://public.example\\@evil.example/","https://public.example/#fragment","https://public..example/"}) {
        assert(!opennow::publicCloudControlUrl(bad));
        assert(!opennow::Http::allowCloudEndpoint(&http,bad));
    }
    for(const char* good:{"https://8.8.8.8/","https://public.example/","https://PUBLIC.EXAMPLE:443/v2/session",
        "https://[2001:4860:4860::8888]/","https://[::ffff:8.8.8.8]/"}) {
        assert(opennow::publicCloudControlUrl(good));
        assert(opennow::Http::allowCloudEndpoint(&http,good));
    }
    assert(opennow::sameCloudControlOrigin("https://PUBLIC.EXAMPLE:443/v2/session","https://public.example/"));
    reset();result=opennow::Http::request(&http,"GET","https://8.8.8.8/v2/session/test",nullptr,"synthetic-token","synthetic-device");
    assert(!result.error&&headers["Authorization"]=="GFNJWT synthetic-token");
    const auto learned=performed;
    for(const char* bad:{"https://8.8.4.4/v2/session/test","https://public.example.evil.test/v2/session/test",
                         "https://public.example:444/v2/session/test"}) {
        result=opennow::Http::request(&http,"GET",bad,nullptr,"synthetic-token","synthetic-device");
        assert(result.error&&performed==learned);
    }
    std::puts("CloudMatch HTTP identity, content types, PUT bodies and header rejection passed");
}
