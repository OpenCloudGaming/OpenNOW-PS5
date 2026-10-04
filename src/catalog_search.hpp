// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>
namespace opennow {
struct CatalogSearch {
    static constexpr char keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-'. ";
    char text[128]{};
    unsigned selected=0;
    bool open=false;
    void move(int x,int y) noexcept {
        constexpr unsigned count=sizeof(keys)-1, rows=(count+9)/10;
        const unsigned row=(selected/10+rows+y)%rows;
        const unsigned columns=row==rows-1 ? count-row*10 : 10;
        unsigned column=selected%10;
        if(column>=columns)column=columns-1;
        column=(column+columns+x)%columns;
        selected=row*10+column;
    }
    void append() noexcept {
        const auto length=std::strlen(text);
        if(length+1<sizeof(text)){text[length]=keys[selected];text[length+1]=0;}
    }
    void erase() noexcept {const auto length=std::strlen(text);if(length)text[length-1]=0;}
};
}
