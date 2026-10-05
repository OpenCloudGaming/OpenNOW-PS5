// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/stream_settings.hpp"
#include "stream/hardware_video_contract.hpp"
#include "stream/video_geometry.hpp"
#include <cassert>
#include <climits>
#include <utility>

int main() {
 using namespace opennow;
 using namespace opennow::video;
 for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i)
  assert(validateSettings(settingsFor(static_cast<StreamProfile>(i)))==SettingsError::none);
 for(auto mode:{VideoMode::h264Software,VideoMode::h264Hardware,VideoMode::hevcMain10SdrHardware,VideoMode::hevcMain10HdrHardware}){
  StreamSettings settings;settings.mode=mode;settings.network=NetworkPolicy::fixed;
  assert(settings.codec()==(settings.tenBit()?VideoCodec::hevc:VideoCodec::h264));
  assert(settings.hdr()==(mode==VideoMode::hevcMain10HdrHardware));
  for(int fps=30;fps<=120;++fps){settings.fps=fps;assert((validateSettings(settings)==SettingsError::none)==(settings.hardware()||fps<=60));}
  for(int fps:{INT_MIN,0,29,121,INT_MAX}){settings.fps=fps;assert(validateSettings(settings)==SettingsError::fps);}
  settings.fps=30;
  for(int bitrate:{4000,100000}){settings.bitrate_kbps=bitrate;assert(validateSettings(settings)==SettingsError::none);}
  for(int bitrate:{INT_MIN,0,3999,100001,INT_MAX}){settings.bitrate_kbps=bitrate;assert(validateSettings(settings)==SettingsError::bitrate);}
 }
 StreamSettings settings;
 for(auto dimensions:{std::pair{320,180},std::pair{1440,1080},std::pair{1920,810}}){settings.width=dimensions.first;settings.height=dimensions.second;assert(validateSettings(settings)==SettingsError::none);}
 for(auto dimensions:{std::pair{319,180},std::pair{320,179},std::pair{1922,1080},std::pair{320,1082},std::pair{INT_MAX,180}}){settings.width=dimensions.first;settings.height=dimensions.second;assert(validateSettings(settings)==SettingsError::dimensions);}
 settings={3840,2160,120,100000,VideoMode::hevcMain10HdrHardware};assert(validateSettings(settings)==SettingsError::none);
 settings.width=3842;assert(validateSettings(settings)==SettingsError::dimensions);
 settings.width=3840;settings.height=2162;assert(validateSettings(settings)==SettingsError::dimensions);
 settings={};settings.mode=static_cast<VideoMode>(4);assert(validateSettings(settings)==SettingsError::mode);
 settings={};settings.quality=static_cast<QualityMode>(3);assert(validateSettings(settings)==SettingsError::quality);
 settings={};settings.network=static_cast<NetworkPolicy>(2);assert(validateSettings(settings)==SettingsError::network);
 settings={};settings.mode=VideoMode::h264Hardware;assert(validateSettings(settings)==SettingsError::network);
 settings.network=NetworkPolicy::fixed;assert(validateSettings(settings)==SettingsError::none);

 assert(nativeMode(NativeCodec::hevc_main10,1920,1080,60)->level==123);
 assert(nativeMode(NativeCodec::hevc_main10,1920,2160,60)->level==150);
 assert(nativeMode(NativeCodec::hevc_main10,2560,2160,60)->level==153);
 assert(nativeMode(NativeCodec::hevc_main10,3840,2160,120)->level==156);
 assert(nativeMode(NativeCodec::h264,2560,2160,45)->level==51);
 assert(nativeMode(NativeCodec::h264,2560,2160,46)->level==52);
 for(unsigned fps=30;fps<=120;++fps)assert(nativeMode(NativeCodec::h264,1440,1080,fps));
 for(unsigned fps:{0u,29u,121u,240u,UINT_MAX})assert(!nativeMode(NativeCodec::h264,1440,1080,fps));

 NativeEnvelope envelope{*nativeMode(NativeCodec::hevc_main10,3840,2160,120),{true,true,true,true,true,3840,2160,60}};
 settings=settingsFor(StreamProfile::native_hdr60);assert(supportsSettings(envelope,settings));
 settings.fps=61;assert(!supportsSettings(envelope,settings));
 settings.fps=47;settings.width=1440;settings.height=1080;assert(supportsSettings(envelope,settings));
 settings.mode=VideoMode::hevcMain10SdrHardware;assert(!supportsSettings(envelope,settings));
 envelope.allocation=*nativeMode(NativeCodec::hevc_main10_sdr,3840,2160,120);envelope.qualification.hdr_output=false;
 assert(supportsSettings(envelope,settings));
 settings.mode=VideoMode::hevcMain10HdrHardware;assert(!supportsSettings(envelope,settings));
 settings.mode=VideoMode::hevcMain10SdrHardware;
 envelope.qualification.gpu_surface_import=false;assert(!supportsSettings(envelope,settings));
 envelope.qualification.gpu_surface_import=true;envelope.qualification.completed_flip=false;assert(!supportsSettings(envelope,settings));
 envelope.qualification.completed_flip=true;envelope.qualification.decode_output_validated=false;assert(!supportsSettings(envelope,settings));
 envelope.qualification.decode_output_validated=true;envelope.qualification.decoder_created=false;assert(!supportsSettings(envelope,settings));
 envelope.qualification.decoder_created=true;envelope.qualification.output_width=1280;assert(!supportsSettings(envelope,settings));
 envelope={*nativeMode(NativeCodec::h264,1920,1080,60),{true,true,true,true,false,3840,2160,120}};
 settings={1440,1080,59,32000,VideoMode::h264Hardware,QualityMode::original,NetworkPolicy::fixed};assert(supportsSettings(envelope,settings));
 settings.fps=61;assert(!supportsSettings(envelope,settings));
 settings.fps=59;settings.width=2560;assert(!supportsSettings(envelope,settings));

 assert((fitVideoRect(1920,1080,1920,1080)==VideoRect{0,0,1920,1080}));
 assert((fitVideoRect(1440,1080,1920,1080)==VideoRect{240,0,1440,1080}));
 assert((fitVideoRect(2560,1080,1920,1080)==VideoRect{0,135,1920,810}));
 assert((fitVideoRect(1440,1080,3840,2160)==VideoRect{480,0,2880,2160}));
 assert((fitVideoRect(2560,1080,3840,2160)==VideoRect{0,270,3840,1620}));
 assert((fitVideoRect(UINT_MAX,UINT_MAX,3840,2160)==VideoRect{840,0,2160,2160}));
 assert(fitVideoRect(0,1080,1920,1080)==VideoRect{});
 for(auto dimensions:{std::pair{1440,1080},std::pair{1920,810},std::pair{1920,1080},std::pair{320,180}}){
  const auto rect=fitVideoRect(dimensions.first,dimensions.second,1920,1080);
  const std::size_t first=rect.y*1920+rect.x,last=first+(rect.height-1)*1920+rect.width;
  assert(rect.width&&rect.height&&rect.x+rect.width<=1920&&rect.y+rect.height<=1080&&last<=1920u*1080u);
 }
 assert(packedVideoBlack(true)==0xc0000000U&&packedVideoBlack(false)==0xff000000U);
}
