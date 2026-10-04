// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
extern "C" {
#include "../platform/ps5-pad.h"
}

namespace opennow {
struct TouchMouse {
    static constexpr int maxStep=512;
    static constexpr std::uint8_t left=1, right=3;
    struct Motion { int dx=0, dy=0; };
    std::uint8_t held=0;
    bool clicking=false;
    int finger=-1;
    unsigned contacts=0;
    std::uint16_t x=0, y=0;
    Motion update(const PS5_PadData& pad,bool enabled) noexcept {
        Motion m;
        if(!enabled||!pad.connected) {
            held=0;clicking=true;finger=-1;contacts=0;
            return m;
        }
        const unsigned count=std::min<unsigned>(pad.touch.fingers,2);
        const PS5_PadTouch* tracked=nullptr;
        for(unsigned i=0;i<count;++i)if(pad.touch.touch[i].finger==finger)tracked=&pad.touch.touch[i];
        if(count&&count==contacts&&tracked) {
            m.dx=std::clamp(static_cast<int>(tracked->x)-x,-maxStep,maxStep);
            m.dy=std::clamp(static_cast<int>(tracked->y)-y,-maxStep,maxStep);
        } else tracked=count?&pad.touch.touch[0]:nullptr;
        contacts=count;
        finger=tracked?tracked->finger:-1;
        if(tracked){x=tracked->x;y=tracked->y;}
        const bool click=pad.buttons&PS5_PAD_BUTTON_TOUCH_PAD;
        if(click&&!clicking&&!(pad.buttons&PS5_PAD_BUTTON_OPTIONS))held=count>=2?right:left;
        if(!click)held=0;
        clicking=click;
        return m;
    }
};
}
