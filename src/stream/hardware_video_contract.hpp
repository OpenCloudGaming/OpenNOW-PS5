// SPDX-License-Identifier: GPL-3.0-or-later
// Native video contracts grounded in the pinned ProsperoLight implementation
// and ps5-hardware-video-decoding-research. This is not a decoder backend.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace opennow::video {
enum class NativeCodec { h264, hevc, hevc_main10, hevc_main10_sdr };
enum class SampleStorage { nv12, low_aligned_10bit };
struct NativeMode {
 NativeCodec codec;
 std::uint32_t codec_type,profile,level;
 unsigned visible_width,visible_height,coded_height,pitch_components,fps;
 SampleStorage storage;
 bool hdr;
 bool full_range=false;
};
// GFN's four-reference HEVC stream signals a five-picture DPB in its SPS.
// Reserve six including reorder/current-picture headroom, not four references.
inline unsigned nativeDpbFrames(const NativeMode& mode) {
 return mode.codec==NativeCodec::h264?4u:6u;
}
inline std::optional<NativeMode> nativeMode(NativeCodec codec,unsigned width,unsigned height,unsigned fps) {
 if(width<320||height<180||width>3840||height>2160||(width&1)||(height&1))return {};
 if(fps!=30&&fps!=60&&fps!=90&&fps!=120)return {};
 if(codec!=NativeCodec::h264&&codec!=NativeCodec::hevc&&codec!=NativeCodec::hevc_main10&&codec!=NativeCodec::hevc_main10_sdr)return {};
 const bool avc=codec==NativeCodec::h264,hdr=codec==NativeCodec::hevc_main10,tenBit=hdr||codec==NativeCodec::hevc_main10_sdr;
 const unsigned coded=(height+31u)&~31u;
 const unsigned bytes=tenBit?2:1;
 const unsigned pitch=((width*bytes+255u)&~255u)/bytes;
 unsigned level;
 if(avc)level=width>2560?(fps>60?60:52):width>1920&&fps>60?52:51;
 else level=width>2560?(fps>60?156:153):width>1920?(fps>60?153:150):(fps>60?150:123);
 return NativeMode{codec,avc?1u:0x000ee049u,avc?100u:tenBit?2u:1u,level,
                   width,height,coded,pitch,fps,tenBit?SampleStorage::low_aligned_10bit:SampleStorage::nv12,hdr};
}
struct NativeSurface {
 const void* buffer=nullptr;
 std::size_t buffer_bytes=0;
 unsigned width=0,height=0,pitch_components=0,pitch_bytes=0;
 std::uint32_t codec_type=0;
 unsigned picture_count=0;
};
// Validate a returned pointer against its exact caller-owned slot. A decoder
// may retain a slot and return another later; the GPU fence controls reuse.
inline bool validNativeSurface(const NativeMode& mode,const NativeSurface& surface,
                               const void* owned_slot,std::size_t slot_bytes) {
 if(!owned_slot||surface.buffer!=owned_slot||!surface.buffer_bytes||surface.buffer_bytes>slot_bytes)return false;
 if(surface.width!=mode.visible_width||surface.codec_type!=mode.codec_type||surface.picture_count!=1)return false;
 if(surface.height!=mode.coded_height&&surface.height!=mode.visible_height)return false;
 if(surface.pitch_components!=mode.pitch_components)return false;
 const std::size_t sample_bytes=mode.storage==SampleStorage::low_aligned_10bit?2:1;
 if(surface.pitch_bytes!=mode.pitch_components*sample_bytes)return false;
 const std::size_t required=std::size_t(surface.pitch_bytes)*surface.height*3/2;
 return surface.buffer_bytes>=required;
}
// Dynamic-resolution output can retain the allocation's padded pitch and rows.
// Validate those against the original allocation before importing GPU memory.
inline bool validNativeSurfaceInEnvelope(const NativeMode& output,const NativeMode& allocation,
                                         const NativeSurface& surface,const void* owned_slot,std::size_t slot_bytes) {
 if(!owned_slot||surface.buffer!=owned_slot||!surface.buffer_bytes||surface.buffer_bytes>slot_bytes)return false;
 if(surface.width!=output.visible_width||surface.codec_type!=output.codec_type||surface.picture_count!=1)return false;
 if(output.storage!=allocation.storage||output.codec_type!=allocation.codec_type)return false;
 if(surface.height<output.visible_height||surface.height>allocation.coded_height||(surface.height&1))return false;
 if(surface.pitch_components<output.pitch_components||surface.pitch_components>allocation.pitch_components)return false;
 const std::size_t sample_bytes=output.storage==SampleStorage::low_aligned_10bit?2:1;
 if(surface.pitch_bytes!=surface.pitch_components*sample_bytes||(surface.pitch_bytes&255))return false;
 const std::size_t required=std::size_t(surface.pitch_bytes)*surface.height*3/2;
 return surface.buffer_bytes>=required;
}
struct HdrSignals {
 bool ten_bit=false,bt2020_primaries=false,bt2020_ncl=false,pq=false,limited_range=false;
};
inline bool validHdrSignals(const NativeMode& mode,const HdrSignals& signals) {
 return mode.hdr&&signals.ten_bit&&signals.bt2020_primaries&&signals.bt2020_ncl&&signals.pq&&signals.limited_range;
}
// Successful memory queries alone do not qualify a mode for GFN negotiation.
struct NativeQualification {
 bool decoder_created=false,decode_output_validated=false,gpu_surface_import=false;
 bool completed_flip=false,hdr_output=false;
 unsigned output_width=0,output_height=0,output_refresh_hz=0;
};
inline bool canNegotiateNativeMode(const NativeMode& mode,const NativeQualification& qualified) {
 return qualified.decoder_created&&qualified.decode_output_validated&&qualified.gpu_surface_import&&qualified.completed_flip&&
        qualified.output_width>=mode.visible_width&&qualified.output_height>=mode.visible_height&&
        qualified.output_refresh_hz>=mode.fps&&(!mode.hdr||qualified.hdr_output);
}
}
