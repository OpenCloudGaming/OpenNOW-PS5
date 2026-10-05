#include "stream/hardware_video_contract.hpp"
#include <cassert>
#include <cstdint>
int main(){
 using namespace opennow::video;
 auto mode=nativeMode(NativeCodec::hevc_main10,3840,2160,120);assert(mode);
 assert(mode->profile==2&&mode->level==156&&mode->coded_height==2176);
 assert(mode->storage==SampleStorage::low_aligned_10bit);
 assert(!nativeMode(NativeCodec::hevc_main10,3840,2160,240));
 const auto hdr90=nativeMode(NativeCodec::hevc_main10,3840,2160,90);
 const auto sdr90=nativeMode(NativeCodec::h264,3840,2160,90);
 assert(hdr90&&hdr90->hdr&&hdr90->fps==90&&hdr90->level==156);
 assert(sdr90&&!sdr90->hdr&&sdr90->fps==90&&sdr90->level==60);
 assert(nativeMode(NativeCodec::h264,3840,2160,91));
 assert(!nativeMode(NativeCodec::h264,3840,2160,121));
 NativeQualification q90={true,true,true,true,true,3840,2160,120};
 assert(canNegotiateNativeMode(*hdr90,q90)&&canNegotiateNativeMode(*sdr90,q90));
 q90.output_refresh_hz=60;assert(!canNegotiateNativeMode(*hdr90,q90));
 assert(!nativeMode(NativeCodec::hevc_main10,1921,2160,120));
 assert(!nativeMode(NativeCodec::hevc_main10,3842,2160,120));
 std::uint64_t owned=0,foreign=0;
 const std::size_t allocation=3840u*2176u*3u;
 NativeSurface surface{&owned,allocation,3840,2176,3840,7680,mode->codec_type,1};
 assert(validNativeSurface(*mode,surface,&owned,allocation));
 surface.buffer=&foreign;assert(!validNativeSurface(*mode,surface,&owned,allocation));surface.buffer=&owned;
 surface.pitch_bytes=3840;assert(!validNativeSurface(*mode,surface,&owned,allocation));surface.pitch_bytes=7680;
 surface.buffer_bytes=allocation-1;assert(!validNativeSurface(*mode,surface,&owned,allocation));surface.buffer_bytes=allocation;
 surface.picture_count=2;assert(!validNativeSurface(*mode,surface,&owned,allocation));surface.picture_count=1;
 surface.width=1920;assert(!validNativeSurface(*mode,surface,&owned,allocation));surface.width=3840;
 assert(validHdrSignals(*mode,{true,true,true,true,true}));
 assert(!validHdrSignals(*mode,{true,true,true,false,true}));
 assert(!validHdrSignals(*mode,{true,true,true,true,false}));
 auto sdr=nativeMode(NativeCodec::hevc,3840,2160,120);assert(sdr&&!sdr->hdr);
 assert(!validHdrSignals(*sdr,{true,true,true,true,true}));
 NativeQualification qualified{};assert(!canNegotiateNativeMode(*mode,qualified));
 qualified={true,true,true,true,true,3840,2160,120};assert(canNegotiateNativeMode(*mode,qualified));
 qualified.output_refresh_hz=60;assert(!canNegotiateNativeMode(*mode,qualified));
 qualified.output_refresh_hz=120;qualified.hdr_output=false;assert(!canNegotiateNativeMode(*mode,qualified));
 assert(canNegotiateNativeMode(*sdr,qualified));
 qualified.gpu_surface_import=false;assert(!canNegotiateNativeMode(*sdr,qualified));
 // 1080p SDR has padded component pitch; a GPU importer must honor it.
 auto avc=nativeMode(NativeCodec::h264,1920,1080,60);assert(avc&&avc->pitch_components==2048);
 assert(!avc->hdr&&avc->profile==100&&avc->codec_type==1);
 surface={&owned,2048u*1088u*3u/2,1920,1088,2048,2048,1,1};
 assert(validNativeSurface(*avc,surface,&owned,surface.buffer_bytes));
 surface.pitch_components=1920;assert(!validNativeSurface(*avc,surface,&owned,surface.buffer_bytes));
 // GFN can send Main10 BT.709 SDR and resize while retaining the UHD DPB.
 const auto resized=*nativeMode(NativeCodec::hevc_main10_sdr,2880,1620,120);
 assert(!resized.hdr&&resized.profile==2&&resized.coded_height==1632&&resized.pitch_components==2944);
 surface={&owned,allocation,2880,1632,2944,5888,mode->codec_type,1};
 assert(validNativeSurfaceInEnvelope(resized,*mode,surface,&owned,allocation));
 surface.height=2176;surface.pitch_components=3840;surface.pitch_bytes=7680;
 assert(validNativeSurfaceInEnvelope(resized,*mode,surface,&owned,allocation));
 surface.buffer=&foreign;assert(!validNativeSurfaceInEnvelope(resized,*mode,surface,&owned,allocation));surface.buffer=&owned;
 surface.buffer_bytes=allocation-1;assert(!validNativeSurfaceInEnvelope(resized,*mode,surface,&owned,allocation));surface.buffer_bytes=allocation;
 surface.height=2178;assert(!validNativeSurfaceInEnvelope(resized,*mode,surface,&owned,allocation));surface.height=2176;
 surface.pitch_bytes=3840;assert(!validNativeSurfaceInEnvelope(resized,*mode,surface,&owned,allocation));
}
