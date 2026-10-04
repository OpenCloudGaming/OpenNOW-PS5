// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>
namespace opennow {
struct CatalogSearch {
    static constexpr char keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ_- .1234567890";
    char text[128]{};
    unsigned selected=0;
    bool open=false;
    static constexpr unsigned count=sizeof(keys)-1;
    static constexpr unsigned row(unsigned index) noexcept {
        return index<30 ? index/10 : index==39 ? 3 : (index-30)/3;
    }
    static constexpr unsigned column(unsigned index) noexcept {
        return index<30 ? index%10 : index==39 ? 12 : 11+(index-30)%3;
    }
    void move(int x,int y) noexcept {
        const unsigned currentRow=row(selected), currentColumn=column(selected);
        if(x) {
            unsigned next=selected, edge=selected;
            bool found=false;
            for(unsigned i=0;i<count;++i)if(row(i)==currentRow) {
                const unsigned col=column(i);
                if(x>0 ? col<column(edge) : col>column(edge))edge=i;
                if((x>0 ? col>currentColumn : col<currentColumn) &&
                   (!found || (x>0 ? col<column(next) : col>column(next)))) {
                    next=i;found=true;
                }
            }
            selected=found?next:edge;
        }
        if(y) {
            const unsigned targetRow=(row(selected)+4+y)%4;
            const unsigned col=column(selected);
            unsigned best=count, distance=100;
            for(unsigned i=0;i<count;++i)if(row(i)==targetRow) {
                const unsigned at=column(i), delta=at>col?at-col:col-at;
                if(delta<distance){best=i;distance=delta;}
            }
            if(best<count)selected=best;
        }
    }
    void append() noexcept {
        const auto length=std::strlen(text);
        if(length+1<sizeof(text)){text[length]=keys[selected];text[length+1]=0;}
    }
    void erase() noexcept {const auto length=std::strlen(text);if(length)text[length-1]=0;}
};
}
