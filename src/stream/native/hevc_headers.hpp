// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded HEVC SPS/VUI inspection. Decode remains on Videodec2.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace opennow::video {
struct HevcHeaders {
 unsigned width=0,height=0,bit_depth=0,profile=0,level=0,max_dpb_frames=0;
 unsigned primaries=0,transfer=0,matrix=0;
 bool full_range=false,valid=false;
 bool hdr10() const {return valid&&profile==2&&bit_depth==10&&primaries==9&&transfer==16&&matrix==9&&!full_range;}
};
class HevcBits {
public:
 explicit HevcBits(const std::vector<std::uint8_t>& data):data_(data){}
 std::uint32_t bits(unsigned count) {
  if(count>32||offset_+count>data_.size()*8){ok=false;return 0;}
  std::uint32_t value=0;
  while(count--){value=(value<<1)|((data_[offset_/8]>>(7-offset_%8))&1);++offset_;}
  return value;
 }
 void skip(unsigned count){while(count>32&&ok){bits(32);count-=32;}bits(count);}
 unsigned ue(unsigned limit=65535) {
  unsigned zeros=0;
  while(ok&&!bits(1)){if(++zeros>16){ok=false;return 0;}}
  if(!ok)return 0;
  unsigned value=((1u<<zeros)-1)+bits(zeros);
  if(value>limit)ok=false;
  return value;
 }
 void se(){(void)ue();}
 bool ok=true;
private:
 const std::vector<std::uint8_t>& data_;std::size_t offset_=0;
};
inline HevcHeaders inspectHevcSps(const std::uint8_t* nal,std::size_t size) {
 HevcHeaders h;
 if(!nal||size<5||size>2048||((nal[0]>>1)&63)!=33||(nal[0]&0x81)||(nal[1]>>3)||!(nal[1]&7))return h;
 std::vector<std::uint8_t> rbsp;rbsp.reserve(size);
 unsigned zeros=0;
 for(std::size_t i=2;i<size;++i){
  if(zeros>=2&&nal[i]==3){if(i+1>=size||nal[i+1]>3)return h;zeros=0;continue;}
  rbsp.push_back(nal[i]);zeros=nal[i]==0?zeros+1:0;
 }
 HevcBits b(rbsp);b.skip(4);const unsigned sub=b.bits(3);b.skip(1);
 if(sub>6)return h;
 const unsigned profile=b.bits(8);h.profile=profile&31;b.skip(80);h.level=b.bits(8);
 bool hasProfile[7]{},hasLevel[7]{};
 for(unsigned i=0;i<sub;++i){hasProfile[i]=b.bits(1);hasLevel[i]=b.bits(1);}
 if(sub)b.skip(2*(8-sub));
 for(unsigned i=0;i<sub;++i){if(hasProfile[i])b.skip(88);if(hasLevel[i])b.skip(8);}
 b.ue(15);const unsigned chroma=b.ue(3);if(chroma!=1)return h;
 h.width=b.ue(8192);h.height=b.ue(8192);
 unsigned left=0,right=0,top=0,bottom=0;
 if(b.bits(1)){left=b.ue(8192);right=b.ue(8192);top=b.ue(8192);bottom=b.ue(8192);}
 const unsigned luma=b.ue(8),chromaDepth=b.ue(8);h.bit_depth=luma+8;
 if(luma!=chromaDepth||(luma!=0&&luma!=2))return {};
 const unsigned poc=b.ue(12)+4;
 const unsigned all=b.bits(1);
 for(unsigned i=all?0:sub;i<=sub&&b.ok;++i){h.max_dpb_frames=std::max(h.max_dpb_frames,b.ue(15)+1);b.ue(15);b.ue();}
 b.ue(3);b.ue(3);b.ue(3);b.ue(3);b.ue(16);b.ue(16);
 if(b.bits(1)&&b.bits(1)){
  for(unsigned id=0;id<4&&b.ok;++id)for(unsigned matrix=0;matrix<6&&b.ok;matrix+=id==3?3:1){
   if(!b.bits(1))b.ue(6);
   else{if(id>1)b.se();for(unsigned i=0;i<std::min(64u,1u<<(4+2*id))&&b.ok;++i)b.se();}
  }
 }
 b.skip(2);
 if(b.bits(1)){b.skip(8);b.ue(3);b.ue(3);b.skip(1);}
 const unsigned sets=b.ue(64);unsigned delta[64]{};
 for(unsigned i=0;i<sets&&b.ok;++i){
  if(i&&b.bits(1)){
   b.skip(1);b.ue();
   for(unsigned j=0;j<=delta[i-1]&&b.ok;++j){const bool used=b.bits(1);const bool use=used||b.bits(1);if(use)++delta[i];}
   if(delta[i]>32)return {};
  }else{
   const unsigned negative=b.ue(16),positive=b.ue(16);delta[i]=negative+positive;
   for(unsigned j=0;j<delta[i]&&b.ok;++j){b.ue();b.skip(1);}
  }
 }
 if(b.bits(1)){const unsigned count=b.ue(32);for(unsigned i=0;i<count&&b.ok;++i)b.skip(poc+1);}
 b.skip(2);
 if(b.bits(1)){
  if(b.bits(1)){const unsigned aspect=b.bits(8);if(aspect==255)b.skip(32);}
  if(b.bits(1))b.skip(1);
  if(b.bits(1)){
   b.skip(3);h.full_range=b.bits(1);
   if(b.bits(1)){h.primaries=b.bits(8);h.transfer=b.bits(8);h.matrix=b.bits(8);}
  }
 }
 if(!b.ok||!h.width||!h.height||2*(left+right)>=h.width||2*(top+bottom)>=h.height)return {};
 h.width-=2*(left+right);h.height-=2*(top+bottom);h.valid=true;return h;
}
inline void hevcSpsView(const std::uint8_t* data,std::size_t size,const std::uint8_t*& nal,std::size_t& bytes) {
 nal=nullptr;bytes=0;if(!data)return;
 for(std::size_t i=0;i+4<size;++i){
  if(data[i]||data[i+1])continue;
  const auto start=data[i+2]==1?i+3:(data[i+2]==0&&data[i+3]==1?i+4:0);
  if(!start||start+2>size||((data[start]>>1)&63)!=33)continue;
  auto end=start+2;while(end+3<size&&!(data[end]==0&&data[end+1]==0&&(data[end+2]==1||(data[end+2]==0&&data[end+3]==1))))++end;
  if(end+3>=size)end=size;nal=data+start;bytes=end-start;return;
 }
}
inline bool updateHevcHeaders(const std::uint8_t* data,std::size_t size,HevcHeaders& headers) {
 if(!data)return false;
 for(std::size_t i=0;i+4<size;++i){
  if(data[i]||data[i+1])continue;
  std::size_t start=0;
  if(data[i+2]==1)start=i+3;
  else if(data[i+2]==0&&data[i+3]==1)start=i+4;
  if(!start||start+2>size||((data[start]>>1)&63)!=33)continue;
  std::size_t end=start+2;
  while(end+3<size&&!(data[end]==0&&data[end+1]==0&&(data[end+2]==1||(data[end+2]==0&&data[end+3]==1))))++end;
  if(end+3>=size)end=size;
  headers=inspectHevcSps(data+start,end-start);return headers.valid;
 }
 return headers.valid;
}
}
