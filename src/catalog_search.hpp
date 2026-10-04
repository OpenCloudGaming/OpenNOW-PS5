// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>
namespace opennow {
struct CatalogSearch {
    static constexpr char keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -'.";
    char text[128]{};
    unsigned selected=0;
    bool open=false;
    void move(int x,int y) noexcept {
        selected=((selected/10+4+y)%4)*10+(selected%10+10+x)%10;
    }
    void append() noexcept {
        const auto length=std::strlen(text);
        if(length+1<sizeof(text)){text[length]=keys[selected];text[length+1]=0;}
    }
    void erase() noexcept {const auto length=std::strlen(text);if(length)text[length-1]=0;}
};
}
