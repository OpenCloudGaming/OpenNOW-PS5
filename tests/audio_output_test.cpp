// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/audio_output.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <set>
#include <thread>
#include <type_traits>
#include <vector>

using opennow::audio::Output;
namespace {
enum class Call {init,open,write,cancel,close};
struct Event {
    Call call;
    int handle=-1;
    unsigned format=0;
    const void* samples=nullptr;
};
std::vector<Event> events;
std::deque<int> openResults;
std::set<int> handles;
int initResult=0,writeResult=0,cancelResult=0,closeResult=0,nextHandle=100;
bool verifyStaticCleanup=false;

void reset() {
    assert(handles.empty());
    assert(openResults.empty());
    events.clear();
    initResult=writeResult=cancelResult=closeResult=0;
}

void expect(std::initializer_list<Call> calls) {
    assert(events.size()==calls.size());
    std::size_t index=0;
    for(auto call:calls)assert(events[index++].call==call);
}

void assertStaticCleanup() {
    assert(verifyStaticCleanup&&handles.empty());
    expect({Call::cancel,Call::close});
    assert(events[0].handle==events[1].handle);
    std::puts("AudioOut lifecycle, fallback, ownership, probe retries and static cleanup passed");
}
}

extern "C" int sceAudioOutInit() {
    events.push_back({Call::init});
    return initResult;
}

extern "C" int sceAudioOutOpen(int user,int type,int index,unsigned frames,unsigned rate,unsigned format) {
    assert(user==0xff&&type==0&&index==0&&frames==256&&rate==48000);
    assert(format==1||format==2);
    const int handle=openResults.empty()?nextHandle++:openResults.front();
    if(!openResults.empty())openResults.pop_front();
    if(handle>=0)assert(handles.insert(handle).second);
    events.push_back({Call::open,handle,format});
    return handle;
}

extern "C" int sceAudioOutOutput(int handle,const void* samples) {
    assert(handles.count(handle)==1);
    events.push_back({samples?Call::write:Call::cancel,handle,0,samples});
    return samples?writeResult:cancelResult;
}

extern "C" int sceAudioOutClose(int handle) {
    assert(handles.count(handle)==1);
    events.push_back({Call::close,handle});
    if(closeResult==0)assert(handles.erase(handle)==1);
    return closeResult;
}

