// SPDX-License-Identifier: GPL-3.0-or-later
// Byte layout and successful-send accounting adapted from OpenNOW commit
// 3217d80c029a23a4d558f81c8611e37d0ad7a80d (nvst_control.rs, PR #908).
#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace opennow::webrtc {
struct QosSample { std::uint32_t sequence=0,frames=0,bytes=0,timestamp=0; };
inline std::array<std::uint8_t,56> qosPacket(const QosSample& current,const QosSample& sent,bool warmed) {
 std::array<std::uint8_t,56> p{};
 auto put=[&](unsigned at,std::uint32_t value,unsigned n=4){while(n--){p[at++]=value&255;value>>=8;}};
 put(0,0x207,2);put(2,52,2);
 put(4,7);put(12,current.sequence);put(16,current.frames);put(20,current.bytes);
 put(32,warmed?2:0,2);put(34,1000,2);put(36,1000,2);put(38,12708,2);
 put(40,current.timestamp);
 const std::uint64_t bits=std::uint64_t(current.bytes-sent.bytes)*8;
 put(48,warmed?static_cast<std::uint32_t>(bits>UINT32_MAX?UINT32_MAX:bits):0);
 put(52,sent.bytes);return p;
}
class QosFeedback {
public:
 void received(std::size_t bytes){++frames_;bytes_+=static_cast<std::uint32_t>(bytes);}
 QosSample next(std::uint32_t timestamp) const {return {sent_.sequence+1,frames_,bytes_,timestamp};}
 auto packet(const QosSample& sample,bool warmed) const {return qosPacket(sample,sent_,warmed);}
 void queued(const QosSample& sample){sent_=sample;}
 std::uint32_t queuedCount() const {return sent_.sequence;}
private:
 std::uint32_t frames_=0,bytes_=0;
 QosSample sent_{};
};
}
