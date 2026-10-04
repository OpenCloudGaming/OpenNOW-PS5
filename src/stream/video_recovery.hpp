#pragma once
#include <cstddef>
#include <cstdint>
namespace opennow::video {
// Caller holds the media mutex. A missing reference invalidates in-flight
// output as well as queued pictures until an independently decodable IDR.
class Recovery {
public:
 void reset() {epoch_=0;waiting_=true;}
 void invalidate() {++epoch_;waiting_=true;}
 std::uint64_t epoch() const {return epoch_;}
 bool current(std::uint64_t epoch) const {return epoch==epoch_;}
 bool accept(const std::uint8_t* data,std::size_t size,bool hevc=false) {
  if(!data||!size)return false;
  if(waiting_&&!hasIdr(data,size,hevc))return false;
  waiting_=false;return true;
 }
 static bool hasIdr(const std::uint8_t* data,std::size_t size,bool hevc=false) {
  if(!data)return false;
  for(std::size_t i=0;i+3<size;++i){
   if(data[i]!=0||data[i+1]!=0)continue;
   std::size_t n=0;
   if(data[i+2]==1)n=i+3;
   else if(i+4<size&&data[i+2]==0&&data[i+3]==1)n=i+4;
   if(!n)continue;
   const unsigned type=hevc?(data[n]>>1)&63:data[n]&31;
   if(hevc?(n+1<size&&!(data[n]&0x80)&&(data[n+1]&7)&&type>=16&&type<=21):type==5)return true;
  }
  return false;
 }
private:
 std::uint64_t epoch_=0;bool waiting_=true;
};
}
