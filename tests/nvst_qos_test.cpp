#include "stream/nvst_qos.hpp"
#include <cassert>
#include <cstdint>
#include <string>
using namespace opennow::webrtc;
static unsigned word(const std::array<std::uint8_t,56>& p,unsigned at){return p[at]|unsigned(p[at+1])<<8|unsigned(p[at+2])<<16|unsigned(p[at+3])<<24;}
int main(){
 const auto fixture=qosPacket({6,2,244808,1818674},{0,0,244808,0},false);
 const std::string hex="070234000700000000000000060000000200000048bc030000000000000000000000e803e803a43132c01b00000000000000000048bc0300";
 assert(hex.size()==fixture.size()*2);
 for(unsigned i=0;i<fixture.size();++i)assert(fixture[i]==std::stoul(hex.substr(i*2,2),nullptr,16));
 for(unsigned mbps:{50u,75u,100u}){
  QosFeedback f;const unsigned interval=mbps*1000000u/8u/18u;
  for(unsigned i=1;i<=5;++i){
   f.received(interval);auto s=f.next(i*5000);auto p=f.packet(s,true);
   assert(s.sequence==i&&word(p,16)==i&&word(p,20)==i*interval);
   assert(word(p,52)==(i-1)*interval&&word(p,48)==interval*8);
   assert(word(p,40)==i*5000);f.queued(s);
  }
  auto idle=f.packet(f.next(25000),true);assert(word(idle,48)==0);
 }
 QosFeedback f;f.received(1000);auto unsent=f.next(1);assert(unsent.sequence==1);
 f.received(2000);auto retry=f.next(2);auto packet=f.packet(retry,true);
 assert(retry.sequence==1&&word(packet,52)==0&&word(packet,48)==24000);
 f.queued(retry);assert(f.next(2).sequence==2);
 auto warm=f.packet(f.next(2),false);assert(word(warm,48)==0&&warm[32]==0);
 auto wrap=qosPacket({0,0,100,0},{UINT32_MAX,0,UINT32_MAX-99,0},true);
 assert(word(wrap,48)==1600);
 auto saturated=qosPacket({1,1,UINT32_MAX,0},{},true);assert(word(saturated,48)==UINT32_MAX);
 QosFeedback fresh;assert(fresh.next(0).bytes==0&&fresh.queuedCount()==0);
}
