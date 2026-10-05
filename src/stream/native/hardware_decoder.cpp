// SPDX-License-Identifier: GPL-3.0-or-later
// Native call sequence grounded in pinned ProsperoLight and Kodi PS5.
#include "hardware_decoder.hpp"
#include "videodec2_api.hpp"
#include "decoder_worker_policy.hpp"
#include <cstring>
#include <limits>
#include <atomic>
#include <cstdio>

#ifdef OPENNOW_PS5
#include <sys/param.h>
#include <sys/cpuset.h>
extern "C" void opennow_media_note(const char*);
extern "C" unsigned long long sceKernelGetProcessTime();
#endif

extern "C" {
std::int64_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(std::int64_t,std::int64_t,std::size_t,std::size_t,int,std::int64_t*);
int sceKernelMapDirectMemory(void**,std::size_t,int,int,std::int64_t,std::size_t);
int sceKernelReleaseDirectMemory(std::int64_t,std::size_t);
int sceKernelMapNamedFlexibleMemory(void**,std::size_t,int,int,const char*);
int sceKernelMunmap(void*,std::size_t);
}
namespace opennow::video {
namespace {
constexpr int invalid=-1,busy=-2;
std::atomic_uint qualifiedMain10Depth[2]{{1},{1}};
std::atomic_uint* qualifiedDepthFor(const NativeMode& mode) noexcept {
 if((mode.codec!=NativeCodec::hevc_main10&&mode.codec!=NativeCodec::hevc_main10_sdr)||
    mode.visible_width!=3840||mode.visible_height!=2160||mode.level!=156)return nullptr;
 return &qualifiedMain10Depth[mode.codec==NativeCodec::hevc_main10_sdr?1:0];
}
std::uint64_t availableCpus() noexcept {
#ifdef OPENNOW_PS5
 cpuset_t set{};std::uint64_t mask=0;
 // The public PS5 cpuset interface requires an eight-byte mask. Read only
 // the current title thread; no process or global affinity is changed.
 if(cpuset_getaffinity(CPU_LEVEL_WHICH,CPU_WHICH_TID,-1,sizeof(mask),&set))return classicDecoderWorkers;
 std::memcpy(&mask,&set,sizeof(mask));return mask;
#else
 // Native API host mocks model the documented title CPU envelope.
 return titleCpuEnvelope;
#endif
}
std::uint64_t nowUs() noexcept {
#ifdef OPENNOW_PS5
 return sceKernelGetProcessTime();
#else
 return 0;
#endif
}
std::size_t aligned(std::uint64_t n) noexcept {
 if(n>std::numeric_limits<std::size_t>::max()-0x3fff)return 0;
 return (static_cast<std::size_t>(n)+0x3fff)&~std::size_t(0x3fff);
}
void probeMain10Pipeline(const sceVideodec2DecoderConfig& active) noexcept {
 if(active.codecType!=0xee049||active.profile!=2||active.maxWidth!=3840)return;
 static std::atomic_flag once=ATOMIC_FLAG_INIT;
 if(once.test_and_set())return;
 // Capability observation only: keep the active decoder at depth one.
 // Querying memory for a copied config neither creates a second decoder
 // nor enables asynchronous input reuse or unqualified presentation.
 for(auto depth:{2u,3u}){
  auto candidate=active;candidate.pipelineDepth=depth;
  sceVideodec2DecoderMemory memory{};memory.size=sizeof(memory);
  const int rc=sceVideodec2QueryDecoderMemoryInfo(&candidate,&memory);
#ifdef OPENNOW_PS5
  char note[240];std::snprintf(note,sizeof(note),"NATIVE pipeline query depth=%u rc=%x cpu=%llu gpu=%llu shared=%llu frame=%llu (not created)",depth,rc,static_cast<unsigned long long>(memory.cpuSize),static_cast<unsigned long long>(memory.gpuSize),static_cast<unsigned long long>(memory.cpuGpuSize),static_cast<unsigned long long>(memory.maxFrameSize));opennow_media_note(note);
#else
  (void)rc;
#endif
 }
}
}
HardwareDecoder::~HardwareDecoder() { (void)close(); }
int HardwareDecoder::loadModule() noexcept {
 static const int result=sceSysmoduleLoadModule(207);
 return result;
}
bool HardwareDecoder::allocate(std::size_t bytes,int protection,Memory& mem) noexcept {
 if(!bytes)return true;
 std::int64_t start=-1;
 int rc=sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),bytes,0x4000,12,&start);
 if(rc)return fail(rc);
 void* pointer=nullptr;
 rc=sceKernelMapDirectMemory(&pointer,bytes,protection,0,start,0x4000);
 if(rc||!pointer){sceKernelReleaseDirectMemory(start,bytes);return fail(rc?rc:invalid);}
 mem={pointer,bytes,start};return true;
}
void HardwareDecoder::free(Memory& mem) noexcept {
 if(mem.address)sceKernelMunmap(mem.address,mem.bytes);
 if(mem.start>=0)sceKernelReleaseDirectMemory(mem.start,mem.bytes);
 mem={};
}
bool HardwareDecoder::fail(int rc) noexcept { error_=rc?rc:invalid;return false; }
bool HardwareDecoder::leased() const noexcept {
 for(const auto& slot:pool_)if(slot.state==State::presenter)return true;
 return false;
}
void* HardwareDecoder::address(unsigned slot) const noexcept {
 return static_cast<std::uint8_t*>(pictures_.address)+slot*frameBytes_;
}
bool HardwareDecoder::open(const NativeMode& mode) noexcept {
 auto* qualified=qualifiedDepthFor(mode);
 const auto depth=qualified?qualified->load():1u;
 if(openWithDepth(mode,depth))return true;
 if(qualified)*qualified=1;
 if(depth==1||decoder_||queue_)return false;
#ifdef OPENNOW_PS5
 char note[140];std::snprintf(note,sizeof(note),"NATIVE pipeline open refused depth=%u rc=%x; retrying depth=1",depth,error_);opennow_media_note(note);
#endif
 return openWithDepth(mode,1);
}
bool HardwareDecoder::setQualifiedMain10Depth(const NativeMode& mode,unsigned depth) noexcept {
 auto* qualified=qualifiedDepthFor(mode);
 if(!qualified||depth<1||depth>3)return false;
 *qualified=depth;return true;
}
bool HardwareDecoder::openForQualification(const NativeMode& mode,unsigned depth) noexcept {
 if(depth<1||depth>3||(depth>1&&!qualifiedDepthFor(mode)))return fail(invalid);
 return openWithDepth(mode,depth);
}
bool HardwareDecoder::openWithDepth(const NativeMode& mode,unsigned depth) noexcept {
 const auto workers=decoderWorkers(availableCpus());
 if(openConfigured(mode,workers,depth))return true;
 if(workers==classicDecoderWorkers||!affinityRefused_)return false;
#ifdef OPENNOW_PS5
 char note[160];std::snprintf(note,sizeof(note),"NATIVE workers rejected mask=%llx rc=%x; retrying mask=3f",static_cast<unsigned long long>(workers),error_);opennow_media_note(note);
#endif
 return openConfigured(mode,classicDecoderWorkers,depth);
}
bool HardwareDecoder::probePipelineCreation(const NativeMode& mode) noexcept {
 // Never replace a live decoder or a surface still sampled by the GPU.
 if(decoder_||queue_||leased())return fail(busy);
 if(!qualifiedDepthFor(mode))return fail(invalid);
 const auto workers=decoderWorkers(availableCpus());
 for(unsigned depth:{2u,3u}){
  const bool opened=openConfigured(mode,workers,depth);
  const int result=error_;
  const bool closed=close();
#ifdef OPENNOW_PS5
  char note[200];std::snprintf(note,sizeof(note),"NATIVE pipeline open depth=%u mask=%llx opened=%d rc=%x closed=%d (no decode)",depth,static_cast<unsigned long long>(workers),opened,result,closed);opennow_media_note(note);
#endif
  (void)opened;(void)result;
  // A decoder/queue that cannot stop keeps all of its memory mapped.
  if(!closed){poisoned_=true;return false;}
 }
 return true;
}
bool HardwareDecoder::openConfigured(const NativeMode& mode,std::uint64_t workers,unsigned depth) noexcept {
 affinityRefused_=false;
 if(!close())return false;
 error_=0;poisoned_=false;errorNotes_=0;pictureNoted_=false;
 copyUs_=0;publishUs_=0;apiUs_=0;flushUs_=0;apiCalls_=0;flushCalls_=0;blockedAttempts_=0;
 const auto known=nativeMode(mode.codec,mode.visible_width,mode.visible_height,mode.fps);
 if(depth<1||depth>3||!known||known->codec_type!=mode.codec_type||known->profile!=mode.profile||known->level!=mode.level||
    known->coded_height!=mode.coded_height||known->pitch_components!=mode.pitch_components||
    known->storage!=mode.storage||known->hdr!=mode.hdr)return fail(invalid);
 mode_=outputMode_=mode;
 int rc=loadModule();if(rc)return fail(rc);
 sceVideodec2ComputeMemory compute{};compute.size=sizeof(compute);
 rc=sceVideodec2QueryComputeMemoryInfo(&compute);
 if(rc)return fail(rc);
 if(!aligned(compute.cpuGpuSize)||!allocate(aligned(compute.cpuGpuSize),0x33,compute_))return fail(error_);
 compute.cpuGpu=compute_.address;compute.cpuGpuSize=compute_.bytes;
 sceVideodec2ComputeConfig queueConfig{};queueConfig.size=sizeof(queueConfig);
 rc=sceVideodec2AllocateComputeQueue(&queueConfig,&compute,&queue_);
 if(rc||!queue_){queue_=nullptr;const int saved=rc?rc:invalid;(void)close();return fail(saved);}
 sceVideodec2DecoderConfig config{};config.size=sizeof(config);
 config.resourceType=1;config.codecType=mode.codec_type;config.profile=mode.profile;
 config.maxLevel=mode.level;config.maxWidth=mode.visible_width;config.maxHeight=mode.coded_height;
 config.maxDpbFrames=nativeDpbFrames(mode);config.pipelineDepth=depth;config.computeQueue=reinterpret_cast<std::uint64_t>(queue_);
 config.cpuAffinity=workers;config.cpuPriority=700;config.optimizeProgressive=1;
 probeMain10Pipeline(config);
 sceVideodec2DecoderMemory memory{};memory.size=sizeof(memory);
 rc=sceVideodec2QueryDecoderMemoryInfo(&config,&memory);
 if(rc){(void)close();affinityRefused_=true;return fail(rc);}
 cpuBytes_=aligned(memory.cpuSize);frameBytes_=aligned(memory.maxFrameSize);
 const auto gpuBytes=aligned(memory.gpuSize),sharedBytes=aligned(memory.cpuGpuSize);
 const auto minimum=std::size_t(mode.pitch_components)*mode.coded_height*3/2*(mode.storage==SampleStorage::low_aligned_10bit?2:1);
 if((memory.cpuSize&&!cpuBytes_)||(memory.gpuSize&&!gpuBytes)||(memory.cpuGpuSize&&!sharedBytes)||
    !frameBytes_||frameBytes_<minimum||frameBytes_>std::numeric_limits<std::size_t>::max()/slots||
    memory.frameAlignment>0x4000){(void)close();return fail(invalid);}
 if(cpuBytes_){
  rc=sceKernelMapNamedFlexibleMemory(&cpu_,cpuBytes_,3,0,"OpenNowVdecCpu");
  if(rc||!cpu_){cpu_=nullptr;(void)close();return fail(rc?rc:invalid);}
 }
 memory.cpu=cpu_;memory.cpuSize=cpuBytes_;
 if(!allocate(gpuBytes,0x32,gpu_)||!allocate(sharedBytes,0x33,shared_)||
    !allocate(inputCapacity*(depth==1?1:depth+1),0x32,input_)||!allocate(frameBytes_*slots,0x32,pictures_)){
  const int saved=error_;(void)close();return fail(saved);
 }
 memory.gpu=gpu_.address;memory.gpuSize=gpu_.bytes;memory.cpuGpu=shared_.address;memory.cpuGpuSize=shared_.bytes;
 rc=sceVideodec2CreateDecoder(&config,&memory,&decoder_);
 if(rc||!decoder_){decoder_=nullptr;(void)close();affinityRefused_=true;return fail(rc?rc:invalid);}
 if(!reset()){const int saved=error_;(void)close();return fail(saved);}
 workerMask_=workers;pipelineDepth_=depth;
#ifdef OPENNOW_PS5
 char note[180];std::snprintf(note,sizeof(note),"NATIVE workers mask=%llx logical=%u pipeline=%u codec=%x",static_cast<unsigned long long>(workers),static_cast<unsigned>(std::popcount(workers)),depth,mode.codec_type);opennow_media_note(note);
#endif
 return true;
}
HardwareDecoder::Result HardwareDecoder::decode(const std::uint8_t* data,std::size_t bytes,
                                               std::uint64_t pts,Picture& picture) noexcept {
 picture={};
 if(!decoder_||poisoned_||!data||!bytes||bytes>inputCapacity){fail(invalid);return Result::error;}
 if(draining_)return Result::blocked;
 const unsigned offered=freePicture();
 if(offered==slots){++blockedAttempts_;return Result::blocked;}
 unsigned inputSlot=inputSlots;
 const auto depth=pipelineDepth_.load();
 for(unsigned i=0;i<(depth==1?1:depth+1);++i)if(!inputUsed_[i]){inputSlot=i;break;}
 if(inputSlot==inputSlots||submissionCount_==inputSlots){++blockedAttempts_;return Result::blocked;}
 auto* at=static_cast<std::uint8_t*>(input_.address)+inputSlot*inputCapacity;
 inputUsed_[inputSlot]=true;
 submissions_[(submissionHead_+submissionCount_)%inputSlots]={inputSlot,pts,outputMode_};
 ++submissionCount_;inFlight_=submissionCount_;
 auto stage=nowUs();
 std::memcpy(at,data,bytes);
 copyUs_+=nowUs()-stage;stage=nowUs();
 // Publish the copied access unit to the media block.
#if defined(__x86_64__)
 for(std::size_t n=0;n<bytes;n+=64)__builtin_ia32_clflush(at+n);
 __builtin_ia32_mfence();
#else
 // Host tests on Apple Silicon; the PS5 target always takes the x86 path.
 (void)at;std::atomic_thread_fence(std::memory_order_release);
#endif
 publishUs_+=nowUs()-stage;
 sceVideodec2Input input{};input.size=sizeof(input);input.au=at;input.auSize=bytes;
 input.pts=pts;input.dts=UINT64_MAX;
 sceVideodec2Frame frame{};frame.size=sizeof(frame);frame.buffer=address(offered);frame.bufferSize=frameBytes_;
 sceVideodec2Output output{};output.size=sizeof(output);
 stage=nowUs();int rc=sceVideodec2Decode(decoder_,&input,&frame,&output);
 apiUs_+=nowUs()-stage;++apiCalls_;
 const bool initiallyAccepted=frame.accepted;
 bool flushed=false;
 if(!rc&&!output.error&&!output.valid&&depth==1){
  flushed=true;
  output={};output.size=sizeof(output);
  stage=nowUs();rc=sceVideodec2Flush(decoder_,&frame,&output);
  flushUs_+=nowUs()-stage;++flushCalls_;
 }
#ifdef OPENNOW_PS5
 if(((rc||output.error)&&errorNotes_++<4)||(output.valid&&!pictureNoted_)){
  char note[300];std::snprintf(note,sizeof(note),"NATIVE decode rc=%x flush=%d accepted=%u valid=%u error=%u count=%u codec=%x %ux%u pitch=%u bytes=%u format=%u dpb=%u",rc,flushed,frame.accepted,output.valid,output.error,output.pictureCount,output.codec,output.width,output.height,output.pitch,output.pitchBytes,output.frameFormat,nativeDpbFrames(mode_));opennow_media_note(note);
  if(output.valid)pictureNoted_=true;
 }
#else
 (void)flushed;
#endif
 if(rc||output.error){
  std::lock_guard<std::mutex> lock(mutex_);
  if(initiallyAccepted||frame.accepted)pool_[offered].state=State::decoder;
  poisoned_=true;fail(rc?rc:invalid);return Result::error;
 }
 const auto result=settle(offered,initiallyAccepted||frame.accepted,output,picture);
 if(result==Result::no_picture&&(depth==1||submissionCount_>depth)){
  poisoned_=true;fail(invalid);return Result::error;
 }
 return result;
}
unsigned HardwareDecoder::freePicture() noexcept {
 std::lock_guard<std::mutex> lock(mutex_);
 for(unsigned i=0;i<slots;++i)if(pool_[i].state==State::free)return i;
 return slots;
}
HardwareDecoder::Result HardwareDecoder::settle(unsigned offered,bool accepted,
                                               const sceVideodec2Output& output,Picture& picture) noexcept {
 std::lock_guard<std::mutex> lock(mutex_);
 if(accepted)pool_[offered].state=State::decoder;
 if(!output.valid)return Result::no_picture;
 if(!submissionCount_){poisoned_=true;fail(invalid);return Result::error;}
 const auto submission=submissions_[submissionHead_];
 unsigned returned=slots;
 for(unsigned i=0;i<slots;++i)if(output.buffer==address(i)){returned=i;break;}
 NativeSurface surface{output.buffer,output.bufferSize,output.width,output.height,output.pitch,
                       output.pitchBytes,output.codec,output.pictureCount};
 if(returned==slots||pool_[returned].state==State::presenter||
    (returned!=offered&&pool_[returned].state!=State::decoder)||
    !validNativeSurfaceInEnvelope(submission.mode,mode_,surface,address(returned),frameBytes_)){
  poisoned_=true;fail(invalid);return Result::error;
 }
 unsigned retained=0;
 for(unsigned i=0;i<slots;++i)if(i!=returned&&pool_[i].state==State::decoder)++retained;
 if(retained>=submissionCount_){poisoned_=true;fail(invalid);return Result::error;}
 pool_[returned]={State::presenter,++leaseSerial_};
 picture={surface,returned,leaseSerial_,submission.mode,submission.pts};
 inputUsed_[submission.input]=false;submissionHead_=(submissionHead_+1)%inputSlots;
 --submissionCount_;inFlight_=submissionCount_;
 return Result::picture;
}
HardwareDecoder::Result HardwareDecoder::drain(Picture& picture) noexcept {
 picture={};
 if(!decoder_||poisoned_){fail(invalid);return Result::error;}
 if(!submissionCount_){draining_=false;return Result::no_picture;}
 const auto offered=freePicture();
 if(offered==slots){++blockedAttempts_;return Result::blocked;}
 draining_=true;
 sceVideodec2Frame frame{};frame.size=sizeof(frame);frame.buffer=address(offered);frame.bufferSize=frameBytes_;
 sceVideodec2Output output{};output.size=sizeof(output);
 const auto stage=nowUs();const int rc=sceVideodec2Flush(decoder_,&frame,&output);
 flushUs_+=nowUs()-stage;++flushCalls_;
 if(rc||output.error||!output.valid){poisoned_=true;fail(rc?rc:invalid);return Result::error;}
 const auto result=settle(offered,frame.accepted,output,picture);
 if(!submissionCount_)draining_=false;
 return result;
}
bool HardwareDecoder::fallbackToClassic() noexcept {
 const auto mode=mode_,output=outputMode_;
 const int cause=error_;
 if(auto* qualified=qualifiedDepthFor(mode))*qualified=1;
 if(!openWithDepth(mode,1))return false;
#ifdef OPENNOW_PS5
 char note[160];std::snprintf(note,sizeof(note),"NATIVE pipeline fallback rc=%x; active depth=1",cause);opennow_media_note(note);
#else
 (void)cause;
#endif
 return expectOutput(output);
}
bool HardwareDecoder::expectOutput(const NativeMode& output) noexcept {
 std::lock_guard<std::mutex> lock(mutex_);
 const auto known=nativeMode(output.codec,output.visible_width,output.visible_height,output.fps);
 if(!decoder_||!known||output.codec_type!=mode_.codec_type||output.profile!=mode_.profile||output.storage!=mode_.storage||
    output.visible_width>mode_.visible_width||output.coded_height>mode_.coded_height||output.level>mode_.level||output.fps>mode_.fps||
    known->codec_type!=output.codec_type||known->profile!=output.profile||known->storage!=output.storage||
    known->level!=output.level||known->pitch_components!=output.pitch_components||known->coded_height!=output.coded_height||known->hdr!=output.hdr)return fail(invalid);
 outputMode_=output;return true;
}
bool HardwareDecoder::release(const Picture& picture) noexcept {
 std::lock_guard<std::mutex> lock(mutex_);
 if(!pictures_.address||picture.slot>=slots||!picture.lease||picture.surface.buffer!=address(picture.slot))return false;
 auto& slot=pool_[picture.slot];
 if(slot.state!=State::presenter||slot.lease!=picture.lease)return false;
 slot={};return true;
}
bool HardwareDecoder::reset() noexcept {
 std::lock_guard<std::mutex> lock(mutex_);
 if(leased())return fail(busy);
 if(!decoder_)return fail(invalid);
 int rc=sceVideodec2Reset(decoder_);if(rc)return fail(rc);
 pool_={};inputUsed_={};submissionHead_=submissionCount_=0;inFlight_=0;draining_=false;poisoned_=false;return true;
}
bool HardwareDecoder::close() noexcept {
 std::lock_guard<std::mutex> lock(mutex_);
 if(leased())return fail(busy);
 // Never unmap decoder/compute memory if the corresponding engine cannot stop.
 if(decoder_){int rc=sceVideodec2DeleteDecoder(decoder_);if(rc)return fail(rc);decoder_=nullptr;}
 if(queue_){int rc=sceVideodec2ReleaseComputeQueue(queue_);if(rc)return fail(rc);queue_=nullptr;}
 free(pictures_);free(input_);free(shared_);free(gpu_);free(compute_);
 if(cpu_)sceKernelMunmap(cpu_,cpuBytes_);
 cpu_=nullptr;cpuBytes_=frameBytes_=0;pool_={};inputUsed_={};submissionHead_=submissionCount_=0;inFlight_=0;draining_=false;poisoned_=false;workerMask_=0;pipelineDepth_=0;return true;
}
}
