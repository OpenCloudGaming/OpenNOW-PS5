// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../demo_renderer.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <pthread.h>

namespace opennow::art {
enum class Kind { tile, hero };
enum class State { missing, loading, ready, failed };
constexpr unsigned tileWidth=248, tileHeight=350, heroWidth=960, heroHeight=540;
constexpr std::size_t maxCompressedBytes=std::size_t(3)<<19;
constexpr unsigned maxSourceDimension=2048;
constexpr unsigned tileSlots=36, heroSlots=2;

using Fetch=bool (*)(void* context,const char* url,unsigned char* buffer,std::size_t capacity,
                     std::size_t& size,const std::atomic_bool& stop) noexcept;

bool httpsFetch(void* context,const char* url,unsigned char* buffer,std::size_t capacity,
                std::size_t& size,const std::atomic_bool& stop) noexcept;
bool requestUrl(const char* artwork,Kind kind,char* out,std::size_t capacity) noexcept;
bool decode(const unsigned char* data,std::size_t size,unsigned width,unsigned height,std::uint32_t* out) noexcept;

class Cache {
public:
    Cache(Fetch fetch,void* context) noexcept;
    ~Cache();
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    bool ready() const noexcept {return scratch_!=nullptr&&compressed_!=nullptr;}
    State want(const char* artwork,Kind kind,unsigned frame) noexcept;
    bool draw(ps5::demo::Canvas& canvas,const char* artwork,Kind kind,float x,float y,float radius,unsigned alpha) noexcept;
    bool drawHero(ps5::demo::Canvas& canvas,const char* artwork,int x,int y,unsigned width,unsigned height,
                  const std::uint8_t* columnAlpha,const std::uint8_t* rowAlpha) noexcept;
    unsigned generation() const noexcept {return generation_.load();}
    bool step() noexcept;
    void requestStop() noexcept {stop_.store(true);cancel_.store(true);}
    void setPaused(bool paused) noexcept {paused_.store(paused);if(paused)cancel_.store(true);}
    bool paused() const noexcept {return paused_.load();}
    bool stopping() const noexcept {return stop_.load();}
    static void* run(void* cache) noexcept;
private:
    struct Slot {
        char url[192]{};
        Kind kind=Kind::tile;
        State state=State::missing;
        bool fetching=false;
        unsigned lastUsed=0;
        std::uint32_t* pixels=nullptr;
    };
    Slot* find(const char* artwork,Kind kind) noexcept;
    Fetch fetch_;
    void* context_;
    pthread_mutex_t mutex_=PTHREAD_MUTEX_INITIALIZER;
    Slot slots_[tileSlots+heroSlots]{};
    std::uint32_t* scratch_=nullptr;
    unsigned char* compressed_=nullptr;
    std::atomic_uint generation_{0};
    std::atomic_bool stop_{false};
    std::atomic_bool paused_{false};
    std::atomic_bool cancel_{false};
};
}
