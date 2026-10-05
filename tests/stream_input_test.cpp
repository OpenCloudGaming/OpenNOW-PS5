// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/input_wire.hpp"
#include "input/input_queue.hpp"
#include "input/stream_keyboard.hpp"
#include "input/touch_mouse.hpp"
#include "input/launcher_mouse.hpp"

#include <cassert>
#include <cstring>

namespace {
using opennow::wire::Bytes;
constexpr std::uint64_t stamp=0x0102030405060708ULL;
#define T8 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08

void wireFixtures() {
    using namespace opennow::wire;
    assert((key(true,0x41,0x1e,1,3,stamp)==Bytes{0x23,T8,0x22,0x03,0,0,0,0x00,0x41,0x00,0x01,0x00,0x1e,T8}));
    assert((key(true,0x41,0x1e,1,2,stamp)==Bytes{0x03,0,0,0,0x00,0x41,0x00,0x01,0x00,0x1e,T8}));
    assert((key(false,0x0d,0x1c,0,2,0x10)==Bytes{0x04,0,0,0,0x00,0x0d,0,0,0x00,0x1c,0,0,0,0,0,0,0,0x10}));
    assert((key(false,0x26,0x148,0,3,stamp)==Bytes{0x23,T8,0x22,0x04,0,0,0,0x00,0x26,0,0,0x01,0x48,T8}));
    assert((mouseMove(-5,300,3,stamp)==Bytes{0x23,T8,0x21,0x00,0x16,0x07,0,0,0,0xff,0xfb,0x01,0x2c,0,0,0,0,0,0,T8}));
    assert((mouseMove(-32768,32767,2,stamp)==Bytes{0x07,0,0,0,0x80,0x00,0x7f,0xff,0,0,0,0,0,0,T8}));
    assert((mouseButton(true,3,3,stamp)==Bytes{0x23,T8,0x22,0x08,0,0,0,0x03,0,0,0,0,0,T8}));
    assert((mouseButton(false,1,2,stamp)==Bytes{0x09,0,0,0,0x01,0,0,0,0,0,T8}));
    assert((mouseWheel(-120,120,2,stamp)==Bytes{0x0a,0,0,0,0xff,0x88,0,0x78,0,0,0,0,0,0,T8}));
    assert((mouseWheel(120,-120,3,stamp)==Bytes{0x23,T8,0x22,0x0a,0,0,0,0,0x78,0xff,0x88,0,0,0,0,0,0,T8}));
}

void wheelQueueScenarios() {
    opennow::InputQueue queue;
    opennow::InputEvent event;
    assert(queue.wheel(-99999,99999));
    assert(queue.take(event,false));
    assert(event.kind==opennow::InputEvent::Kind::wheel&&event.dx==-32768&&event.dy==32767);
    for(unsigned i=0;i<opennow::InputQueue::capacity;++i)assert(queue.wheel(0,120));
    assert(!queue.wheel(120,0));
    queue.cancel();
    assert(queue.takeCancel()&&!queue.takeCancel()&&!queue.take(event,true));
    queue.move(65535,-65535);
    queue.move(INT32_MAX,INT32_MIN);
    assert(queue.take(event,true)&&event.dx==65535&&event.dy==-65535);
}

PS5_PadData touchPad(unsigned fingers,std::uint8_t id0,std::uint16_t x0,std::uint16_t y0,std::uint8_t id1=0,std::uint16_t x1=0,std::uint16_t y1=0) {
    PS5_PadData pad{};
    pad.connected=true;pad.touch.fingers=static_cast<std::uint8_t>(fingers);
    pad.touch.touch[0]={x0,y0,id0,{}};pad.touch.touch[1]={x1,y1,id1,{}};
    return pad;
}

void touchScenarios() {
    opennow::TouchMouse mouse;
    auto m=mouse.update(touchPad(1,7,100,100),true);
    assert(!m.dx&&!m.dy&&!mouse.held);
    m=mouse.update(touchPad(1,7,130,90),true);
    assert(m.dx==30&&m.dy==-10);
    m=mouse.update(touchPad(1,7,1900,90),true);
    assert(m.dx==opennow::TouchMouse::maxStep&&m.dy==0);
    m=mouse.update(touchPad(1,8,50,50),true);
    assert(!m.dx&&!m.dy);
    m=mouse.update(touchPad(2,9,10,10,8,60,55),true);
    assert(!m.dx&&!m.dy);
    m=mouse.update(touchPad(2,9,10,10,8,70,65),true);
    assert(m.dx==0&&m.dy==0);
    m=mouse.update(touchPad(2,8,72,70,9,12,15),true);
    assert(m.dx==2&&m.dy==5);
    m=mouse.update(touchPad(1,9,12,12),true);
    assert(!m.dx&&!m.dy);
    m=mouse.update(touchPad(0,0,0,0),true);
    assert(!m.dx&&!m.dy&&mouse.finger==-1);
    m=mouse.update(touchPad(1,10,500,500),true);
    assert(!m.dx&&!m.dy);

    auto pad=touchPad(1,10,500,500);pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;
    mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    pad=touchPad(2,10,500,500,11,600,600);pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;
    mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    pad.buttons=0;mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::right);
    pad=touchPad(1,10,500,500);pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::right);
    pad.buttons=0;mouse.update(pad,true);
    assert(!mouse.held);

    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD|PS5_PAD_BUTTON_OPTIONS;mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=0;mouse.update(pad,true);

    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    PS5_PadData gone{};
    m=mouse.update(gone,true);
    assert(!mouse.held&&!m.dx&&!m.dy&&mouse.finger==-1);
    mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=0;mouse.update(pad,true);
    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    m=mouse.update(touchPad(1,10,900,900),false);
    assert(!mouse.held&&!m.dx&&!m.dy);
    m=mouse.update(touchPad(1,10,950,900),true);
    assert(!m.dx&&!m.dy);
}

