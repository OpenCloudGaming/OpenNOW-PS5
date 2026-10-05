#pragma once

#include "audio_format.hpp"
#include "audio_playout.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

struct OpusMSDecoder;

namespace opennow::audio {

struct Stats {
    unsigned packets=0,errors=0,recovered=0,concealed=0,fecAttempts=0;
    unsigned bytes=0,samples=0,channels=0,queueFrames=0,queuePeak=0;
    unsigned droppedFrames=0,underruns=0,stereoFallbacks=0;
};

class Receiver {
public:
    Receiver() = default;
    ~Receiver();
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    bool open(unsigned outputChannels) noexcept;
    bool configure(const Format& format) noexcept;
    void receive(const std::uint8_t* data,std::size_t size,std::uint16_t sequence,
                 std::uint8_t payload,std::uint32_t timestamp) noexcept;
    void pop(std::int16_t* block) noexcept;
    void close() noexcept;
    Stats stats() const noexcept;

private:
    void resetEpoch() noexcept;
    void releaseCodec() noexcept;
    mutable std::mutex mutex_;
    OpusMSDecoder* decoder_=nullptr;
    PlayoutQueue queue_;
    std::array<std::int16_t,5760*8> decoded_{};
    Stats stats_{};
    unsigned outputChannels_=0,channels_=0,probePackets_=0,lastPacketSamples_=0;
    int payload_=-1,redPayload_=-1;
    std::uint16_t lastSequence_=0;
    std::uint32_t lastTimestamp_=0,expectedTimestamp_=0;
    std::uint64_t lastArrival_=0;
    bool sequenceSeen_=false,haveExpected_=false,surroundEstablished_=false;
};

}
