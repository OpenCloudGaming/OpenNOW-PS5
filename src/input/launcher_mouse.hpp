// SPDX-License-Identifier: GPL-3.0-or-later
// Right-stick pointer, trigger clicks, D-pad wheel and speed steps adapted from
// Portablelle/OpenNOW-PS5 123eb46 (Stream::input virtual mouse mode).
#pragma once
#include <algorithm>
#include <cstdint>
extern "C" {
#include "../platform/ps5-pad.h"
}

namespace opennow {
struct LauncherMouse {
    static constexpr std::uint64_t wheelIntervalUs=150000;
    static constexpr int wheelStep=120;
    static constexpr float speeds[3]={250.f,700.f,1400.f};
    struct Frame { int dx=0, dy=0, wheelX=0, wheelY=0; bool left=false, right=false; };
    bool active=false;
    unsigned speed=1;
    float x=0, y=0;
    std::uint64_t last=0, nextWheel=0;
    static float axis(unsigned v) noexcept {
        const int n=static_cast<int>(v)-128;
        return n>-16&&n<16?0.f:float(n>0?n-16:n+16)/111.f;
    }
    void reset() noexcept {x=y=0;last=nextWheel=0;}
    Frame update(const PS5_PadData& pad,std::uint64_t now,bool enabled) noexcept {
        Frame f;
        if(!enabled||!active||!pad.connected){reset();return f;}
        const auto elapsed=last?std::min<std::uint64_t>(now-last,50000):16000;
        last=now;
        f.left=(pad.buttons&PS5_PAD_BUTTON_R2)||pad.analogButtons.r2>32;
        f.right=(pad.buttons&PS5_PAD_BUTTON_L2)||pad.analogButtons.l2>32;
        x+=axis(pad.rightStick.x)*speeds[speed]*float(elapsed)/1000000.f;
        y+=axis(pad.rightStick.y)*speeds[speed]*float(elapsed)/1000000.f;
        f.dx=static_cast<int>(x);f.dy=static_cast<int>(y);
        x-=float(f.dx);y-=float(f.dy);
        x=std::clamp(x,-100.f,100.f);y=std::clamp(y,-100.f,100.f);
        if(now>=nextWheel) {
            f.wheelY=((pad.buttons&PS5_PAD_BUTTON_UP)?wheelStep:0)-((pad.buttons&PS5_PAD_BUTTON_DOWN)?wheelStep:0);
            f.wheelX=((pad.buttons&PS5_PAD_BUTTON_RIGHT)?wheelStep:0)-((pad.buttons&PS5_PAD_BUTTON_LEFT)?wheelStep:0);
        }
        return f;
    }
    void wheelSent(std::uint64_t now) noexcept {nextWheel=now+wheelIntervalUs;}
    void cycleSpeed() noexcept {speed=(speed+1)%3;}
};
}
