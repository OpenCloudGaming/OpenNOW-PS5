#include "stream/audio_receiver.hpp"
#include <opus/opus.h>
#include <opus/opus_multistream.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

using namespace opennow::audio;
using Packet=std::vector<std::uint8_t>;
using Block=std::array<std::int16_t,256*8>;

static Format format(unsigned channels,int payload=109,int red=117) {
    Format f;
    f.channels=channels;f.payload=payload;f.redPayload=red;
    if(channels==2){f.streams=1;f.coupled=1;f.mapping={0,1};}
    if(channels==6){f.streams=4;f.coupled=2;f.mapping={0,4,1,2,3,5};}
    if(channels==8){f.streams=5;f.coupled=3;f.mapping={0,6,1,2,3,4,5,7};}
    return f;
}

class Encoder {
public:
    explicit Encoder(unsigned channels,int bitrate=0):channels_(channels) {
        int error=OPUS_OK;
        if(channels==2){
            stereo_=opus_encoder_create(48000,2,OPUS_APPLICATION_AUDIO,&error);
            assert(stereo_&&error==OPUS_OK);
            assert(opus_encoder_ctl(stereo_,OPUS_SET_BITRATE(64000))==OPUS_OK);
            assert(opus_encoder_ctl(stereo_,OPUS_SET_FORCE_CHANNELS(2))==OPUS_OK);
            assert(opus_encoder_ctl(stereo_,OPUS_SET_INBAND_FEC(1))==OPUS_OK);
            assert(opus_encoder_ctl(stereo_,OPUS_SET_PACKET_LOSS_PERC(20))==OPUS_OK);
        }else{
            const auto f=format(channels);
            multi_=opus_multistream_encoder_create(48000,channels,f.streams,f.coupled,
                                                  f.mapping.data(),OPUS_APPLICATION_AUDIO,&error);
            assert(multi_&&error==OPUS_OK);
            assert(opus_multistream_encoder_ctl(multi_,OPUS_SET_BITRATE(bitrate?bitrate:channels*64000))==OPUS_OK);
        }
    }
    ~Encoder(){if(stereo_)opus_encoder_destroy(stereo_);if(multi_)opus_multistream_encoder_destroy(multi_);}
    Packet packet(unsigned frames=960) {
        std::vector<std::int16_t> pcm(frames*channels_);
        for(unsigned i=0;i<frames;++i)
            for(unsigned c=0;c<channels_;++c)
                pcm[i*channels_+c]=static_cast<std::int16_t>(9000*std::sin((position_+i)*(c+1)*0.013));
        position_+=frames;
        Packet result(16384);
        const int size=stereo_?opus_encode(stereo_,pcm.data(),frames,result.data(),result.size()):
            opus_multistream_encode(multi_,pcm.data(),frames,result.data(),result.size());
        assert(size>0);
        result.resize(size);
        return result;
    }
private:
    unsigned channels_,position_=0;
    OpusEncoder* stereo_=nullptr;
    OpusMSEncoder* multi_=nullptr;
};

static void receive(Receiver& receiver,const Packet& packet,std::uint16_t seq,std::uint32_t timestamp,
                    std::uint8_t payload=109) {
    receiver.receive(packet.data(),packet.size(),seq,payload,timestamp);
}

static Packet red(const Packet& previous,const Packet& current,unsigned offset=960,unsigned payload=109) {
    assert(previous.size()<1024);
    Packet result={static_cast<std::uint8_t>(0x80|payload),static_cast<std::uint8_t>(offset>>6),
        static_cast<std::uint8_t>(((offset&63)<<2)|(previous.size()>>8)),
        static_cast<std::uint8_t>(previous.size()),static_cast<std::uint8_t>(payload)};
    result.insert(result.end(),previous.begin(),previous.end());
    result.insert(result.end(),current.begin(),current.end());
    return result;
}

static void silent(Receiver& receiver) {
    Block block;block.fill(123);
    receiver.pop(block.data());
    assert(std::all_of(block.begin(),block.end(),[](auto sample){return sample==0;}));
}

