#include "stream/video_recovery.hpp"
#include <cassert>
#include <cstdint>
int main(){
 using opennow::video::Recovery;
 const std::uint8_t predictive[]={0,0,1,0x41,0xaa};
 const std::uint8_t idr[]={0,0,0,1,0x65,0xaa};
 const std::uint8_t parameters[]={0,0,1,0x67,0xaa,0,0,1,0x68,0xbb};
 Recovery recovery;
 assert(!recovery.accept(predictive,sizeof(predictive)));
 assert(!recovery.accept(parameters,sizeof(parameters)));
 assert(recovery.accept(idr,sizeof(idr)));
 const auto inFlight=recovery.epoch();
 assert(recovery.accept(predictive,sizeof(predictive)));
 // Losing an assembled AU or overflowing the queue invalidates references.
 recovery.invalidate();
 assert(!recovery.current(inFlight));
 assert(!recovery.accept(predictive,sizeof(predictive)));
 assert(!recovery.accept(parameters,sizeof(parameters)));
 assert(recovery.accept(idr,sizeof(idr)));
 const auto recovered=recovery.epoch();
 assert(recovery.current(recovered)&&recovered!=inFlight);
 assert(recovery.accept(predictive,sizeof(predictive)));
 for(std::size_t length=0;length<5;++length)assert(!Recovery::hasIdr(idr,length));
 const std::uint8_t shortIdr[]={0,0,1,0x65};
 assert(Recovery::hasIdr(shortIdr,sizeof(shortIdr)));
 assert(!Recovery::hasIdr(nullptr,100));
 recovery.invalidate();recovery.invalidate();
 assert(!recovery.current(recovered));
 assert(!recovery.accept(predictive,sizeof(predictive)));
 assert(recovery.accept(idr,sizeof(idr)));
 recovery.reset();assert(!recovery.accept(predictive,sizeof(predictive)));
 const std::uint8_t hevcPredictive[]={0,0,1,2,1,0xaa},hevcIdr[]={0,0,1,38,1,0xbb},hevcCra[]={0,0,1,42,1,0xaa};
 recovery.reset();assert(!recovery.accept(hevcPredictive,sizeof(hevcPredictive),true));
 assert(recovery.accept(hevcIdr,sizeof(hevcIdr),true));assert(recovery.accept(hevcPredictive,sizeof(hevcPredictive),true));
 recovery.invalidate();assert(recovery.accept(hevcCra,sizeof(hevcCra),true));
}

