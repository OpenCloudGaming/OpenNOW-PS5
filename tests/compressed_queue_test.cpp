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
}
