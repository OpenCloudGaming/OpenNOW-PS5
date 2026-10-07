#include "stream/compressed_queue.hpp"
#include "stream/video_recovery.hpp"
#include <cassert>
#include <array>
#include <cstring>
int main(){
 using opennow::video::CompressedQueue;
 CompressedQueue q;
 assert(!q.open(9,32,64));assert(!q.open(8,SIZE_MAX,1));
 assert(q.open(8,32,64));
 const std::uint8_t idr[]={0,0,1,38,1,0xbb},pred[]={0,0,1,2,1,0xaa};
 opennow::video::Recovery recovery;
 assert(recovery.accept(idr,sizeof(idr),true));assert(q.push(idr,sizeof(idr),100));
 // Four assembled AUs in a network burst must preserve their reference chain.
 for(unsigned i=0;i<4;++i){assert(recovery.accept(pred,sizeof(pred),true));assert(q.push(pred,sizeof(pred),101+i));}
 const auto epoch=recovery.epoch();const auto inFlight=q.take();
 assert(inFlight.received==100&&inFlight.size==sizeof(idr));
 for(unsigned n=0;n<64;++n)assert(inFlight.data[inFlight.size+n]==0);
 // While the decoder holds the first AU, ring wrap, refill, and a recovery
 // clear must never overwrite its bytes or free its allocation.
 for(unsigned i=0;i<4;++i)assert(q.push(pred,sizeof(pred),200+i));
 assert(q.full());assert(!q.push(pred,sizeof(pred),300));
 assert(std::memcmp(inFlight.data,idr,sizeof(idr))==0);
 recovery.invalidate();q.clear();assert(!recovery.current(epoch));
 assert(std::memcmp(inFlight.data,idr,sizeof(idr))==0);
 assert(!recovery.accept(pred,sizeof(pred),true));assert(recovery.accept(idr,sizeof(idr),true));
 assert(q.push(idr,sizeof(idr),400));
 auto recovered=q.take();assert(recovered.received==400&&std::memcmp(recovered.data,idr,sizeof(idr))==0);
 // Repeated ring/spare turnover preserves order and decoder buffer ownership.
 for(unsigned pass=0;pass<1000;++pass){
  for(unsigned i=0;i<8;++i){std::array<std::uint8_t,32> au{};au.fill(i+1);assert(q.push(au.data(),au.size(),pass*8+i));}
  for(unsigned i=0;i<8;++i){auto unit=q.take();assert(unit.received==pass*8+i);for(unsigned n=0;n<32;++n)assert(unit.data[n]==i+1);}
  assert(q.size()==0&&!q.take().data);
 }
 q.close();assert(!q.push(pred,sizeof(pred),0));
 assert(q.open(2,32,64));assert(!q.push(pred,33,0));
 assert(q.push(pred,sizeof(pred),0));assert(q.push(pred,sizeof(pred),1));assert(q.full());
 // Saturation can skip obsolete frames before a queued keyframe while
 // preserving its entire reference chain and the decoder's handoff buffer.
 const std::uint8_t h264Idr[]={0,0,0,1,0x65,0xbb},h264Pred[]={0,0,1,0x41,0xaa};
 for(bool hevc:{false,true}){
  const auto* key=hevc?idr:h264Idr;const auto keySize=hevc?sizeof(idr):sizeof(h264Idr);
  const auto* delta=hevc?pred:h264Pred;const auto deltaSize=hevc?sizeof(pred):sizeof(h264Pred);
  assert(q.open(8,32,64));
  // Force ring wrap and retain an AU currently owned by the decoder.
  for(unsigned i=0;i<7;++i)assert(q.push(delta,deltaSize,i));
  for(unsigned i=0;i<7;++i)q.take();
  assert(q.push(key,keySize,7,true));const auto decoding=q.take();
  recovery.reset();assert(recovery.accept(key,keySize,hevc));const auto active=recovery.epoch();
  for(unsigned i=0;i<8;++i){
   const bool isKey=i==2||i==5;
   assert(q.push(isKey?key:delta,isKey?keySize:deltaSize,10+i,isKey));
  }
  assert(q.full());assert(q.discardBeforeKeyframe(false)==5);
  assert(q.size()==3&&recovery.current(active));
  assert(std::memcmp(decoding.data,key,keySize)==0);
  assert(recovery.accept(delta,deltaSize,hevc));assert(q.push(delta,deltaSize,18));
  for(unsigned i=15;i<=18;++i){auto unit=q.take();assert(unit.received==i);assert(recovery.accept(unit.data,unit.size,hevc));}
  // A keyframe at the head cannot make space: never discard a dependent
  // delta and pretend the following frames are still safe to decode.
  assert(q.push(key,keySize,20,true));
  for(unsigned i=1;i<8;++i)assert(q.push(delta,deltaSize,20+i));
  assert(q.discardBeforeKeyframe(false)==0&&q.full());
  // An incoming keyframe replaces even this full queue immediately.
  assert(q.discardBeforeKeyframe(true)==8&&q.size()==0);
  assert(q.push(key,keySize,30,true));auto latest=q.take();assert(latest.received==30);
  // Slot reuse must erase old keyframe markers.
  for(unsigned i=0;i<8;++i)assert(q.push(delta,deltaSize,40+i));
  assert(q.discardBeforeKeyframe(false)==0&&q.full());
  recovery.invalidate();q.clear();assert(!recovery.accept(delta,deltaSize,hevc));
  assert(recovery.accept(key,keySize,hevc));
 }
 q.close();assert(q.discardBeforeKeyframe(false)==0);assert(q.discardBeforeKeyframe(true)==0);
}
