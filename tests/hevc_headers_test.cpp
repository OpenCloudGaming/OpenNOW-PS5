#include "stream/native/hevc_headers.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main(){
 auto* f=std::fopen("assets/hdr-check.hevc","rb");assert(f);
 std::vector<unsigned char> data(300000);auto n=std::fread(data.data(),1,data.size(),f);std::fclose(f);data.resize(n);
 opennow::video::HevcHeaders h;assert(opennow::video::updateHevcHeaders(data.data(),data.size(),h));
 std::printf("HEVC SPS %ux%u profile=%u level=%u depth=%u colour=%u/%u/%u range=%d\n",h.width,h.height,h.profile,h.level,h.bit_depth,h.primaries,h.transfer,h.matrix,h.full_range);
 assert(h.width==3840&&h.height==2160&&h.hdr10());
 assert(h.max_dpb_frames==3);
 for(unsigned n=0;n<10;++n){opennow::video::HevcHeaders truncated;assert(!opennow::video::updateHevcHeaders(data.data(),n,truncated));}
}