void queueScenarios() {
    using opennow::InputEvent;
    opennow::InputQueue queue;
    InputEvent e;
    assert(!queue.take(e,true));
    queue.move(0,0);
    assert(queue.size()==0);
    queue.move(3,4);queue.move(-1,2);
    assert(queue.size()==1);
    queue.move(70000,-70000);
    assert(queue.take(e,true)&&e.kind==InputEvent::Kind::move&&e.dx==65535&&e.dy==-65535);
    assert(queue.button(1,true));
    queue.move(5,0);
    assert(queue.key({0x41,0x1e,0}));
    queue.move(1,1);
    assert(queue.size()==4);
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::button&&e.button==1&&e.down);
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::move&&e.dx==5);
    assert(!queue.take(e,false));
    assert(queue.take(e,true)&&e.kind==InputEvent::Kind::key&&e.key.vk==0x41);
    assert(queue.take(e,false)&&e.dx==1&&e.dy==1);
    unsigned accepted=0;
    while(queue.key({0x42,0x30,0}))++accepted;
    assert(accepted==opennow::InputQueue::keyLimit);
    unsigned buttons=0;
    while(queue.button(1,(buttons&1)==0))++buttons;
    assert(queue.size()==opennow::InputQueue::capacity&&buttons==opennow::InputQueue::capacity-opennow::InputQueue::keyLimit);
    queue.move(9,9);
    assert(queue.size()==opennow::InputQueue::capacity);
    queue.cancel();
    assert(queue.size()==0);
    assert(queue.key({0x43,0x2e,0})&&queue.button(1,false));
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::cancel);
    assert(!queue.take(e,false));
    assert(queue.take(e,true)&&e.kind==InputEvent::Kind::key&&e.key.vk==0x43);
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::button&&!e.down&&!queue.take(e,true));
    for(unsigned i=0;i<opennow::InputQueue::capacity;++i)assert(queue.button(1,(i&1)==0));
    queue.cancel();queue.cancel();
    assert(queue.key({0x44,0x20,0}));
    queue.clear();
    assert(queue.size()==0&&queue.take(e,false)&&e.kind==InputEvent::Kind::cancel&&!queue.take(e,true));
}

unsigned find(const char* label) {
    for(unsigned i=0;i<opennow::StreamKeyboard::count;++i)if(!std::strcmp(opennow::StreamKeyboard::keys[i].label,label))return i;
    assert(false);
    return 0;
}