static void routing(unsigned channels,unsigned outputChannels) {
    Receiver receiver;
    const auto f=format(channels);
    assert(receiver.open(outputChannels)&&receiver.configure(f));
    Encoder encoder(channels);
    const auto first=encoder.packet(),second=encoder.packet();
    receive(receiver,first,10,0);receive(receiver,second,11,960);
    assert(receiver.stats().samples==1920&&receiver.stats().channels==channels);
    int error=OPUS_OK;
    auto* reference=opus_multistream_decoder_create(48000,channels,f.streams,f.coupled,f.mapping.data(),&error);
    assert(reference&&error==OPUS_OK);
    std::array<std::int16_t,5760*8> decoded{};
    assert(opus_multistream_decode(reference,first.data(),first.size(),decoded.data(),5760,0)==960);
    Block block;block.fill(123);
    receiver.pop(block.data());
    constexpr unsigned fiveOne[]={0,2,1,5,3,4};
    constexpr unsigned sevenOne[]={0,2,1,7,5,6,3,4};
    for(unsigned frame=0;frame<256;++frame){
        for(unsigned channel=0;channel<outputChannels;++channel){
            const unsigned source=channels==2?channel:channels==6?fiveOne[channel%6]:sevenOne[channel];
            const auto expected=channel<channels?decoded[frame*channels+source]:0;
            assert(block[frame*outputChannels+channel]==expected);
        }
    }
    assert(std::any_of(block.begin(),block.end(),[](auto sample){return sample!=0;}));
    assert(std::all_of(block.begin()+256*outputChannels,block.end(),[](auto sample){return sample==0;}));
    opus_multistream_decoder_destroy(reference);
}

static void recovery() {
    Encoder encoder(2);
    const auto first=encoder.packet(),lost=encoder.packet(),next=encoder.packet();
    Receiver receiver;
    assert(receiver.open(8)&&receiver.configure(format(2)));
    receive(receiver,first,1,0);
    receive(receiver,red(lost,next),3,1920,117);
    auto stats=receiver.stats();
    assert(stats.recovered==1&&stats.concealed==0&&stats.fecAttempts==0&&stats.samples==1920);
    assert(stats.queueFrames==1920&&stats.droppedFrames==960);

    Receiver reference;
    assert(reference.open(8)&&reference.configure(format(2)));
    receive(reference,first,1,0);receive(reference,lost,2,960);receive(reference,next,3,1920);
    Block actual{},expected{};
    receiver.pop(actual.data());reference.pop(expected.data());
    assert(actual==expected);

    assert(receiver.configure(format(2)));
    receive(receiver,first,1,0);receive(receiver,next,3,1920);
    stats=receiver.stats();
    assert(stats.fecAttempts==1&&stats.concealed==1);
    const auto fec=stats.fecAttempts,concealed=stats.concealed;
    receive(receiver,next,6,4800);
    stats=receiver.stats();
    assert(stats.fecAttempts==fec&&stats.concealed==concealed+1);
    assert(stats.queueFrames<=5760);

    assert(receiver.configure(format(2)));
    receive(receiver,first,1,0);receive(receiver,red(lost,next,480),3,1920,117);
    assert(receiver.stats().fecAttempts==fec+1);

    assert(receiver.configure(format(2)));
    receive(receiver,first,1,0);receive(receiver,next,2,100000);
    assert(receiver.stats().queueFrames==960);
    silent(receiver);
    receive(receiver,next,3,100961);
    assert(receiver.stats().queueFrames==960);
    receive(receiver,next,100,101921);
    assert(receiver.stats().queueFrames==960);

    for(unsigned gap:{120u,1560u,2880u}){
        assert(receiver.open(8)&&receiver.configure(format(2)));
        receive(receiver,first,1,0);receive(receiver,next,4,960+gap);
        assert(receiver.stats().concealed==1&&receiver.stats().fecAttempts==0);
        assert(receiver.stats().samples==1920);
    }
    assert(receiver.open(8)&&receiver.configure(format(2)));
    receive(receiver,first,1,0);receive(receiver,next,5,3960);
    assert(receiver.stats().concealed==0&&receiver.stats().queueFrames==960);

    for(unsigned channels:{6u,8u}){
        Encoder surround(channels,channels*24000);
        const auto a=surround.packet(),b=surround.packet(),c=surround.packet();
        assert(receiver.open(8)&&receiver.configure(format(channels)));
        receive(receiver,a,1,0);receive(receiver,c,3,1920);
        assert(receiver.stats().fecAttempts==1&&receiver.stats().concealed==1);
        assert(receiver.stats().samples==1920&&receiver.stats().channels==channels);
        assert(receiver.open(8)&&receiver.configure(format(channels)));
        receive(receiver,a,1,0);receive(receiver,red(b,c),3,1920,117);
        assert(receiver.stats().recovered==1&&receiver.stats().samples==1920);
    }
}

