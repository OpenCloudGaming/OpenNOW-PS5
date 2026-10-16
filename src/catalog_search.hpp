// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>
namespace opennow {
struct CatalogSearch {
    static constexpr char keys[]="1234567890QWERTYUIOPASDFGHJKLZXCVBNM_- .";
    char text[128]{};
    unsigned selected=0;
    bool open=false;
    static constexpr unsigned count=sizeof(keys)-1;
    static constexpr unsigned rows=5;
    static constexpr unsigned rowStart[rows+1]={0,10,20,29,36,count};
    static constexpr unsigned row(unsigned index) noexcept {
        unsigned r=0;
        while(r+1<rows&&index>=rowStart[r+1])++r;
        return r;
    }
    static constexpr unsigned column(unsigned index) noexcept {
        return index-rowStart[row(index)];
    }
    void move(int x,int y) noexcept {
        const unsigned r=row(selected), start=rowStart[r], end=rowStart[r+1];
        if(x<0)selected=selected>start?selected-1:end-1;
        if(x>0)selected=selected+1<end?selected+1:start;
        if(y) {
            const unsigned target=(r+rows+y)%rows;
            const unsigned col=column(selected), length=rowStart[target+1]-rowStart[target];
            selected=rowStart[target]+(col<length?col:length-1);
        }
    }
    void append() noexcept {
        const auto length=std::strlen(text);
        if(length+1<sizeof(text)){text[length]=keys[selected];text[length+1]=0;}
    }
    void erase() noexcept {const auto length=std::strlen(text);if(length)text[length-1]=0;}
};
}