void keyboardScenarios() {
    using K=opennow::StreamKeyboard;
    for(unsigned r=0;r<K::rows;++r) {
        unsigned width=0;
        for(unsigned i=K::rowStart[r];i<K::rowStart[r+1];++i)width+=K::keys[i].width;
        assert(width==K::units);
    }
    K k;
    k.show();
    assert(k.open&&!std::strcmp(k.label(k.selected),"q"));
    k.move(-1,0);assert(!std::strcmp(k.label(k.selected),"Tab"));
    k.move(-1,0);assert(!std::strcmp(k.label(k.selected),"Tab"));
    k.move(0,-1);assert(!std::strcmp(k.label(k.selected),"1"));
    k.move(0,-1);assert(!std::strcmp(k.label(k.selected),"1"));
    k.selected=find("Backspace");k.move(1,0);assert(k.selected==find("Backspace"));
    k.move(0,1);assert(!std::strcmp(k.label(k.selected),"\\"));
    k.selected=find("g");k.move(0,1);assert(!std::strcmp(k.label(k.selected),"v"));
    k.move(0,1);assert(!std::strcmp(k.label(k.selected),"Space"));
    k.move(0,1);assert(!std::strcmp(k.label(k.selected),"Space"));

    opennow::InputQueue queue;
    opennow::InputEvent e;
    assert(k.press(find("h"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x48&&e.key.scan==0x23&&e.key.modifiers==0);
    assert(k.press(find("Shift"),queue)&&k.shift&&queue.size()==0);
    assert(!std::strcmp(k.label(find("h")),"H")&&!std::strcmp(k.label(find("1")),"!"));
    assert(k.press(find("1"),queue)&&!k.shift);
    assert(queue.take(e,true)&&e.key.vk==0x31&&e.key.scan==0x02&&e.key.modifiers==opennow::modifierShift);
    assert(k.press(find("Caps"),queue)&&k.caps);
    assert(!std::strcmp(k.label(find("h")),"H")&&!std::strcmp(k.label(find("1")),"1"));
    assert(k.press(find("i"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x49&&e.key.modifiers==opennow::modifierShift);
    assert(k.press(find("Shift"),queue)&&k.press(find("i"),queue));
    assert(queue.take(e,true)&&e.key.modifiers==0);
    assert(k.press(find("Caps"),queue)&&!k.caps);
    assert(k.press(find("Ctrl"),queue)&&k.press(find("Alt"),queue)&&k.latched(find("Ctrl"))&&k.latched(find("Alt")));
    assert(k.press(find("a"),queue)&&!k.ctrl&&!k.alt);
    assert(queue.take(e,true)&&e.key.vk==0x41&&e.key.modifiers==(opennow::modifierCtrl|opennow::modifierAlt));
    assert(k.press(K::spaceKey,queue)&&k.press(find("Tab"),queue)&&k.press(find("Esc"),queue)&&k.press(find("Left"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x20&&e.key.scan==0x39);
    assert(queue.take(e,true)&&e.key.vk==0x09&&e.key.scan==0x0f);
    assert(queue.take(e,true)&&e.key.vk==0x1b&&e.key.scan==0x01);
    assert(queue.take(e,true)&&e.key.vk==0x25&&e.key.scan==0x14b);
    assert(k.press(K::backspaceKey,queue));
    assert(queue.take(e,true)&&e.key.vk==0x08&&e.key.scan==0x0e);
    assert(k.press(find("Enter"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x0d&&e.key.scan==0x1c);

    for(unsigned i=0;i<opennow::InputQueue::keyLimit;++i)assert(k.press(find("x"),queue));
    k.press(find("Shift"),queue);
    assert(!k.press(find("y"),queue)&&k.shift);
    k.hide();
    assert(!k.open&&!k.shift&&!k.ctrl&&!k.alt);
}
}

void launcherScenarios() {
    opennow::LauncherMouse mouse;
    PS5_PadData pad{};pad.connected=true;pad.leftStick={128,128};pad.rightStick={255,128};
    auto f=mouse.update(pad,1000000,true);
    assert(!f.dx&&!f.dy&&!f.left&&!f.right&&!f.wheelX&&!f.wheelY);
    mouse.active=true;
    f=mouse.update(pad,1000000,true);
    assert(f.dx==11&&f.dy==0);
    f=mouse.update(pad,1016000,true);
    assert(f.dx==11);
    f=mouse.update(pad,1032000,true);
    assert(f.dx==11||f.dx==12);
    pad.rightStick={128+15,128-15};
    f=mouse.update(pad,1048000,true);
    assert(f.dx==0&&f.dy==0);
    pad.rightStick={0,128};mouse.speed=2;
    f=mouse.update(pad,1148000,true);
    assert(f.dx<=-70&&f.dx>=-71);
    mouse.cycleSpeed();assert(mouse.speed==0);
    pad.rightStick={128,255};
    f=mouse.update(pad,1164000,true);
    assert(f.dy==4&&f.dx==0);
    pad.rightStick={128,128};
    pad.buttons=PS5_PAD_BUTTON_R2;pad.analogButtons={40,0};
    f=mouse.update(pad,1180000,true);
    assert(f.left&&f.right);
    pad.buttons=0;pad.analogButtons={0,33};
    f=mouse.update(pad,1196000,true);
    assert(f.left&&!f.right);
    pad.analogButtons={0,32};
    pad.buttons=PS5_PAD_BUTTON_UP|PS5_PAD_BUTTON_LEFT;
    f=mouse.update(pad,1212000,true);
    assert(!f.left&&f.wheelY==120&&f.wheelX==-120);
    f=mouse.update(pad,1228000,true);
    assert(f.wheelY==120);
    mouse.wheelSent(1228000);
    f=mouse.update(pad,1300000,true);
    assert(!f.wheelX&&!f.wheelY);
    pad.buttons=PS5_PAD_BUTTON_DOWN|PS5_PAD_BUTTON_RIGHT;
    f=mouse.update(pad,1378000,true);
    assert(f.wheelY==-120&&f.wheelX==120);
    pad.rightStick={255,255};
    f=mouse.update(pad,1394000,false);
    assert(!f.dx&&!f.dy&&!f.wheelY&&mouse.last==0&&mouse.x==0);
    PS5_PadData gone{};
    f=mouse.update(gone,1410000,true);
    assert(!f.left&&!f.dx);
}

int main() {
    launcherScenarios();
    wireFixtures();
    wheelQueueScenarios();
    touchScenarios();
    queueScenarios();
    keyboardScenarios();
}
