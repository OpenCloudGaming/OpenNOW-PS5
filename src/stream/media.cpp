// SPDX-License-Identifier: GPL-3.0-or-later
#include "media.hpp"
#include "AudioRtpUtils.hpp"
#include "native/gpu_presenter.hpp"
#include "native/hevc_headers.hpp"
#include "video_geometry.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <algorithm>
#include <cerrno>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
// FFmpeg 7.0 FFCodec starts with its public AVCodec, pinned by PacBrew.
extern const AVCodec ff_h264_decoder;
int scePthreadCreate(void**,const void*,void*(*)(void*),void*,const char*);
int scePthreadJoin(void*,void**);
int sceKernelUsleep(unsigned);
unsigned long long sceKernelGetProcessTime();
}
extern "C" void opennow_media_note(const char*);
extern "C" void opennow_hevc_sps_note(const std::uint8_t*,std::size_t);
namespace opennow {
namespace {
void decoderLog(void*,int level,const char* format,va_list args){
 if(level>AV_LOG_ERROR)return;
 char line[384];std::vsnprintf(line,sizeof(line),format,args);opennow_media_note(line);
}
}

unsigned Media::availableAudioChannels() noexcept {return audio::Output::probeCapacity();}
void Media::updateAudioStats() noexcept {
 const auto stats=audioReceiver_.stats();
 audioPackets=stats.packets;audioErrors=stats.errors;audioRecovered=stats.recovered;audioConcealed=stats.concealed;
 audioFecAttempts=stats.fecAttempts;audioChannels=stats.channels;audioBytes=stats.bytes;audioSamples=stats.samples;
 audioQueueFrames=stats.queueFrames;audioQueuePeak=stats.queuePeak;audioDroppedFrames=stats.droppedFrames;audioUnderruns=stats.underruns;audioStereoFallbacks=stats.stereoFallbacks;
}
bool Media::configureAudio(const audio::Format& format) noexcept {
 if(!running_||!audioReceiver_.configure(format))return false;
 updateAudioStats();
 char note[192];std::snprintf(note,sizeof(note),"AUDIO negotiated=%uch output=%uch streams=%u coupled=%u PT=%d RED=%d ceiling=%u",
     format.channels,audioOutput_.channels(),format.streams,format.coupled,format.payload,format.redPayload,format.bitrate());opennow_media_note(note);
 return true;
}
bool Media::start(const StreamSettings& settings,unsigned requestedAudioChannels) noexcept {
 if(validateSettings(settings)!=SettingsError::none){stop();return false;}
 stop();if(audioOutput_.active())return false;
 if((requestedAudioChannels!=2&&requestedAudioChannels!=6&&requestedAudioChannels!=8)||audio::requestedChannels(settings.audio_mode,requestedAudioChannels)!=requestedAudioChannels)return false;
 settings_=settings;presented=0;actualHdr=false;frames=0;dropped=0;videoUnits=0;audioPackets=0;audioErrors=0;audioRecovered=0;audioConcealed=0;audioUnderruns=0;decodeError=0;fresh_=false;recovery_.reset();queueDrops=0;corruptFrames=0;recoveryResets=0;idrFrames=0;decodedWidth=0;decodedHeight=0;videoBytes=0;
 audioChannels=0;audioBytes=0;audioSamples=0;audioQueueFrames=0;audioQueuePeak=0;audioDroppedFrames=0;audioOutputErrors=0;audioFecAttempts=0;audioStereoFallbacks=0;
 queueDepth=0;queuePeak=0;decodeCalls=0;gpuCalls=0;decodeUs=0;decodeMaxUs=0;queueMaxUs=0;gpuUs=0;gpuMaxUs=0;
 if(settings_.hardware()){
#ifdef OPENNOW_GPU
  if(!gpu::available())return false;
  const auto allocation=gpu::allocationFor(settings_);
  const auto output=video::nativeMode(settings_);
  if(!allocation||!output)return false;nativeMode_=*allocation;
  if(!nativeDecoder_.open(nativeMode_)||!nativeDecoder_.expectOutput(*output)){decodeError=nativeDecoder_.error();nativeDecoder_.close();return false;}
 #else
  return false;
 #endif
 } else {
 av_log_set_callback(decoderLog);
 auto* decoder=&ff_h264_decoder;
 if(LIBAVCODEC_VERSION_INT!=AV_VERSION_INT(61,3,100)||decoder->id!=AV_CODEC_ID_H264)return false;
 codec_=avcodec_alloc_context3(decoder);if(!codec_)return false;
 codec_->thread_count=3;codec_->thread_type=FF_THREAD_SLICE;codec_->flags|=AV_CODEC_FLAG_LOW_DELAY;
 if(avcodec_open2(codec_,decoder,nullptr)<0){stop();return false;}
 }
 if(!settings_.hardware())pixels_=static_cast<std::uint32_t*>(std::calloc(1920*1080,4));
 const bool queueReady=compressed_.open(settings_.hardware()?8:2,cap,AV_INPUT_BUFFER_PADDING_SIZE);
 if((!settings_.hardware()&&!pixels_)||!queueReady){stop();return false;}
 if(!audioOutput_.open(requestedAudioChannels)||!audioReceiver_.open(audioOutput_.channels())){stop();return false;}
 running_=true;
 if(scePthreadCreate(&videoThread_,nullptr,decode,this,"opennow-video")||scePthreadCreate(&audioThread_,nullptr,output,this,"opennow-audio")){stop();return false;}
 return true;
}
void Media::stop() noexcept {
 running_=false;pthread_cond_broadcast(&wake_);
 if(videoThread_){scePthreadJoin(videoThread_,nullptr);videoThread_=nullptr;}
 if(audioThread_){scePthreadJoin(audioThread_,nullptr);audioThread_=nullptr;}
 if(!audioOutput_.close())++audioOutputErrors;
 audioReceiver_.close();
 #ifdef OPENNOW_GPU
 pthread_mutex_lock(&lock_);clearNativePending();while(nativeInFlight_)pthread_cond_wait(&wake_,&lock_);pthread_mutex_unlock(&lock_);
 nativeDecoder_.close();
 #endif
 avcodec_free_context(&codec_);if(scaler_){sws_freeContext(scaler_);scaler_=nullptr;}
 compressed_.close();
 pthread_mutex_lock(&lock_);std::free(pixels_);pixels_=nullptr;fresh_=false;pthread_mutex_unlock(&lock_);
}
void Media::clearNativePending() noexcept {
 #ifdef OPENNOW_GPU
 if(nativePending_){nativeDecoder_.release(pendingPicture_);nativePending_=false;}
 #endif
}
void Media::requireKeyframe() noexcept {
 pthread_mutex_lock(&lock_);recovery_.invalidate();compressed_.clear();queueDepth=0;fresh_=false;clearNativePending();
 ++recoveryResets;pthread_mutex_unlock(&lock_);
}
bool Media::video(const std::uint8_t* data,std::size_t size) noexcept {
 videoBytes+=static_cast<unsigned>(size);
 const auto units=++videoUnits;
 if(units<=3){char note[256];unsigned mask=0;
  for(std::size_t i=0;data&&i+4<size;++i)if(data[i]==0&&data[i+1]==0){if(data[i+2]==1)mask|=1u<<(data[i+3]&31);else if(data[i+2]==0&&data[i+3]==1)mask|=1u<<(data[i+4]&31);}
  std::snprintf(note,sizeof(note),"VIDEO codec=%s unit=%u bytes=%zu nalTypesMask=%u",settings_.codec()==VideoCodec::hevc?"HEVC":"H264",units,size,mask);opennow_media_note(note);
 }
 if(!running_||!data||!size||size>cap)return false;
 const bool keyframe=video::Recovery::hasIdr(data,size,settings_.codec()==VideoCodec::hevc);
 pthread_mutex_lock(&lock_);
 if(compressed_.full()){
  const auto discarded=compressed_.discardBeforeKeyframe(keyframe);
  if(discarded){queueDrops+=discarded;dropped+=discarded;}
  else{
   recovery_.invalidate();compressed_.clear();queueDepth=0;fresh_=false;clearNativePending();++queueDrops;++recoveryResets;++dropped;
  }
  // A retained or incoming keyframe restores references without a decoder
  // reset or another network IDR request. With no safe prefix, fail closed.
 }
 if(!recovery_.accept(data,size,settings_.codec()==VideoCodec::hevc)){pthread_mutex_unlock(&lock_);return false;}
 const bool queued=compressed_.push(data,size,sceKernelGetProcessTime(),keyframe);queueDepth=compressed_.size();if(queueDepth.load()>queuePeak.load())queuePeak=queueDepth.load();
 if(queued)pthread_cond_signal(&wake_);pthread_mutex_unlock(&lock_);return queued;
}
void* Media::decode(void* context) {
 auto& self=*static_cast<Media*>(context);auto* packet=av_packet_alloc();auto* frame=av_frame_alloc();
 if(!packet||!frame){av_packet_free(&packet);av_frame_free(&frame);return nullptr;}
 std::uint64_t decoderEpoch=UINT64_MAX;
#ifdef OPENNOW_GPU
 video::HevcHeaders headers;unsigned headerLogs=0;
 std::uint64_t lastNativeInput=0;
 auto publishNative=[&](video::HardwareDecoder::Picture& picture,std::uint64_t epoch){
  pthread_mutex_lock(&self.lock_);
  if(!self.running_||!self.recovery_.current(epoch))self.nativeDecoder_.release(picture);
  else{self.clearNativePending();self.pendingPicture_=picture;self.nativePending_=true;++self.frames;self.decodedWidth=picture.surface.width;self.decodedHeight=picture.mode.visible_height;self.actualHdr=picture.mode.hdr;self.decodeError=0;}
  pthread_mutex_unlock(&self.lock_);
 };
 auto recoverNative=[&](){
  self.decodeError=self.nativeDecoder_.error();++self.dropped;
  const bool deeper=self.nativeDecoder_.timing().pipeline_depth>1;
  self.requireKeyframe();
  if(deeper){
   pthread_mutex_lock(&self.lock_);self.clearNativePending();while(self.nativeInFlight_)pthread_cond_wait(&self.wake_,&self.lock_);pthread_mutex_unlock(&self.lock_);
   if(!self.nativeDecoder_.fallbackToClassic())return false;
   decoderEpoch=UINT64_MAX;
  }
  return true;
 };
#endif
 while(self.running_) {
  bool drainNow=false;
  pthread_mutex_lock(&self.lock_);while(!self.compressed_.size()&&self.running_){
#ifdef OPENNOW_GPU
   const auto timing=self.nativeDecoder_.timing();
   if(self.settings_.hardware()&&timing.pipeline_depth>1&&timing.in_flight&&self.recovery_.current(decoderEpoch)){
    const auto grace=std::max(20000u,1500000u/unsigned(self.settings_.fps));
    if(sceKernelGetProcessTime()-lastNativeInput>=grace){drainNow=true;break;}
    timespec until{};
    if(clock_gettime(CLOCK_REALTIME,&until)){pthread_mutex_unlock(&self.lock_);sceKernelUsleep(5000);pthread_mutex_lock(&self.lock_);continue;}
    until.tv_nsec+=5000000;
    if(until.tv_nsec>=1000000000){++until.tv_sec;until.tv_nsec-=1000000000;}
    const int waited=pthread_cond_timedwait(&self.wake_,&self.lock_,&until);
    if(waited&&waited!=ETIMEDOUT){pthread_mutex_unlock(&self.lock_);sceKernelUsleep(5000);pthread_mutex_lock(&self.lock_);}
    continue;
   }
#endif
   pthread_cond_wait(&self.wake_,&self.lock_);
  }
  if(!self.running_){pthread_mutex_unlock(&self.lock_);break;}
  const auto packetEpoch=self.recovery_.epoch();
#ifdef OPENNOW_GPU
  if(drainNow){
   pthread_mutex_unlock(&self.lock_);bool recovered=true;
   // Once flushing starts, fully drain before taking any newly queued AU.
   while(self.running_&&self.nativeDecoder_.timing().in_flight){
    video::HardwareDecoder::Picture picture;const auto result=self.nativeDecoder_.drain(picture);
    if(result==video::HardwareDecoder::Result::blocked){sceKernelUsleep(1000);continue;}
    if(result!=video::HardwareDecoder::Result::picture){recovered=recoverNative();break;}
    publishNative(picture,packetEpoch);
   }
   if(!recovered)break;
   continue;
  }
#else
  (void)drainNow;
#endif
  const auto unit=self.compressed_.take();self.queueDepth=self.compressed_.size();pthread_mutex_unlock(&self.lock_);
  const auto waited=sceKernelGetProcessTime()-unit.received;if(waited>self.queueMaxUs.load())self.queueMaxUs=waited;
  // Native decode consumes the owned handoff buffer directly. Software FFmpeg
  // retains its refcounted packet allocation for delayed frame references.
  const std::uint8_t* data=unit.data;const auto size=unit.size;
 #ifdef OPENNOW_GPU
  if(self.settings_.hardware()){
   if(decoderEpoch!=packetEpoch){
    pthread_mutex_lock(&self.lock_);self.clearNativePending();while(self.nativeInFlight_)pthread_cond_wait(&self.wake_,&self.lock_);pthread_mutex_unlock(&self.lock_);
    if(!self.nativeDecoder_.reset()){self.decodeError=self.nativeDecoder_.error();av_packet_unref(packet);break;}decoderEpoch=packetEpoch;
   }
   if(self.settings_.codec()==VideoCodec::hevc){
    const auto previous=headers;const bool valid=video::updateHevcHeaders(data,size,headers);
    if(headerLogs<3&&(!headerLogs||(previous.width!=headers.width||previous.height!=headers.height||previous.bit_depth!=headers.bit_depth||previous.primaries!=headers.primaries||previous.transfer!=headers.transfer||previous.matrix!=headers.matrix||previous.full_range!=headers.full_range||previous.profile!=headers.profile||previous.valid!=headers.valid))){
     const std::uint8_t* nal=nullptr;std::size_t bytes=0;video::hevcSpsView(data,size,nal,bytes);
     if(nal)opennow_hevc_sps_note(nal,bytes);
     char note[256];std::snprintf(note,sizeof(note),"HEVC SPS valid=%d %ux%u profile=%u level=%u depth=%u colors=%u/%u/%u full=%d dpb=%u/%u bytes=%zu",valid,headers.width,headers.height,headers.profile,headers.level,headers.bit_depth,headers.primaries,headers.transfer,headers.matrix,headers.full_range,headers.max_dpb_frames,video::nativeDpbFrames(self.nativeMode_),bytes);opennow_media_note(note);++headerLogs;
    }
    const bool sdr=valid&&headers.primaries==1&&headers.matrix==1&&(headers.transfer==1||headers.transfer==13);
    const auto actual=video::nativeMode(headers.hdr10()?video::NativeCodec::hevc_main10:headers.bit_depth==10?video::NativeCodec::hevc_main10_sdr:video::NativeCodec::hevc,headers.width,headers.height,self.settings_.fps);
    if(!valid||(!headers.hdr10()&&!sdr)||!actual||(actual->hdr&&!self.settings_.hdr())||headers.profile!=actual->profile||headers.level>self.nativeMode_.level||headers.max_dpb_frames>video::nativeDpbFrames(self.nativeMode_)){
     self.decodeError=-1001;if(headerLogs==1){opennow_media_note("HEVC color metadata rejected; retaining the last picture");++headerLogs;}av_packet_unref(packet);self.requireKeyframe();continue;
    }
    auto output=*actual;output.full_range=headers.full_range;
    if(!self.nativeDecoder_.expectOutput(output)){self.decodeError=self.nativeDecoder_.error();av_packet_unref(packet);self.requireKeyframe();continue;}
   }
   if(video::Recovery::hasIdr(data,size,self.settings_.codec()==VideoCodec::hevc))++self.idrFrames;
   const auto decodeStart=sceKernelGetProcessTime();
   video::HardwareDecoder::Picture picture;auto result=self.nativeDecoder_.decode(data,size,unit.received,picture);
   while(result==video::HardwareDecoder::Result::blocked&&self.running_){sceKernelUsleep(1000);result=self.nativeDecoder_.decode(data,size,unit.received,picture);}
   const auto decodeTime=sceKernelGetProcessTime()-decodeStart;++self.decodeCalls;self.decodeUs+=decodeTime;if(decodeTime>self.decodeMaxUs.load())self.decodeMaxUs=decodeTime;
   lastNativeInput=sceKernelGetProcessTime();
   av_packet_unref(packet);
   if(result==video::HardwareDecoder::Result::error){if(!recoverNative())break;continue;}
   if(result==video::HardwareDecoder::Result::picture)publishNative(picture,packetEpoch);
   continue;
  }
 #endif
  if(av_new_packet(packet,size)<0)break;
  std::memcpy(packet->data,data,size);
  // Flush only on the decoder thread; recovered IDRs must not share old
  // reference pictures, and stale in-flight output must never be published.
  if(decoderEpoch!=packetEpoch){avcodec_flush_buffers(self.codec_);decoderEpoch=packetEpoch;}
  if(video::Recovery::hasIdr(packet->data,packet->size))++self.idrFrames;
  int result=avcodec_send_packet(self.codec_,packet);av_packet_unref(packet);
  if(result<0){self.decodeError=result;++self.dropped;self.requireKeyframe();continue;}
  while(avcodec_receive_frame(self.codec_,frame)==0){
   if((frame->flags&AV_FRAME_FLAG_CORRUPT)||frame->decode_error_flags){++self.corruptFrames;++self.dropped;self.requireKeyframe();av_frame_unref(frame);continue;}
   if(self.decodedWidth!=frame->width||self.decodedHeight!=frame->height){
    char note[128];std::snprintf(note,sizeof(note),"VIDEO dimensions changed: %dx%d",frame->width,frame->height);opennow_media_note(note);
   }
   self.decodedWidth=frame->width;self.decodedHeight=frame->height;
   if(frame->width<=0||frame->width>1920||frame->height<=0||frame->height>1080){++self.dropped;av_frame_unref(frame);continue;}
   const auto viewport=video::fitVideoRect(frame->width,frame->height,1920,1080);
   self.scaler_=sws_getCachedContext(self.scaler_,frame->width,frame->height,static_cast<AVPixelFormat>(frame->format),viewport.width,viewport.height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
   if(self.scaler_){
    // Honor the stream's YUV range/matrix instead of swscale's SD defaults.
    int matrix=SWS_CS_ITU709;
    if(frame->colorspace==AVCOL_SPC_BT470BG||frame->colorspace==AVCOL_SPC_SMPTE170M)matrix=SWS_CS_ITU601;
    const auto* coefficients=sws_getCoefficients(matrix);
    sws_setColorspaceDetails(self.scaler_,coefficients,frame->color_range==AVCOL_RANGE_JPEG,
                            coefficients,1,0,1<<16,1<<16);
    pthread_mutex_lock(&self.lock_);
    if(!self.recovery_.current(packetEpoch)){pthread_mutex_unlock(&self.lock_);av_frame_unref(frame);continue;}
    std::fill_n(self.pixels_,1920*1080,video::packedVideoBlack(false));
    std::uint8_t* dst[]={reinterpret_cast<std::uint8_t*>(self.pixels_+viewport.y*1920+viewport.x)};int stride[]={1920*4};sws_scale(self.scaler_,frame->data,frame->linesize,0,frame->height,dst,stride);self.fresh_=true;++self.frames;pthread_mutex_unlock(&self.lock_);}
   av_frame_unref(frame);
  }
 }
 av_packet_free(&packet);av_frame_free(&frame);return nullptr;
}
void Media::audio(const std::uint8_t* data,std::size_t size,std::uint16_t sequence,std::uint8_t payloadType,std::uint32_t timestamp) noexcept {
 if(!running_)return;
 const auto fallbacks=audioStereoFallbacks.load();
 audioReceiver_.receive(data,size,sequence,payloadType,timestamp);updateAudioStats();
 if(audioStereoFallbacks.load()>fallbacks)opennow_media_note("AUDIO startup fallback: server sent stereo instead of described surround");
}
void* Media::output(void* context) {
 auto& self=*static_cast<Media*>(context);alignas(64) std::int16_t block[256*8];
 while(self.running_){self.audioReceiver_.pop(block);self.updateAudioStats();
  if(self.audioOutput_.write(block)<0){++self.audioOutputErrors;sceKernelUsleep(5000);}
 }
 return nullptr;
}
bool Media::draw(ps5::demo::Canvas& c,bool redraw) noexcept {
 #ifdef OPENNOW_GPU
 if(settings_.hardware()){
  pthread_mutex_lock(&lock_);if(!nativePending_){pthread_mutex_unlock(&lock_);return redraw&&gpu::hasRetainedVideo();}
  const auto picture=pendingPicture_;nativePending_=false;nativeInFlight_=true;pthread_mutex_unlock(&lock_);
  const auto drawStart=sceKernelGetProcessTime();const bool drawn=gpu::drawVideo(picture.surface,picture.mode);nativeDecoder_.release(picture);
  const auto drawTime=sceKernelGetProcessTime()-drawStart;++gpuCalls;gpuUs+=drawTime;if(drawTime>gpuMaxUs.load())gpuMaxUs=drawTime;
  pthread_mutex_lock(&lock_);nativeInFlight_=false;pthread_cond_broadcast(&wake_);pthread_mutex_unlock(&lock_);
  if(drawn)++presented;else{decodeError=-1002;requireKeyframe();}return drawn;
 }
 #endif
 pthread_mutex_lock(&lock_);const bool changed=(fresh_||redraw)&&pixels_;if(changed){c.image(0,0,1920,1080,pixels_);if(fresh_)++presented;fresh_=false;}pthread_mutex_unlock(&lock_);return changed;
}
}
