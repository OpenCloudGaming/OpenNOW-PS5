#include "audio_receiver.hpp"
#include "AudioRtpUtils.hpp"
#include <algorithm>
#include <chrono>
#include <opus/opus.h>
#include <opus/opus_multistream.h>

namespace opennow::audio {

Receiver::~Receiver() { close(); }

void Receiver::resetEpoch() noexcept {
    if(decoder_)opus_multistream_decoder_ctl(decoder_,OPUS_RESET_STATE);
    queue_.clear();
    sequenceSeen_=haveExpected_=false;
    probePackets_=lastPacketSamples_=0;
    lastArrival_=0;
}

void Receiver::releaseCodec() noexcept {
    if(decoder_)opus_multistream_decoder_destroy(decoder_);
    decoder_=nullptr;
    channels_=0;
    payload_=redPayload_=-1;
    surroundEstablished_=false;
    resetEpoch();
}

bool Receiver::open(unsigned outputChannels) noexcept {
    std::lock_guard<std::mutex> guard(mutex_);
    releaseCodec();
    stats_={};
    outputChannels_=outputChannels==2||outputChannels==8?outputChannels:0;
    queue_.open(outputChannels_?outputChannels_:2);
    return outputChannels_!=0;
}

bool Receiver::configure(const Format& format) noexcept {
    std::lock_guard<std::mutex> guard(mutex_);
    releaseCodec();
    stats_.channels=0;
    queue_.open(outputChannels_?outputChannels_:2);
    if(!outputChannels_||!validFormat(format)||format.channels>outputChannels_)return false;
    int error=OPUS_OK;
    decoder_=opus_multistream_decoder_create(48000,format.channels,format.streams,
                                            format.coupled,format.mapping.data(),&error);
    if(!decoder_||error!=OPUS_OK){releaseCodec();return false;}
    channels_=format.channels;
    payload_=format.payload;
    redPayload_=format.redPayload;
    stats_.channels=channels_;
    return true;
}

void Receiver::receive(const std::uint8_t* data,std::size_t size,std::uint16_t sequence,
                       std::uint8_t payloadType,std::uint32_t timestamp) noexcept {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.packets;
    if(!decoder_||!data||!size||size>16384||
       (payloadType!=payload_&&payloadType!=redPayload_)){
        ++stats_.errors;probePackets_=0;return;
    }
    const auto payload=ParseRedPrimary(data,size,payloadType,payload_,redPayload_);
    if(!payload.data||!payload.size){++stats_.errors;probePackets_=0;return;}
    const int packetSamples=opus_packet_get_nb_samples(payload.data,payload.size,48000);
    if(packetSamples<=0||packetSamples>5760){++stats_.errors;probePackets_=0;return;}
    const auto arrival=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if(ShouldResetEpoch(lastArrival_,arrival,sequenceSeen_,
                        static_cast<std::uint16_t>(lastSequence_+1),sequence,12))resetEpoch();
    const auto sequenceDelta=static_cast<std::uint16_t>(sequence-lastSequence_);
    if(sequenceSeen_&&(sequenceDelta==0||sequenceDelta>=0x8000u)){
        probePackets_=0;return;
    }
    const auto timestampDelta=timestamp-lastTimestamp_;
    if(sequenceSeen_&&(timestampDelta==0||timestampDelta>=0x80000000u)){
        probePackets_=0;return;
    }
    if(sequenceSeen_&&(sequenceDelta!=1||timestamp!=lastTimestamp_+lastPacketSamples_))probePackets_=0;
    stats_.bytes+=static_cast<unsigned>(payload.size);
    if(haveExpected_&&timestamp!=expectedTimestamp_){
        const int gap=RecoverySamples(timestamp,expectedTimestamp_);
        if(gap){
            const auto redundant=payload.red?ParseLatestRedundant(data,size,payload_):RedundantPayload{};
            int recovered=0;
            if(redundant.data&&redundant.timestamp_offset==gap&&
               opus_packet_get_nb_samples(redundant.data,redundant.size,48000)==gap){
                recovered=opus_multistream_decode(decoder_,redundant.data,redundant.size,
                                                 decoded_.data(),gap,0);
                if(recovered>0)++stats_.recovered;
            }
            if(recovered<=0){
                if(sequenceDelta==2&&gap==packetSamples){
                    ++stats_.fecAttempts;
                    recovered=opus_multistream_decode(decoder_,payload.data,payload.size,
                                                     decoded_.data(),gap,1);
                }
                if(recovered<=0)recovered=opus_multistream_decode(decoder_,nullptr,0,decoded_.data(),gap,0);
                if(recovered>0)++stats_.concealed;
            }
            if(recovered>0){
                queue_.push(decoded_.data(),recovered,channels_);
                expectedTimestamp_=timestamp;
            }else ++stats_.errors;
        }else resetEpoch();
    }
    sequenceSeen_=true;
    lastSequence_=sequence;
    lastTimestamp_=timestamp;
    lastPacketSamples_=static_cast<unsigned>(packetSamples);
    lastArrival_=arrival;
    int samples=opus_multistream_decode(decoder_,payload.data,payload.size,decoded_.data(),5760,0);
    if(samples<=0&&channels_>2&&!surroundEstablished_){
        int error=OPUS_OK;
        auto* probe=opus_decoder_create(48000,2,&error);
        const int stereoSamples=probe&&error==OPUS_OK?
            opus_decode(probe,payload.data,payload.size,decoded_.data(),5760,0):0;
        if(probe)opus_decoder_destroy(probe);
        probePackets_=stereoSamples==packetSamples?probePackets_+1:0;
        if(probePackets_>=8){
            const unsigned char mapping[]={0,1};
            auto* stereo=opus_multistream_decoder_create(48000,2,1,1,mapping,&error);
            if(stereo&&error==OPUS_OK){
                opus_multistream_decoder_destroy(decoder_);
                decoder_=stereo;
                channels_=stats_.channels=2;
                queue_.clear();
                haveExpected_=false;
                ++stats_.stereoFallbacks;
                samples=opus_multistream_decode(decoder_,payload.data,payload.size,decoded_.data(),5760,0);
            }else if(stereo)opus_multistream_decoder_destroy(stereo);
            probePackets_=0;
        }
    }else probePackets_=0;
    if(samples<=0){++stats_.errors;return;}
    if(channels_>2)surroundEstablished_=true;
    haveExpected_=true;
    expectedTimestamp_=timestamp+static_cast<unsigned>(samples);
    stats_.samples+=static_cast<unsigned>(samples);
    queue_.push(decoded_.data(),samples,channels_);
}

void Receiver::pop(std::int16_t* block) noexcept {
    if(!block)return;
    std::lock_guard<std::mutex> guard(mutex_);
    std::fill_n(block,256*8,0);
    if(decoder_)queue_.pop(block);
}

void Receiver::close() noexcept {
    std::lock_guard<std::mutex> guard(mutex_);
    releaseCodec();
    outputChannels_=0;
    stats_={};
    queue_.open(2);
}

Stats Receiver::stats() const noexcept {
    std::lock_guard<std::mutex> guard(mutex_);
    auto result=stats_;
    result.queueFrames=queue_.frames();
    result.queuePeak=queue_.peak();
    result.droppedFrames=queue_.dropped();
    result.underruns=queue_.underruns();
    return result;
}

}