int main() {
    static_assert(!std::is_copy_constructible_v<Output>&&!std::is_copy_assignable_v<Output>);
    static_assert(!std::is_move_constructible_v<Output>&&!std::is_move_assignable_v<Output>);
    assert(std::atexit(assertStaticCleanup)==0);
    std::int16_t samples[256*8]{};
    {
        Output output;
        assert(!output.active()&&output.channels()==2);
        assert(output.write(samples)<0&&output.write(nullptr)<0);
        assert(output.close()&&output.close());
        for(unsigned channels:{0U,1U,3U,4U,5U,7U,9U,0xffffffffU})assert(!output.open(channels));
        expect({});
        for(int result:{-1,1,static_cast<int>(0x8026000fU)}) {
            reset();
            initResult=result;
            assert(!output.open(8)&&!output.active()&&output.channels()==2);
            expect({Call::init});
        }
    }
    reset();
    for(unsigned channels:{2U,6U,8U}) {
        for(int result:{0,static_cast<int>(0x8026000eU)}) {
            reset();
            initResult=result;
            Output output;
            assert(output.open(channels));
            assert(output.active()&&output.channels()==(channels==2?2:8));
            expect({Call::init,Call::open});
            assert(events[1].format==(channels==2?1:2));
            const int handle=events[1].handle;
            writeResult=-123;
            assert(output.write(samples)==-123);
            assert(events.back().samples==samples&&events.back().handle==handle);
            writeResult=0;
            assert(output.write(samples)==0);
            assert(output.close()&&!output.active()&&output.channels()==2);
            expect({Call::init,Call::open,Call::write,Call::write,Call::cancel,Call::close});
            assert(events[4].handle==handle&&events[5].handle==handle);
            assert(output.close());
            assert(output.write(samples)<0);
            assert(events.size()==6);
        }
    }
    for(unsigned channels:{6U,8U}) {
        reset();
        {
            Output output;
            openResults={-1,0};
            assert(output.open(channels)&&output.active()&&output.channels()==2);
            expect({Call::init,Call::open,Call::open});
            assert(events[1].format==2&&events[2].format==1&&events[2].handle==0);
            assert(output.write(nullptr)==0);
            assert(output.close());
            expect({Call::init,Call::open,Call::open,Call::cancel,Call::cancel,Call::close});
        }
        reset();
        {
            Output output;
            openResults={-1,-2};
            assert(!output.open(channels)&&!output.active()&&output.channels()==2);
            expect({Call::init,Call::open,Call::open});
        }
        reset();
        {
            Output output;
            openResults={-1};
            assert(!output.open(channels,false)&&!output.active());
            expect({Call::init,Call::open});
            assert(events[1].format==2);
        }
    }
    reset();
    {
        Output output;
        openResults={-1};
        assert(!output.open(2));
        expect({Call::init,Call::open});
        assert(events[1].format==1);
    }
    reset();
    {
        Output output;
        assert(output.open(8));
        const int handle=events.back().handle;
        events.clear();
        assert(!output.open(7)&&output.active()&&output.channels()==8);
        expect({});
        closeResult=-7;
        assert(!output.close()&&output.active()&&output.channels()==8);
        expect({Call::cancel,Call::close});
        events.clear();
        assert(!output.open(2)&&output.active()&&output.channels()==8);
        expect({Call::cancel,Call::close});
        assert(events[0].handle==handle&&events[1].handle==handle&&handles.size()==1);
        assert(output.write(samples)==0&&events.back().handle==handle);
        closeResult=0;
        events.clear();
        assert(output.open(2)&&output.active()&&output.channels()==2);
        expect({Call::cancel,Call::close,Call::init,Call::open});
        assert(events[0].handle==handle&&events[1].handle==handle);
        assert(events.back().handle!=handle&&handles.size()==1);
        cancelResult=-9;
        assert(output.close()&&!output.active());
    }
    reset();
    {
        Output output;
        assert(output.open(8));
        events.clear();
        initResult=-1;
        assert(!output.open(2)&&!output.active()&&output.channels()==2);
        expect({Call::cancel,Call::close,Call::init});
    }
    reset();
    {
        Output output;
        assert(output.open(6));
        events.clear();
    }
    expect({Call::cancel,Call::close});
    assert(handles.empty()&&events[0].handle==events[1].handle);

    reset();
    assert(Output::probeCapacity()==8);
    expect({Call::init,Call::open,Call::cancel,Call::close});
    assert(events[1].format==2&&handles.empty());
    reset();
    openResults={-1};
    assert(Output::probeCapacity()==2);
    expect({Call::init,Call::open});
    reset();
    initResult=-1;
    assert(Output::probeCapacity()==2);
    expect({Call::init});
    reset();
    closeResult=-1;
    assert(Output::probeCapacity()==2&&handles.size()==1);
    const int probeHandle=events[1].handle;
    expect({Call::init,Call::open,Call::cancel,Call::close});
    events.clear();
    assert(Output::probeCapacity()==2&&handles.size()==1);
    expect({Call::cancel,Call::close});
    assert(events[0].handle==probeHandle&&events[1].handle==probeHandle);
    closeResult=0;
    events.clear();
    assert(Output::probeCapacity()==8&&handles.empty());
    expect({Call::cancel,Call::close,Call::init,Call::open,Call::cancel,Call::close});
    assert(events[0].handle==probeHandle&&events[1].handle==probeHandle);
    reset();
    {
        std::vector<std::thread> threads;
        for(unsigned i=0;i<8;++i)threads.emplace_back([]{for(unsigned n=0;n<20;++n)assert(Output::probeCapacity()==8);});
        for(auto& thread:threads)thread.join();
        assert(handles.empty()&&events.size()==8*20*4);
        for(std::size_t i=0;i<events.size();i+=4) {
            assert(events[i].call==Call::init&&events[i+1].call==Call::open);
            assert(events[i+2].call==Call::cancel&&events[i+3].call==Call::close);
            assert(events[i+1].handle==events[i+2].handle&&events[i+2].handle==events[i+3].handle);
        }
    }
    reset();
    closeResult=-1;
    assert(Output::probeCapacity()==2&&handles.size()==1);
    closeResult=0;
    events.clear();
    verifyStaticCleanup=true;
}
