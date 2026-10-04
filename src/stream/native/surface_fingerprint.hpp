// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../hardware_video_contract.hpp"
#include <optional>
#include <atomic>
namespace opennow::video {
struct SurfaceFingerprint {std::uint64_t hash=0;unsigned luma_span=0;};
// Sample visible luma and both chroma components, ignoring coded padding.
// Only caller-owned, completed decoder output may be inspected here.
inline std::optional<SurfaceFingerprint> fingerprintSurface(const NativeSurface& s,const NativeMode& m) noexcept {
 if(!s.buffer||!s.width||s.width!=m.visible_width||s.height<m.visible_height||s.height>2176||
    s.pitch_components<s.width||s.pitch_components>3840||!m.visible_height||m.visible_height>2160)return {};
 const unsigned bytes=m.storage==SampleStorage::low_aligned_10bit?2u:1u;
 if(s.pitch_bytes!=s.pitch_components*bytes||s.buffer_bytes<std::size_t(s.pitch_bytes)*s.height*3/2)return {};
 const auto* base=static_cast<const unsigned char*>(s.buffer);
 std::uint64_t hash=14695981039346656037ull;unsigned lo=65535,hi=0;
 for(unsigned plane=0;plane<2;++plane){
  const auto rows=m.visible_height/(plane?2:1),columns=m.visible_width/(plane?2:1);
  if(!rows||!columns)return {};
  const auto offset=plane?std::size_t(s.pitch_bytes)*s.height:0;
  for(unsigned r=0;r<18;++r)for(unsigned c=0;c<32;++c){
   const auto row=std::size_t(r)*(rows-1)/17,column=std::size_t(c)*(columns-1)/31;
   const auto* at=base+offset+row*s.pitch_bytes+column*(plane?2:1)*bytes;
#if defined(__x86_64__)
   __builtin_ia32_clflush(at);__builtin_ia32_mfence();
#else
   std::atomic_thread_fence(std::memory_order_acquire);
#endif
   for(unsigned component=0;component<(plane?2u:1u);++component){
    const volatile unsigned char* word=at+component*bytes;
    unsigned value=word[0];if(bytes==2)value|=unsigned(word[1])<<8;
    hash=(hash^value)*1099511628211ull;
    if(!plane){if(value<lo)lo=value;if(value>hi)hi=value;}
   }
  }
 }
 return SurfaceFingerprint{hash,hi-lo};
}
}
