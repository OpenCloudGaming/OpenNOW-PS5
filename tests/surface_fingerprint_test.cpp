// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/native/surface_fingerprint.hpp"
#include <cassert>
#include <vector>
int main(){
 using namespace opennow::video;
 const auto mode=*nativeMode(NativeCodec::hevc_main10,640,360,120);
 std::vector<std::uint16_t> pixels(std::size_t(mode.pitch_components)*mode.coded_height*3/2);
 auto surface=NativeSurface{pixels.data(),pixels.size()*2,640,mode.coded_height,mode.pitch_components,mode.pitch_components*2,mode.codec_type,1};
 for(unsigned r=0;r<360;++r)for(unsigned c=0;c<640;++c)pixels[r*640+c]=64+(r*3+c)%876;
 const auto uv=std::size_t(640)*mode.coded_height;
 for(unsigned r=0;r<180;++r)for(unsigned c=0;c<640;++c)pixels[uv+r*640+c]=unsigned(c&1?600:400);
 const auto baseline=fingerprintSurface(surface,mode);assert(baseline&&baseline->luma_span>8);
 // Coded-row padding is not part of the visible-image comparison.
 for(unsigned r=360;r<mode.coded_height;++r)for(unsigned c=0;c<640;++c)pixels[r*640+c]=999;
 for(unsigned r=180;r<mode.coded_height/2;++r)for(unsigned c=0;c<640;++c)pixels[uv+r*640+c]=999;
 assert(fingerprintSurface(surface,mode)->hash==baseline->hash);
 pixels[uv]=500;assert(fingerprintSurface(surface,mode)->hash!=baseline->hash);
 auto invalid=surface;--invalid.buffer_bytes;assert(!fingerprintSurface(invalid,mode));
 invalid=surface;invalid.pitch_bytes=0;assert(!fingerprintSurface(invalid,mode));
 invalid=surface;invalid.width=642;assert(!fingerprintSurface(invalid,mode));
 std::fill(pixels.begin(),pixels.end(),0);
 const auto black=fingerprintSurface(surface,mode);assert(black&&black->luma_span==0&&black->hash!=baseline->hash);
}
