// SPDX-License-Identifier: GPL-3.0-or-later
#include "artwork.hpp"
#include <cstring>
#include <curl/curl.h>
#ifdef OPENNOW_PS5
#include "../platform/console_curl.h"
#endif

namespace opennow::art {
namespace {
struct Transfer {
    unsigned char* buffer;
    std::size_t capacity;
    std::size_t size;
    const std::atomic_bool* stop;
};
std::size_t write(char* bytes,std::size_t size,std::size_t count,void* context) noexcept {
    auto& transfer=*static_cast<Transfer*>(context);
    if(transfer.stop->load()||(size&&count>(transfer.capacity-transfer.size)/size))return 0;
    std::memcpy(transfer.buffer+transfer.size,bytes,size*count);
    transfer.size+=size*count;
    return size*count;
}
int progress(void* context,curl_off_t,curl_off_t,curl_off_t,curl_off_t) noexcept {
    return static_cast<Transfer*>(context)->stop->load()?1:0;
}
}

bool httpsFetch(void*,const char* url,unsigned char* buffer,std::size_t capacity,std::size_t& size,
                const std::atomic_bool& stop) noexcept {
    size=0;
    static constexpr char prefix[]="https://img.nvidiagrid.net/apps/";
    if(!url||!buffer||std::strncmp(url,prefix,sizeof(prefix)-1)||stop.load())return false;
    CURL* easy=curl_easy_init();
    if(!easy)return false;
    Transfer transfer{buffer,capacity,0,&stop};
    curl_easy_setopt(easy,CURLOPT_URL,url);
    curl_easy_setopt(easy,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(easy,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(easy,CURLOPT_PROXY,"");
    curl_easy_setopt(easy,CURLOPT_USERAGENT,"OpenNOW-PS5");
    curl_easy_setopt(easy,CURLOPT_MAXFILESIZE_LARGE,static_cast<curl_off_t>(capacity));
    curl_easy_setopt(easy,CURLOPT_CONNECTTIMEOUT,8L);
    curl_easy_setopt(easy,CURLOPT_TIMEOUT,15L);
    curl_easy_setopt(easy,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,&write);
    curl_easy_setopt(easy,CURLOPT_WRITEDATA,&transfer);
    curl_easy_setopt(easy,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(easy,CURLOPT_XFERINFOFUNCTION,&progress);
    curl_easy_setopt(easy,CURLOPT_XFERINFODATA,&transfer);
    curl_easy_setopt(easy,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(easy,CURLOPT_SSL_VERIFYHOST,2L);
#ifdef OPENNOW_PS5
    console_curl_setup(easy);
    curl_easy_setopt(easy,CURLOPT_CAINFO,"/app0/assets/cacert.pem");
#endif
    const CURLcode result=curl_easy_perform(easy);
    long status=0;
    curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(easy);
    if(result!=CURLE_OK||status!=200)return false;
    size=transfer.size;
    return size>0;
}
}