static void ordering() {
    Encoder encoder(2);const auto packet=encoder.packet();
    Receiver receiver;
    assert(receiver.open(2)&&receiver.configure(format(2)));
    receive(receiver,packet,65535,0xfffffe00u);
    receive(receiver,packet,0,0x1c0u);
    const auto stats=receiver.stats();
    assert(stats.samples==1920&&stats.fecAttempts==0&&stats.queueFrames==1920);
    receive(receiver,packet,0,0x1c0u);receive(receiver,packet,65535,0xfffffe00u);
    receive(receiver,packet,1,0x1c0u);receive(receiver,packet,1,0x100u);
    assert(receiver.stats().samples==stats.samples&&receiver.stats().queueFrames==stats.queueFrames);
    receive(receiver,packet,1,0x580u);
    assert(receiver.stats().samples==2880);
}

static void invalidAndLifecycle() {
    Encoder encoder(2);const auto packet=encoder.packet();
    Receiver receiver;
    silent(receiver);
    receive(receiver,packet,1,0);
    assert(receiver.stats().errors==1);
    assert(!receiver.configure(format(2)));
    assert(!receiver.open(6));silent(receiver);
    assert(receiver.open(2));
    assert(!receiver.configure(format(6)));
    auto invalid=format(2);invalid.payload=-1;
    assert(!receiver.configure(invalid));
    assert(receiver.configure(format(2)));
    const Packet malformed={0xff},oversized(16385,0),badRed={0x80|109,0,0,3,109,0};
    receiver.receive(nullptr,10,1,109,0);
    receiver.receive(packet.data(),0,1,109,0);
    receive(receiver,oversized,1,0);receive(receiver,oversized,1,0,117);
    receive(receiver,malformed,1,0);receive(receiver,badRed,1,0,117);
    receive(receiver,packet,1,0,111);
    assert(receiver.stats().errors==7&&receiver.stats().samples==0);
    receive(receiver,packet,1,0);receive(receiver,packet,2,960);
    assert(receiver.stats().queueFrames==1920);
    assert(!receiver.configure(invalid));silent(receiver);
    assert(receiver.stats().channels==0&&receiver.stats().queueFrames==0);
    assert(receiver.configure(format(2)));
    receive(receiver,packet,1,0);receive(receiver,packet,2,960);
    receiver.close();receiver.close();silent(receiver);
    assert(receiver.stats().channels==0&&receiver.stats().queueFrames==0);
    assert(receiver.open(8));silent(receiver);
    assert(receiver.configure(format(2)));silent(receiver);
    receive(receiver,packet,1,0);receive(receiver,packet,2,960);
    assert(receiver.open(2));silent(receiver);
    assert(receiver.stats().samples==0&&receiver.stats().channels==0);

    assert(receiver.open(8)&&receiver.configure(format(6)));
    Encoder surround(6);const auto surroundPacket=surround.packet();
    receive(receiver,surroundPacket,1,0);receive(receiver,surroundPacket,2,960);
    assert(receiver.configure(format(2)));silent(receiver);
    receive(receiver,packet,1,0);receive(receiver,packet,2,960);
    Block block{};receiver.pop(block.data());
    for(unsigned i=0;i<256;++i)for(unsigned c=2;c<8;++c)assert(block[i*8+c]==0);
}

