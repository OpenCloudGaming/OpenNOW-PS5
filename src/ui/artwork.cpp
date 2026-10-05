// SPDX-License-Identifier: GPL-3.0-or-later
#include "artwork.hpp"
#include "../cloud.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_ASSERT(x) ((void)0)
#define STBI_MAX_DIMENSIONS 2048
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../vendor/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef OPENNOW_PS5
extern "C" int sceKernelUsleep(unsigned);
#else
#include <unistd.h>
#endif

namespace opennow::art {
namespace {
unsigned widthOf(Kind kind) noexcept {return kind==Kind::tile?tileWidth:heroWidth;}
unsigned heightOf(Kind kind) noexcept {return kind==Kind::tile?tileHeight:heroHeight;}
std::size_t pixelsOf(Kind kind) noexcept {return std::size_t(widthOf(kind))*heightOf(kind);}
}

bool requestUrl(const char* artwork,Kind kind,char* out,std::size_t capacity) noexcept {
    if(!artworkUrl(artwork))return false;
    const int n=std::snprintf(out,capacity,"%s;f=jpg;w=%u",artwork,kind==Kind::tile?272U:960U);
    return n>0&&static_cast<std::size_t>(n)<capacity;
}

bool decode(const unsigned char* data,std::size_t size,unsigned width,unsigned height,std::uint32_t* out) noexcept {
    if(!data||!out||size<4||size>maxCompressedBytes||!width||!height)return false;
    int sourceWidth=0,sourceHeight=0,components=0;
    if(!stbi_info_from_memory(data,static_cast<int>(size),&sourceWidth,&sourceHeight,&components))return false;
    if(sourceWidth<8||sourceHeight<8||sourceWidth>static_cast<int>(maxSourceDimension)||sourceHeight>static_cast<int>(maxSourceDimension))return false;
    unsigned char* rgba=stbi_load_from_memory(data,static_cast<int>(size),&sourceWidth,&sourceHeight,&components,4);
    if(!rgba)return false;
    const float scale=std::max(static_cast<float>(width)/static_cast<float>(sourceWidth),static_cast<float>(height)/static_cast<float>(sourceHeight));
    const float offsetX=(static_cast<float>(sourceWidth)*scale-static_cast<float>(width))/2;
    const float offsetY=(static_cast<float>(sourceHeight)*scale-static_cast<float>(height))/2;
    for(unsigned y=0;y<height;++y) {
        const float sy=std::clamp((static_cast<float>(y)+0.5f+offsetY)/scale-0.5f,0.0f,static_cast<float>(sourceHeight-1));
        const int y0=static_cast<int>(sy),y1=std::min(y0+1,sourceHeight-1);
        const float fy=sy-static_cast<float>(y0);
        for(unsigned x=0;x<width;++x) {
            const float sx=std::clamp((static_cast<float>(x)+0.5f+offsetX)/scale-0.5f,0.0f,static_cast<float>(sourceWidth-1));
            const int x0=static_cast<int>(sx),x1=std::min(x0+1,sourceWidth-1);
            const float fx=sx-static_cast<float>(x0);
            std::uint32_t pixel=0xff000000U;
            for(unsigned channel=0;channel<3;++channel) {
                const auto at=[&](int px,int py){return static_cast<float>(rgba[(std::size_t(py)*sourceWidth+px)*4+channel]);};
                const float top=at(x0,y0)+(at(x1,y0)-at(x0,y0))*fx;
                const float bottom=at(x0,y1)+(at(x1,y1)-at(x0,y1))*fx;
                pixel|=static_cast<std::uint32_t>(top+(bottom-top)*fy+0.5f)<<(channel*8);
            }
            out[std::size_t(y)*width+x]=pixel;
        }
    }
    stbi_image_free(rgba);
    return true;
}

Cache::Cache(Fetch fetch,void* context) noexcept:fetch_(fetch),context_(context) {
    scratch_=static_cast<std::uint32_t*>(std::malloc(pixelsOf(Kind::hero)*4));
    compressed_=static_cast<unsigned char*>(std::malloc(maxCompressedBytes));
}

Cache::~Cache() {
    for(auto& slot:slots_)std::free(slot.pixels);
    std::free(scratch_);
    std::free(compressed_);
}

Cache::Slot* Cache::find(const char* artwork,Kind kind) noexcept {
    for(auto& slot:slots_)if(slot.state!=State::missing&&slot.kind==kind&&!std::strcmp(slot.url,artwork))return &slot;
    return nullptr;
}

State Cache::want(const char* artwork,Kind kind,unsigned frame) noexcept {
    if(!artworkUrl(artwork)||std::strlen(artwork)>=sizeof(Slot::url))return State::failed;
    pthread_mutex_lock(&mutex_);
    Slot* slot=find(artwork,kind);
    if(!slot) {
        const unsigned first=kind==Kind::tile?0:tileSlots, last=kind==Kind::tile?tileSlots:tileSlots+heroSlots;
        for(unsigned i=first;i<last;++i) {
            Slot& candidate=slots_[i];
            if(candidate.fetching||(candidate.state!=State::missing&&candidate.lastUsed>=frame))continue;
            if(!slot||candidate.state==State::missing||(slot->state!=State::missing&&candidate.lastUsed<slot->lastUsed))slot=&candidate;
        }
        if(slot) {
            std::snprintf(slot->url,sizeof(slot->url),"%s",artwork);
            slot->kind=kind;slot->state=State::loading;slot->fetching=false;
        }
    }
    State state=State::loading;
    if(slot){slot->lastUsed=frame;state=slot->state;}
    pthread_mutex_unlock(&mutex_);
    return state;
}

bool Cache::draw(ps5::demo::Canvas& canvas,const char* artwork,Kind kind,float x,float y,float radius,unsigned alpha) noexcept {
    pthread_mutex_lock(&mutex_);
    const Slot* slot=find(artwork,kind);
    const bool shown=slot&&slot->state==State::ready&&slot->pixels;
    if(shown)canvas.imageRounded(static_cast<int>(std::lround(x)),static_cast<int>(std::lround(y)),widthOf(kind),heightOf(kind),slot->pixels,radius,alpha);
    pthread_mutex_unlock(&mutex_);
    return shown;
}

bool Cache::drawHero(ps5::demo::Canvas& canvas,const char* artwork,int x,int y,unsigned width,unsigned height,
                     const std::uint8_t* columnAlpha,const std::uint8_t* rowAlpha) noexcept {
    pthread_mutex_lock(&mutex_);
    const Slot* slot=find(artwork,Kind::hero);
    const bool shown=slot&&slot->state==State::ready&&slot->pixels;
    if(shown)canvas.imageFaded(x,y,width,height,slot->pixels,heroWidth,heroHeight,columnAlpha,rowAlpha);
    pthread_mutex_unlock(&mutex_);
    return shown;
}

bool Cache::step() noexcept {
    if(!ready()||stop_.load()||paused_.load())return false;
    char artwork[sizeof(Slot::url)]{};
    Kind kind=Kind::tile;
    pthread_mutex_lock(&mutex_);
    Slot* chosen=nullptr;
    for(auto& slot:slots_)
        if(slot.state==State::loading&&!slot.fetching&&(!chosen||slot.lastUsed>chosen->lastUsed))chosen=&slot;
    if(chosen){chosen->fetching=true;std::memcpy(artwork,chosen->url,sizeof(artwork));kind=chosen->kind;}
    pthread_mutex_unlock(&mutex_);
    if(!chosen)return false;
    cancel_.store(false);
    if(stop_.load()||paused_.load()) {
        cancel_.store(true);
        pthread_mutex_lock(&mutex_);
        chosen->fetching=false;
        pthread_mutex_unlock(&mutex_);
        return false;
    }
    char url[256];
    std::size_t size=0;
    bool ok=requestUrl(artwork,kind,url,sizeof(url))&&
        fetch_(context_,url,compressed_,maxCompressedBytes,size,cancel_)&&
        decode(compressed_,size,widthOf(kind),heightOf(kind),scratch_);
    pthread_mutex_lock(&mutex_);
    if(chosen->fetching&&chosen->kind==kind&&!std::strcmp(chosen->url,artwork)) {
        chosen->fetching=false;
        if(!ok&&cancel_.load()&&!stop_.load()){pthread_mutex_unlock(&mutex_);return true;}
        if(ok&&!chosen->pixels)chosen->pixels=static_cast<std::uint32_t*>(std::malloc(pixelsOf(kind)*4));
        ok=ok&&chosen->pixels;
        if(ok)std::memcpy(chosen->pixels,scratch_,pixelsOf(kind)*4);
        chosen->state=ok?State::ready:State::failed;
    }
    pthread_mutex_unlock(&mutex_);
    generation_.fetch_add(1);
    return true;
}

void* Cache::run(void* cache) noexcept {
    auto& self=*static_cast<Cache*>(cache);
    while(!self.stop_.load())
        if(!self.step()) {
#ifdef OPENNOW_PS5
            sceKernelUsleep(16000);
#else
            usleep(16000);
#endif
        }
    return nullptr;
}
}
