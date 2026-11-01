// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>

namespace opennow {
constexpr unsigned maxRegions=32;
constexpr std::size_t regionNameSize=128;
// An empty preference means NVIDIA's automatic routing. Persist identifiers,
// never URLs: manual endpoints must be rediscovered from trusted server info.
inline bool validRegionName(const char* name) noexcept {
    if(!name)return false;
    for(std::size_t i=0;i<regionNameSize;++i) {
        const unsigned char ch=name[i];
        if(!ch)return true;
        if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'))return false;
    }
    return false;
}
struct NetworkView {
    char names[maxRegions][regionNameSize]{};
    unsigned count=0;
    char preferred[regionNameSize]{};
    char endpoint[272]{};
    char server[160]{};
    char error[192]{};
    int saveError=0;
};
}
