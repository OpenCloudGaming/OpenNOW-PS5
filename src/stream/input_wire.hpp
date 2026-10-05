// SPDX-License-Identifier: GPL-3.0-or-later
// Keyboard and mouse wire formats adapted from OpenNOW Android StreamInputEncoder.kt, MIT, Copyright (c) 2025 Zortos (assets/licenses/OpenNOW-Android.txt).
#pragma once
#include <cstdint>
#include <vector>

namespace opennow::wire {
using Bytes=std::vector<std::uint8_t>;
inline void le32(Bytes& b,std::uint32_t n){for(int i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
inline void be(Bytes& b,std::uint64_t n,unsigned bytes){while(bytes){--bytes;b.push_back(static_cast<std::uint8_t>(n>>(bytes*8)));}}
inline Bytes single(const Bytes& payload,int protocol,std::uint64_t now){
    if(protocol<=2)return payload;
    Bytes wire{0x23};be(wire,now,8);wire.push_back(0x22);
    wire.insert(wire.end(),payload.begin(),payload.end());
    return wire;
}
inline Bytes key(bool down,std::uint16_t vk,std::uint16_t scan,std::uint16_t modifiers,int protocol,std::uint64_t now){
    Bytes p;le32(p,down?3:4);be(p,vk,2);be(p,modifiers,2);be(p,scan,2);be(p,now,8);
    return single(p,protocol,now);
}
inline Bytes mouseButton(bool down,std::uint8_t button,int protocol,std::uint64_t now){
    Bytes p;le32(p,down?8:9);p.push_back(button);p.push_back(0);be(p,0,4);be(p,now,8);
    return single(p,protocol,now);
}
inline Bytes mouseMove(std::int16_t dx,std::int16_t dy,int protocol,std::uint64_t now){
    Bytes p;le32(p,7);be(p,static_cast<std::uint16_t>(dx),2);be(p,static_cast<std::uint16_t>(dy),2);be(p,0,2);be(p,0,4);be(p,now,8);
    if(protocol<=2)return p;
    Bytes wire{0x23};be(wire,now,8);wire.push_back(0x21);be(wire,p.size(),2);
    wire.insert(wire.end(),p.begin(),p.end());
    return wire;
}
}
