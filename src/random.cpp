// SPDX-License-Identifier: GPL-3.0-or-later
// Native entropy mechanism adapted from ProsperoLight platform/ps5/ps5_entropy.c.
#include "random.hpp"
#include <cstring>
extern "C" int sysctlbyname(const char*,void*,std::size_t*,const void*,std::size_t);
namespace opennow {
namespace {
void wipe(void* p,std::size_t n) noexcept {
    auto* bytes=static_cast<volatile unsigned char*>(p);
    while (n--) *bytes++=0;
}
}
bool randomBytes(void* output,std::size_t length) noexcept {
    if (!output && length) return false;
    auto* destination=static_cast<unsigned char*>(output);
    std::size_t remaining=length;
    while (remaining) {
        unsigned char block[64]{};
        std::size_t received=sizeof(block);
        const auto count=remaining<sizeof(block) ? remaining : sizeof(block);
        if (sysctlbyname("kern.rng_pseudo",block,&received,nullptr,0)!=0 || received<count || received>sizeof(block)) {
            wipe(block,sizeof(block));wipe(output,length);return false;
        }
        std::memcpy(destination,block,count);wipe(block,sizeof(block));
        destination+=count;remaining-=count;
    }
    return true;
}
}
