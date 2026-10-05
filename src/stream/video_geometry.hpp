// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace opennow::video {
struct VideoRect {
 unsigned x=0,y=0,width=0,height=0;
 bool operator==(const VideoRect&) const = default;
};
inline VideoRect fitVideoRect(unsigned sourceWidth,unsigned sourceHeight,unsigned targetWidth,unsigned targetHeight) noexcept {
 if(!sourceWidth||!sourceHeight||!targetWidth||!targetHeight)return {};
 unsigned width=targetWidth,height=targetHeight;
 if(std::uint64_t(sourceWidth)*targetHeight>std::uint64_t(sourceHeight)*targetWidth)
  height=static_cast<unsigned>(std::uint64_t(targetWidth)*sourceHeight/sourceWidth);
 else width=static_cast<unsigned>(std::uint64_t(targetHeight)*sourceWidth/sourceHeight);
 return {(targetWidth-width)/2,(targetHeight-height)/2,width,height};
}
inline constexpr std::uint32_t packedVideoBlack(bool hdr) noexcept {
 return hdr?0xc0000000U:0xff000000U;
}
}
