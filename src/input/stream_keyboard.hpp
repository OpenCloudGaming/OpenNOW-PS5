// SPDX-License-Identifier: GPL-3.0-or-later
// US-layout virtual-key and scan-code tables adapted from OpenNOW Android StreamInputEncoder.kt, MIT, Copyright (c) 2025 Zortos (assets/licenses/OpenNOW-Android.txt).
#pragma once
#include "input_queue.hpp"

namespace opennow {
struct StreamKeyboard {
    enum class Action : std::uint8_t { type, shift, caps, ctrl, alt, backspace, space };
    struct Key { const char* label; const char* shifted; std::uint16_t vk, scan; std::uint8_t width; Action action; };
    static constexpr unsigned rows=5, units=15;
    static constexpr Key keys[]={
        {"`","~",0xc0,0x29,1,Action::type},{"1","!",0x31,0x02,1,Action::type},{"2","@",0x32,0x03,1,Action::type},
        {"3","#",0x33,0x04,1,Action::type},{"4","$",0x34,0x05,1,Action::type},{"5","%",0x35,0x06,1,Action::type},
        {"6","^",0x36,0x07,1,Action::type},{"7","&",0x37,0x08,1,Action::type},{"8","*",0x38,0x09,1,Action::type},
        {"9","(",0x39,0x0a,1,Action::type},{"0",")",0x30,0x0b,1,Action::type},{"-","_",0xbd,0x0c,1,Action::type},
        {"=","+",0xbb,0x0d,1,Action::type},{"Backspace","Backspace",0x08,0x0e,2,Action::backspace},
        {"Tab","Tab",0x09,0x0f,2,Action::type},{"q","Q",0x51,0x10,1,Action::type},{"w","W",0x57,0x11,1,Action::type},
        {"e","E",0x45,0x12,1,Action::type},{"r","R",0x52,0x13,1,Action::type},{"t","T",0x54,0x14,1,Action::type},
        {"y","Y",0x59,0x15,1,Action::type},{"u","U",0x55,0x16,1,Action::type},{"i","I",0x49,0x17,1,Action::type},
        {"o","O",0x4f,0x18,1,Action::type},{"p","P",0x50,0x19,1,Action::type},{"[","{",0xdb,0x1a,1,Action::type},
        {"]","}",0xdd,0x1b,1,Action::type},{"\\","|",0xdc,0x2b,1,Action::type},
        {"Caps","Caps",0,0,2,Action::caps},{"a","A",0x41,0x1e,1,Action::type},{"s","S",0x53,0x1f,1,Action::type},
        {"d","D",0x44,0x20,1,Action::type},{"f","F",0x46,0x21,1,Action::type},{"g","G",0x47,0x22,1,Action::type},
        {"h","H",0x48,0x23,1,Action::type},{"j","J",0x4a,0x24,1,Action::type},{"k","K",0x4b,0x25,1,Action::type},
        {"l","L",0x4c,0x26,1,Action::type},{";",":",0xba,0x27,1,Action::type},{"'","\"",0xde,0x28,1,Action::type},
        {"Enter","Enter",0x0d,0x1c,2,Action::type},
        {"Shift","Shift",0,0,3,Action::shift},{"z","Z",0x5a,0x2c,1,Action::type},{"x","X",0x58,0x2d,1,Action::type},
        {"c","C",0x43,0x2e,1,Action::type},{"v","V",0x56,0x2f,1,Action::type},{"b","B",0x42,0x30,1,Action::type},
        {"n","N",0x4e,0x31,1,Action::type},{"m","M",0x4d,0x32,1,Action::type},{",","<",0xbc,0x33,1,Action::type},
        {".",">",0xbe,0x34,1,Action::type},{"/","?",0xbf,0x35,1,Action::type},{"Up","Up",0x26,0x148,2,Action::type},
        {"Esc","Esc",0x1b,0x01,2,Action::type},{"Ctrl","Ctrl",0,0,2,Action::ctrl},{"Alt","Alt",0,0,2,Action::alt},
        {"Space","Space",0x20,0x39,6,Action::space},{"Left","Left",0x25,0x14b,1,Action::type},
        {"Down","Down",0x28,0x150,1,Action::type},{"Right","Right",0x27,0x14d,1,Action::type},
    };
    static constexpr unsigned count=sizeof(keys)/sizeof(keys[0]);
    static constexpr unsigned rowStart[rows+1]={0,14,28,41,53,count};
    static constexpr unsigned backspaceKey=13, spaceKey=56;
    bool open=false, shift=false, caps=false, ctrl=false, alt=false;
    unsigned selected=0;

    static constexpr unsigned row(unsigned index) noexcept {
        unsigned r=0;
        while(r+1<rows&&index>=rowStart[r+1])++r;
        return r;
    }
    static constexpr unsigned column(unsigned index) noexcept {
        unsigned at=0;
        for(unsigned i=rowStart[row(index)];i<index;++i)at+=keys[i].width;
        return at;
    }
    static constexpr bool letter(const Key& k) noexcept {return k.label[0]>='a'&&k.label[0]<='z'&&!k.label[1];}
    bool upper(const Key& k) const noexcept {return letter(k)?shift!=caps:shift;}
    const char* label(unsigned index) const noexcept {return upper(keys[index])?keys[index].shifted:keys[index].label;}
    bool latched(unsigned index) const noexcept {
        switch(keys[index].action) {
        case Action::shift: return shift;
        case Action::caps: return caps;
        case Action::ctrl: return ctrl;
        case Action::alt: return alt;
        default: return false;
        }
    }
    void show() noexcept {open=true;shift=ctrl=alt=false;selected=rowStart[1]+1;}
    void hide() noexcept {open=shift=ctrl=alt=false;}
    void move(int x,int y) noexcept {
        const unsigned r=row(selected);
        if(x<0&&selected>rowStart[r])--selected;
        if(x>0&&selected+1<rowStart[r+1])++selected;
        if(!y)return;
        if((y<0&&r==0)||(y>0&&r+1==rows))return;
        const unsigned target=y<0?r-1:r+1;
        const unsigned centre=column(selected)*2+keys[selected].width;
        unsigned next=rowStart[target];
        while(next+1<rowStart[target+1]&&(column(next)+keys[next].width)*2<=centre)++next;
        selected=next;
    }
    bool press(unsigned index,InputQueue& queue) noexcept {
        const Key& k=keys[index];
        switch(k.action) {
        case Action::shift: shift=!shift;return true;
        case Action::caps: caps=!caps;return true;
        case Action::ctrl: ctrl=!ctrl;return true;
        case Action::alt: alt=!alt;return true;
        default: break;
        }
        const std::uint8_t modifiers=(upper(k)?modifierShift:0)|(ctrl?modifierCtrl:0)|(alt?modifierAlt:0);
        if(!queue.key({k.vk,k.scan,modifiers}))return false;
        shift=ctrl=alt=false;
        return true;
    }
};
static_assert(StreamKeyboard::keys[StreamKeyboard::backspaceKey].action==StreamKeyboard::Action::backspace);
static_assert(StreamKeyboard::keys[StreamKeyboard::spaceKey].action==StreamKeyboard::Action::space);
}
