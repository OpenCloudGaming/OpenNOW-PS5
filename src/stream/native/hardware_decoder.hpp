// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../hardware_video_contract.hpp"
#include <array>
#include <atomic>
#include <mutex>
struct sceVideodec2Output;

namespace opennow::video {
// Native backend used by qualified GPU streaming profiles.
// Decode/open/reset/close belong to one decoder thread. release() can run
// on the presenter thread, only after the GPU has finished sampling a slot.
class HardwareDecoder final {
public:
 enum class Result { picture, no_picture, blocked, error };
 struct Picture { NativeSurface surface; unsigned slot=0; std::uint64_t lease=0; NativeMode mode{}; std::uint64_t pts=0; };
 struct Timing {std::uint64_t copy_us=0,publish_us=0,decode_us=0,flush_us=0,decode_calls=0,flush_calls=0,blocked_attempts=0,worker_mask=0;unsigned pipeline_depth=0,in_flight=0;};
 Timing timing() const noexcept{return {copyUs_.load(),publishUs_.load(),apiUs_.load(),flushUs_.load(),apiCalls_.load(),flushCalls_.load(),blockedAttempts_.load(),workerMask_.load(),pipelineDepth_.load(),inFlight_.load()};}
 HardwareDecoder()=default;
 ~HardwareDecoder();
 HardwareDecoder(const HardwareDecoder&)=delete;
 HardwareDecoder& operator=(const HardwareDecoder&)=delete;
 // Call before any sandbox root change, on the process startup thread.
 static int loadModule() noexcept;
 bool open(const NativeMode&) noexcept;
 // Only startup qualification may select a deeper mode. Live open retries
 // depth one if a previously qualified deeper configuration is refused.
 bool openForQualification(const NativeMode&,unsigned depth) noexcept;
 static bool setQualifiedMain10Depth(unsigned depth) noexcept;
 // Startup-only capability probe on a closed instance. Opens and closes
 // deeper configurations without submitting any compressed input.
 bool probePipelineCreation(const NativeMode&) noexcept;
 Result decode(const std::uint8_t*,std::size_t,std::uint64_t pts,Picture&) noexcept;
 // Once drain starts, finish it before submitting another access unit.
 Result drain(Picture&) noexcept;
 bool fallbackToClassic() noexcept;
 bool release(const Picture&) noexcept;
 // Output may resize inside the allocation envelope, keeping the DPB intact.
 bool expectOutput(const NativeMode&) noexcept;
 // Busy reset/close preserve GPU-visible memory rather than unmapping it.
 bool reset() noexcept;
 bool close() noexcept;
 int error() const noexcept { return error_; }
 std::uint64_t workerMask() const noexcept {return workerMask_.load();}
 std::size_t accessUnitLimit() const noexcept { return input_.address?inputCapacity:0; }
private:
 struct Memory { void* address=nullptr; std::size_t bytes=0; std::int64_t start=-1; };
 enum class State { free, decoder, presenter };
 struct Slot { State state=State::free; std::uint64_t lease=0; };
 static constexpr unsigned slots=12,inputSlots=4;
 static constexpr std::size_t inputCapacity=8*1024*1024;
 struct Submission {unsigned input=0;std::uint64_t pts=0;NativeMode mode{};};
 bool allocate(std::size_t,int,Memory&) noexcept;
 static void free(Memory&) noexcept;
 bool fail(int) noexcept;
 bool openConfigured(const NativeMode&,std::uint64_t,unsigned depth=1) noexcept;
 bool openWithDepth(const NativeMode&,unsigned depth) noexcept;
 unsigned freePicture() noexcept;
 Result settle(unsigned offered,bool accepted,const ::sceVideodec2Output&,Picture&) noexcept;
 bool leased() const noexcept;
 void* address(unsigned) const noexcept;
 NativeMode mode_{},outputMode_{};
 Memory compute_,gpu_,shared_,input_,pictures_;
 void* cpu_=nullptr;
 std::size_t cpuBytes_=0,frameBytes_=0;
 void* queue_=nullptr;
 void* decoder_=nullptr;
 int error_=0;
 bool poisoned_=false;
 unsigned errorNotes_=0;
 bool pictureNoted_=false;
 bool affinityRefused_=false;
 std::atomic_uint pipelineDepth_{0},inFlight_{0};
 std::array<bool,inputSlots> inputUsed_{};
 std::array<Submission,inputSlots> submissions_{};
 unsigned submissionHead_=0,submissionCount_=0;
 bool draining_=false;
 std::atomic<std::uint64_t> workerMask_{0};
 std::uint64_t leaseSerial_=0;
 std::array<Slot,slots> pool_{};
 mutable std::mutex mutex_;
 std::atomic<std::uint64_t> copyUs_{0},publishUs_{0},apiUs_{0},flushUs_{0},apiCalls_{0},flushCalls_{0},blockedAttempts_{0};
};
}