static void fallback() {
    Encoder encoder(2);const auto packet=encoder.packet();
    Receiver receiver;
    assert(receiver.open(8)&&receiver.configure(format(8)));
    for(unsigned i=0;i<7;++i)receive(receiver,packet,i,i*960);
    assert(receiver.stats().channels==8&&receiver.stats().samples==0);
    receive(receiver,packet,7,6720);
    assert(receiver.stats().channels==2&&receiver.stats().samples==960&&receiver.stats().stereoFallbacks==1);

    assert(receiver.open(8)&&receiver.configure(format(6)));
    for(unsigned i=0;i<7;++i)receive(receiver,packet,i,i*960);
    for(unsigned i=0;i<12;++i)receive(receiver,packet,6,5760);
    assert(receiver.stats().channels==6&&receiver.stats().stereoFallbacks==0);
    for(unsigned i=7;i<14;++i)receive(receiver,packet,i,i*960);
    assert(receiver.stats().channels==6);
    receive(receiver,packet,14,14*960);
    assert(receiver.stats().channels==2);

    for(unsigned interruption=0;interruption<5;++interruption){
        assert(receiver.open(8)&&receiver.configure(format(6)));
        for(unsigned i=0;i<7;++i)receive(receiver,packet,i,i*960);
        if(interruption==0)receive(receiver,Packet{0xff},7,6720);
        if(interruption==1)receive(receiver,packet,5,4800);
        if(interruption==2)receive(receiver,packet,7,6720,110);
        if(interruption==4)receive(receiver,packet,7,5760);
        const unsigned start=interruption==3?9:8;
        for(unsigned i=start;i<start+7;++i)receive(receiver,packet,i,i*960);
        assert(receiver.stats().channels==6);
        receive(receiver,packet,start+7,(start+7)*960);
        assert(receiver.stats().channels==2);
    }

    assert(receiver.open(8)&&receiver.configure(format(6)));
    Encoder surround(6);const auto surroundPacket=surround.packet();
    receive(receiver,surroundPacket,0,0);
    assert(receiver.stats().samples==960);
    for(unsigned i=1;i<20;++i)receive(receiver,packet,i,i*960);
    assert(receiver.stats().channels==6&&receiver.stats().stereoFallbacks==0);
    receive(receiver,packet,100,100000);
    for(unsigned i=101;i<112;++i)receive(receiver,packet,i,100000+(i-100)*960);
    assert(receiver.stats().channels==6&&receiver.stats().stereoFallbacks==0);
}

static void queueAndStress() {
    Encoder encoder(2);const auto packet=encoder.packet();
    Receiver receiver;
    assert(receiver.open(8)&&receiver.configure(format(2)));
    for(unsigned i=0;i<100;++i)receive(receiver,packet,i,i*960);
    assert(receiver.stats().queueFrames==1920&&receiver.stats().queuePeak==1920);
    assert(receiver.stats().droppedFrames==96000-1920);
    Block block{};
    for(unsigned i=0;i<8;++i)receiver.pop(block.data());
    assert(receiver.stats().underruns==1&&receiver.stats().queueFrames==0);
    receive(receiver,packet,100,96000);silent(receiver);
    receive(receiver,packet,101,96960);silent(receiver);
    receive(receiver,packet,102,97920);receiver.pop(block.data());
    assert(std::any_of(block.begin(),block.end(),[](auto sample){return sample!=0;}));
    const auto longPacket=encoder.packet(5760);
    receive(receiver,longPacket,103,98880);
    assert(receiver.stats().queueFrames==5760&&receiver.stats().queuePeak==5760);
    auto f=format(2);
    std::thread producer([&]{for(unsigned i=0;i<300;++i)receive(receiver,packet,i,i*960);});
    std::thread consumer([&]{Block output{};for(unsigned i=0;i<300;++i){receiver.pop(output.data());assert(receiver.stats().queueFrames<=5760);}});
    std::thread lifecycle([&]{for(unsigned i=0;i<50;++i){receiver.close();assert(receiver.open(8));assert(receiver.configure(f));}});
    producer.join();consumer.join();lifecycle.join();
    receiver.close();silent(receiver);
}

int main() {
    routing(2,2);routing(2,8);routing(6,8);routing(8,8);
    recovery();ordering();invalidAndLifecycle();fallback();queueAndStress();
}
