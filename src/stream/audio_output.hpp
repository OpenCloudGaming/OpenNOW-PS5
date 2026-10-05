// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <mutex>

extern "C" {
int sceAudioOutInit();
int sceAudioOutOpen(int,int,int,unsigned,unsigned,unsigned);
int sceAudioOutOutput(int,const void*);
int sceAudioOutClose(int);
}

namespace opennow::audio {
class Output {
public:
    Output() noexcept=default;
    ~Output(){close();}
    Output(const Output&)=delete;
    Output& operator=(const Output&)=delete;

    bool open(unsigned requestedChannels,bool stereoFallback=true) noexcept {
        if(requestedChannels!=2&&requestedChannels!=6&&requestedChannels!=8)return false;
        if(!close())return false;
        const int result=sceAudioOutInit();
        if(result!=0&&static_cast<unsigned>(result)!=0x8026000eU)return false;
        unsigned channels=requestedChannels==2?2:8;
        int handle=sceAudioOutOpen(0xff,0,0,256,48000,channels==2?1:2);
        if(handle<0&&channels==8&&stereoFallback) {
            handle=sceAudioOutOpen(0xff,0,0,256,48000,1);
            channels=2;
        }
        if(handle<0)return false;
        handle_=handle;
        channels_=channels;
        return true;
    }

    bool close() noexcept {
        if(!active())return true;
        sceAudioOutOutput(handle_,nullptr);
        if(sceAudioOutClose(handle_)!=0)return false;
        handle_=-1;
        channels_=2;
        return true;
    }

    int write(const std::int16_t* samples) noexcept {
        return active()?sceAudioOutOutput(handle_,samples):-1;
    }

    unsigned channels() const noexcept {return channels_;}
    bool active() const noexcept {return handle_>=0;}

    static unsigned probeCapacity() noexcept {
        static std::mutex mutex;
        static Output output;
        const std::lock_guard<std::mutex> lock(mutex);
        if(!output.open(8,false))return 2;
        return output.close()?8:2;
    }

private:
    int handle_=-1;
    unsigned channels_=2;
};
}
