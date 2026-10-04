// SPDX-License-Identifier: GPL-3.0-or-later
// Fault-injected native API tests validate ownership and cleanup, not decode
// performance or whether a real PS5 accepts these configurations.
#include "stream/native/hardware_decoder.hpp"
#include "stream/native/videodec2_api.hpp"
#include "stream/native/decoder_worker_policy.hpp"
#include <cassert>
#include <cstdlib>
#include <map>
#include <vector>
#include <deque>
#include <cstring>
using opennow::video::HardwareDecoder;
namespace {
std::map<std::int64_t,void*> allocations;
std::int64_t nextStart=0;
sceVideodec2DecoderConfig lastConfig{};
int failCreate=0,failMap=0,failDelete=0;
bool refuseWideQuery=false,refuseWideCreate=false;
bool allowDeepQuery=false,refuseDeepCreate=false;
bool failDeepDecode=false,failDeepFlush=false;
struct PendingInput {const void* address;std::vector<std::uint8_t> bytes;sceVideodec2Output output;};
std::deque<PendingInput> pendingInputs;
void checkInputs(){for(const auto& input:pendingInputs)assert(!std::memcmp(input.address,input.bytes.data(),input.bytes.size()));}
std::vector<std::uint64_t> queriedWorkers;
std::vector<unsigned> queriedDepths;
std::vector<unsigned> createdDepths;
unsigned flexible=0,queues=0,decoders=0;
bool foreign=false,interior=false,delayed=false,forceRetained=false;
unsigned outputWidth=0,outputHeight=0,outputPitch=0;
void* retained=nullptr;
void output(sceVideodec2Frame* frame,sceVideodec2Output* out) {
 out->valid=1;out->codec=lastConfig.codecType;out->width=lastConfig.maxWidth;
 const auto sampleBytes=lastConfig.profile==2?2u:1u;
 out->height=lastConfig.maxHeight;out->pitch=((lastConfig.maxWidth*sampleBytes+255u)&~255u)/sampleBytes;
 if(outputWidth)out->width=outputWidth;
 if(outputHeight)out->height=outputHeight;
 if(outputPitch)out->pitch=outputPitch;
 out->pitchBytes=out->pitch*(lastConfig.profile==2?2:1);out->pictureCount=1;
 out->bufferSize=frame->bufferSize;
 out->buffer=foreign?reinterpret_cast<void*>(0x1000):interior?static_cast<char*>(frame->buffer)+64:
             forceRetained?retained:frame->buffer;
 if(!retained)retained=frame->buffer;
}
void clean() { assert(allocations.empty()&&flexible==0&&queues==0&&decoders==0); }
}
extern "C" {
int32_t sceSysmoduleLoadModule(uint32_t id){assert(id==207);return 0;}
int64_t sceKernelGetDirectMemorySize(){return 8ll<<30;}
int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t bytes,size_t alignment,int type,int64_t* start){
 assert(alignment==0x4000&&type==12);void* ptr=nullptr;assert(!posix_memalign(&ptr,alignment,bytes));
 *start=++nextStart;allocations[*start]=ptr;return 0;
}
int sceKernelMapDirectMemory(void** ptr,size_t,int,int,int64_t start,size_t){
 if(failMap)return -100;
 *ptr=allocations.at(start);return 0;
}
int sceKernelReleaseDirectMemory(int64_t start,size_t){std::free(allocations.at(start));allocations.erase(start);return 0;}
int sceKernelMapNamedFlexibleMemory(void** ptr,size_t bytes,int,int,const char*){*ptr=std::malloc(bytes);++flexible;return 0;}
int sceKernelMunmap(void* ptr,size_t){
 for(const auto& entry:allocations)if(entry.second==ptr)return 0;
 std::free(ptr);--flexible;return 0;
}
int32_t sceVideodec2QueryComputeMemoryInfo(sceVideodec2ComputeMemory* mem){mem->cpuGpuSize=0x4000;return 0;}
int32_t sceVideodec2AllocateComputeQueue(const sceVideodec2ComputeConfig*,const sceVideodec2ComputeMemory*,void** queue){*queue=reinterpret_cast<void*>(1);++queues;return 0;}
int32_t sceVideodec2ReleaseComputeQueue(void*){--queues;return 0;}
int32_t sceVideodec2QueryDecoderMemoryInfo(const sceVideodec2DecoderConfig* config,sceVideodec2DecoderMemory* mem){
 queriedWorkers.push_back(config->cpuAffinity);
 queriedDepths.push_back(config->pipelineDepth);
 if(config->pipelineDepth>1&&!allowDeepQuery)return -105;
 if(refuseWideQuery&&config->cpuAffinity!=0x3f)return -103;
 lastConfig=*config;mem->cpuSize=0x4000;mem->gpuSize=0x4000;mem->cpuGpuSize=0x4000;
 mem->frameAlignment=0x4000;
 mem->maxFrameSize=std::size_t(config->maxWidth==1920?2048:config->maxWidth)*config->maxHeight*3/2*(config->profile==2?2:1);
 return 0;
}
int32_t sceVideodec2CreateDecoder(const sceVideodec2DecoderConfig* config,const sceVideodec2DecoderMemory*,void** ptr){
 createdDepths.push_back(config->pipelineDepth);
 if(refuseDeepCreate&&config->pipelineDepth>1)return -106;
 if(refuseWideCreate&&config->cpuAffinity!=0x3f)return -104;
 if(failCreate)return -101;
 *ptr=reinterpret_cast<void*>(2);++decoders;return 0;
}
int32_t sceVideodec2DeleteDecoder(void*){if(failDelete)return -102;pendingInputs.clear();--decoders;return 0;}
int32_t sceVideodec2Reset(void*){retained=nullptr;pendingInputs.clear();return 0;}
int32_t sceVideodec2Decode(void*,sceVideodec2Input* input,sceVideodec2Frame* frame,sceVideodec2Output* out){
 frame->accepted=1;
 if(lastConfig.pipelineDepth==1){if(!delayed)output(frame,out);return 0;}
 checkInputs();if(failDeepDecode)return -107;
 sceVideodec2Output snapshot{};output(frame,&snapshot);
 const auto* data=static_cast<const std::uint8_t*>(input->au);
 pendingInputs.push_back({input->au,{data,data+input->auSize},snapshot});
 if(pendingInputs.size()>=lastConfig.pipelineDepth){*out=pendingInputs.front().output;pendingInputs.pop_front();}
 return 0;
}
int32_t sceVideodec2Flush(void*,sceVideodec2Frame* frame,sceVideodec2Output* out){
 if(lastConfig.pipelineDepth==1){output(frame,out);return 0;}
 checkInputs();if(failDeepFlush)return -108;
 assert(!pendingInputs.empty());*out=pendingInputs.front().output;pendingInputs.pop_front();return 0;
}
}
int main() {
 using namespace opennow::video;
 static_assert(decoderWorkers(0x1fff)==0x3ff);
 static_assert(decoderWorkers(0x1ffff)==0x3ff);
 static_assert(decoderWorkers(0xff)==0x3f);
 static_assert(decoderWorkers(0x3ff)==0xff);
 static_assert(decoderWorkers(0x3f)==0x3f);
 static_assert(decoderWorkers(0)==0x3f);
 const auto mode=*nativeMode(NativeCodec::h264,1920,1080,60);
 const std::uint8_t au[]={0,0,0,1,0x65,1};
 HardwareDecoder decoder;HardwareDecoder::Picture picture;
 for(bool queryFailure:{true,false}){
  queriedWorkers.clear();refuseWideQuery=queryFailure;refuseWideCreate=!queryFailure;
  assert(decoder.open(mode));assert(decoder.workerMask()==0x3f);
  assert(queriedWorkers==std::vector<std::uint64_t>({0x3ff,0x3f}));
  assert(decoder.close());clean();refuseWideQuery=refuseWideCreate=false;
 }
 failMap=1;assert(!decoder.open(mode));failMap=0;assert(decoder.close());clean();
 failCreate=1;assert(!decoder.open(mode));assert(decoder.error()==-101);failCreate=0;clean();
 queriedDepths.clear();
 for(auto codec:{NativeCodec::hevc_main10,NativeCodec::h264}){
  const auto ninety=*nativeMode(codec,3840,2160,90);assert(decoder.open(ninety));
  if(codec==NativeCodec::hevc_main10)assert(queriedDepths==std::vector<unsigned>({2,3,1}));
  assert(lastConfig.pipelineDepth==1);
  assert(lastConfig.maxLevel==ninety.level&&lastConfig.maxHeight==2176);
  assert(!decoder.expectOutput(*nativeMode(codec,3840,2160,120)));
  assert(decoder.expectOutput(ninety));
  assert(decoder.decode(au,sizeof(au),0,picture)==HardwareDecoder::Result::picture);
  assert(picture.mode.fps==90&&decoder.timing().decode_calls==1&&decoder.timing().flush_calls==0);
  assert(decoder.release(picture));assert(decoder.close());clean();
 }
 assert(decoder.open(*nativeMode(NativeCodec::hevc_main10,3840,2160,120)));
 assert(lastConfig.codecType==0xee049&&lastConfig.profile==2&&lastConfig.maxLevel==156);
 assert(lastConfig.maxHeight==2176&&lastConfig.pipelineDepth==1);
 assert(lastConfig.cpuAffinity==0x3ff&&decoder.workerMask()==0x3ff);
 assert(lastConfig.maxDpbFrames==6);
 auto resized=*nativeMode(NativeCodec::hevc_main10_sdr,2880,1620,120);resized.full_range=true;
 auto invalid=resized;invalid.storage=SampleStorage::nv12;assert(!decoder.expectOutput(invalid));
 invalid=resized;invalid.profile=1;assert(!decoder.expectOutput(invalid));
 invalid=resized;invalid.visible_width=2881;assert(!decoder.expectOutput(invalid));
 assert(decoder.expectOutput(resized));outputWidth=2880;
 assert(decoder.decode(au,sizeof(au),0,picture)==HardwareDecoder::Result::picture);
 assert(picture.mode.full_range&&!picture.mode.hdr&&picture.mode.visible_width==2880);
 assert(picture.surface.pitch_components==3840&&picture.surface.height==2176);
 assert(decoder.release(picture));
 outputHeight=1632;outputPitch=2944;
 assert(decoder.decode(au,sizeof(au),1,picture)==HardwareDecoder::Result::picture);
 assert(picture.surface.pitch_bytes==5888&&decoder.release(picture));
 outputWidth=outputHeight=outputPitch=0;
 assert(decoder.close());clean();
 const auto hdr=*nativeMode(NativeCodec::hevc_main10,3840,2160,120);
 // Query support is not creation support; both failure stages clean up.
 for(unsigned refusal:{0u,1u,2u}){
  queriedDepths.clear();createdDepths.clear();
  allowDeepQuery=refusal!=0;refuseDeepCreate=refusal==1;
  assert(decoder.probePipelineCreation(hdr));clean();
  assert(queriedDepths==std::vector<unsigned>({2,3}));
  assert(createdDepths==(refusal==0?std::vector<unsigned>{}:std::vector<unsigned>{2,3}));
  // The probe never leaves a candidate open or submits input to it.
  assert(decoder.decode(au,sizeof(au),0,picture)==HardwareDecoder::Result::error);
  assert(decoder.open(hdr)&&lastConfig.pipelineDepth==1);
  const auto activeQueries=queriedDepths.size();
  assert(!decoder.probePipelineCreation(hdr)&&queriedDepths.size()==activeQueries);
  assert(decoder.close());clean();
 }
 // Failure to delete a candidate preserves mapped memory and its handle.
 allowDeepQuery=true;refuseDeepCreate=false;failDelete=1;
 assert(!decoder.probePipelineCreation(hdr)&&decoders==1&&!allocations.empty());
 assert(decoder.decode(au,sizeof(au),0,picture)==HardwareDecoder::Result::error);
 failDelete=0;assert(decoder.close());clean();allowDeepQuery=false;
 // Distinct compressed inputs survive delayed GPU/worker consumption.
 // Geometry, color mode and timestamps follow the returned submission.
 allowDeepQuery=true;assert(HardwareDecoder::setQualifiedMain10Depth(3));
 assert(!HardwareDecoder::setQualifiedMain10Depth(4));
 assert(decoder.open(hdr)&&decoder.timing().pipeline_depth==3);
 for(unsigned n=0;n<6;++n){
  if(n==1){assert(decoder.expectOutput(resized));outputWidth=2880;}
  const std::uint8_t unique[]={0,0,0,1,0x26,std::uint8_t(n)};
  const auto result=decoder.decode(unique,sizeof(unique),n,picture);
  if(n<2)assert(result==HardwareDecoder::Result::no_picture);
  else{
   assert(result==HardwareDecoder::Result::picture&&picture.pts==n-2);
   assert(picture.mode.hdr==(n==2));assert(picture.mode.full_range==(n!=2));
   assert(!decoder.reset());assert(decoder.release(picture));
  }
 }
 assert(decoder.timing().in_flight==2);
 assert(decoder.drain(picture)==HardwareDecoder::Result::picture&&picture.pts==4);
 assert(decoder.release(picture));const auto callCount=decoder.timing().decode_calls;
 assert(decoder.decode(au,sizeof(au),6,picture)==HardwareDecoder::Result::blocked);
 assert(decoder.timing().decode_calls==callCount);
 assert(decoder.drain(picture)==HardwareDecoder::Result::picture&&picture.pts==5);
 assert(decoder.release(picture)&&decoder.timing().in_flight==0);
 for(unsigned n=6;n<9;++n){
  const auto result=decoder.decode(au,sizeof(au),n,picture);
  if(n<8)assert(result==HardwareDecoder::Result::no_picture);
  else assert(result==HardwareDecoder::Result::picture&&picture.pts==6&&decoder.release(picture));
 }
 assert(decoder.reset()&&decoder.timing().in_flight==0&&pendingInputs.empty());
 assert(decoder.decode(au,sizeof(au),9,picture)==HardwareDecoder::Result::no_picture);
 failDeepFlush=true;assert(decoder.drain(picture)==HardwareDecoder::Result::error);failDeepFlush=false;
 assert(decoder.fallbackToClassic()&&decoder.timing().pipeline_depth==1&&pendingInputs.empty());
 assert(decoder.decode(au,sizeof(au),10,picture)==HardwareDecoder::Result::picture);
 assert(picture.mode.full_range&&!picture.mode.hdr&&decoder.release(picture));
 assert(decoder.close());clean();outputWidth=0;
 assert(HardwareDecoder::setQualifiedMain10Depth(3));refuseDeepCreate=true;
 assert(decoder.open(hdr)&&decoder.timing().pipeline_depth==1);
 refuseDeepCreate=false;assert(decoder.close());clean();
 assert(decoder.open(hdr));failDeepDecode=true;
 assert(decoder.decode(au,sizeof(au),11,picture)==HardwareDecoder::Result::error);failDeepDecode=false;
 assert(decoder.fallbackToClassic()&&decoder.timing().pipeline_depth==1);
 assert(decoder.close());clean();allowDeepQuery=false;
 assert(HardwareDecoder::setQualifiedMain10Depth(1));
 assert(decoder.open(mode));
 assert(lastConfig.maxDpbFrames==4);
 assert(decoder.decode(au,sizeof(au),0,picture)==HardwareDecoder::Result::picture);
 const auto stale=picture;
 // Even shutdown must wait until the presenter has completed its GPU fence.
 assert(!decoder.reset());assert(!decoder.close());assert(!allocations.empty());
 assert(decoder.release(picture));assert(!decoder.release(picture));
 assert(decoder.decode(au,sizeof(au),1,picture)==HardwareDecoder::Result::picture);
 assert(!decoder.release(stale));assert(decoder.release(picture));
 foreign=true;assert(decoder.decode(au,sizeof(au),2,picture)==HardwareDecoder::Result::error);
 foreign=false;assert(decoder.decode(au,sizeof(au),3,picture)==HardwareDecoder::Result::error);
 assert(decoder.reset());
 interior=true;assert(decoder.decode(au,sizeof(au),4,picture)==HardwareDecoder::Result::error);
 interior=false;assert(decoder.reset());
 delayed=true;const auto beforeFlush=decoder.timing().flush_calls;assert(decoder.decode(au,sizeof(au),5,picture)==HardwareDecoder::Result::picture);
 assert(decoder.timing().flush_calls==beforeFlush+1);
 delayed=false;
 // Reject a decoder returning a slot still sampled by the GPU.
 HardwareDecoder::Picture rejected;
 forceRetained=true;assert(decoder.decode(au,sizeof(au),6,rejected)==HardwareDecoder::Result::error);
 forceRetained=false;assert(decoder.release(picture));assert(decoder.reset());
 std::vector<HardwareDecoder::Picture> held(12);
 for(auto& p:held)assert(decoder.decode(au,sizeof(au),7,p)==HardwareDecoder::Result::picture);
 assert(decoder.decode(au,sizeof(au),8,picture)==HardwareDecoder::Result::blocked);
 for(const auto& p:held)assert(decoder.release(p));
 failDelete=1;assert(!decoder.close());assert(!allocations.empty());
 failDelete=0;assert(decoder.close());clean();
}
