// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/artwork.hpp"
#include "ui/artwork_disk.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <string>
#include <vector>
#include <unistd.h>

using namespace opennow;
namespace {
const unsigned char jpeg[]={
    0xff,0xd8,0xff,0xe0,0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00,
    0xff,0xdb,0x00,0x43,0x00,0x03,0x02,0x02,0x03,0x02,0x02,0x03,0x03,0x03,0x03,0x04,0x03,0x03,0x04,0x05,
    0x08,0x05,0x05,0x04,0x04,0x05,0x0a,0x07,0x07,0x06,0x08,0x0c,0x0a,0x0c,0x0c,0x0b,0x0a,0x0b,0x0b,0x0d,
    0x0e,0x12,0x10,0x0d,0x0e,0x11,0x0e,0x0b,0x0b,0x10,0x16,0x10,0x11,0x13,0x14,0x15,0x15,0x15,0x0c,0x0f,
    0x17,0x18,0x16,0x14,0x18,0x12,0x14,0x15,0x14,0xff,0xdb,0x00,0x43,0x01,0x03,0x04,0x04,0x05,0x04,0x05,
    0x09,0x05,0x05,0x09,0x14,0x0d,0x0b,0x0d,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,
    0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,
    0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0xff,0xc0,
    0x00,0x11,0x08,0x00,0x30,0x00,0x20,0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01,0xff,0xc4,0x00,
    0x1f,0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
    0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x10,0x00,0x02,0x01,0x03,0x03,
    0x02,0x04,0x03,0x05,0x05,0x04,0x04,0x00,0x00,0x01,0x7d,0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,
    0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,
    0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,
    0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,
    0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,
    0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,
    0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,
    0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,
    0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xff,0xc4,0x00,0x1f,0x01,0x00,0x03,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,
    0x06,0x07,0x08,0x09,0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x11,0x00,0x02,0x01,0x02,0x04,0x04,0x03,0x04,0x07,
    0x05,0x04,0x04,0x00,0x01,0x02,0x77,0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,
    0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,
    0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,
    0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,
    0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,
    0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,
    0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,
    0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,
    0x00,0x3f,0x00,0xfa,0xee,0x8a,0x28,0xaf,0xe1,0x23,0xf6,0xd3,0xe3,0x4a,0x28,0xa2,0xbf,0x72,0x3f,0xca,
    0xa3,0xec,0xba,0x2b,0xf3,0x9e,0x8a,0xf9,0xbf,0xf5,0x2b,0xfe,0xa2,0x3f,0xf2,0x4f,0xfe,0xd8,0xff,0x00,
    0x6e,0x7f,0xe2,0x1d,0xff,0x00,0xd4,0x5f,0xfe,0x49,0xff,0x00,0xdb,0x9e,0x9b,0x45,0x7e,0x72,0x51,0x5f,
    0xb9,0xff,0x00,0xa9,0x5f,0xf5,0x11,0xff,0x00,0x92,0x7f,0xf6,0xc7,0xf9,0x55,0xff,0x00,0x10,0xef,0xfe,
    0xa2,0xff,0x00,0xf2,0x4f,0xfe,0xdc,0xfb,0x3a,0x8a,0x28,0xaf,0x9b,0x3f,0xdb,0x93,0xe3,0x1a,0x28,0xa2,
    0xbf,0x73,0x3f,0xca,0xa3,0xff,0xd9,
};
struct Stub {
    std::vector<std::string> urls;
    bool fail=false;
    std::atomic_bool waitForStop{false};
    std::atomic_uint calls{0};
    std::atomic_bool entered{false};
};
struct Temp {
    char parent[64]="/tmp/opennow-artwork-cache-XXXXXX";
    std::string root;
    Temp(){assert(mkdtemp(parent));root=std::string(parent)+"/artwork";}
    ~Temp(){art::DiskCache disk(root.c_str());disk.clear();assert(rmdir(root.c_str())==0);assert(rmdir(parent)==0);}
};
bool fetch(void* context,const char* url,unsigned char* buffer,std::size_t capacity,std::size_t& size,const std::atomic_bool& stop) noexcept {
    auto& stub=*static_cast<Stub*>(context);
    stub.urls.emplace_back(url);
    stub.calls.fetch_add(1);
    stub.entered.store(true);
    if(stub.waitForStop){while(!stop.load())usleep(1000);return false;}
    if(stub.fail||sizeof(jpeg)>capacity)return false;
    std::memcpy(buffer,jpeg,sizeof(jpeg));
    size=sizeof(jpeg);
    return true;
}
}

int main() {
    const char* box="https://img.nvidiagrid.net/apps/101611411/ZZ/GAME_BOX_ART_01_fixture.jpg";
    char url[256];
    assert(art::requestUrl(box,art::Kind::tile,url,sizeof(url)));
    assert(!std::strcmp(url,"https://img.nvidiagrid.net/apps/101611411/ZZ/GAME_BOX_ART_01_fixture.jpg;f=jpg;w=272"));
    assert(art::requestUrl(box,art::Kind::hero,url,sizeof(url))&&std::strstr(url,";f=jpg;w=960"));
    for(const char* bad:{"https://evil.test/apps/1.jpg","https://img.nvidiagrid.net/apps/1/a.jpg;f=webp","https://img.nvidiagrid.net/apps/../x","http://img.nvidiagrid.net/apps/1.jpg",""})
        assert(!art::requestUrl(bad,art::Kind::tile,url,sizeof(url)));
    char tiny[16];
    assert(!art::requestUrl(box,art::Kind::tile,tiny,sizeof(tiny)));

    std::vector<std::uint32_t> decoded(art::tileWidth*art::tileHeight);
    assert(art::decode(jpeg,sizeof(jpeg),art::tileWidth,art::tileHeight,decoded.data()));
    const std::uint32_t topLeft=decoded[10*art::tileWidth+10], bottomRight=decoded[(art::tileHeight-10)*art::tileWidth+art::tileWidth-10];
    assert((topLeft>>24)==0xff&&(topLeft&0xff)>200&&((topLeft>>8)&0xff)>150);
    assert((bottomRight&0xff)<60&&((bottomRight>>8)&0xff)<60);
    assert(!art::decode(jpeg,10,art::tileWidth,art::tileHeight,decoded.data()));
    assert(!art::decode(jpeg,art::maxCompressedBytes+1,art::tileWidth,art::tileHeight,decoded.data()));
    assert(!art::decode(nullptr,sizeof(jpeg),art::tileWidth,art::tileHeight,decoded.data()));
    std::vector<unsigned char> broken(jpeg,jpeg+sizeof(jpeg));
    for(std::size_t i=200;i<broken.size();++i)broken[i]=0;
    std::vector<unsigned char> huge(jpeg,jpeg+sizeof(jpeg));
    bool patched=false;
    for(std::size_t i=0;i+8<huge.size();++i)if(huge[i]==0xff&&(huge[i+1]==0xc0||huge[i+1]==0xc2)) {
        huge[i+5]=0x0f;huge[i+6]=0xa0;huge[i+7]=0x0f;huge[i+8]=0xa0;patched=true;break;
    }
    assert(patched&&!art::decode(huge.data(),huge.size(),art::tileWidth,art::tileHeight,decoded.data()));
    const char text[]="not an image";
    assert(!art::decode(reinterpret_cast<const unsigned char*>(text),sizeof(text),art::tileWidth,art::tileHeight,decoded.data()));
    (void)art::decode(broken.data(),broken.size(),art::tileWidth,art::tileHeight,decoded.data());

    Stub stub;
    {
        art::Cache cache(fetch,&stub);
        assert(cache.ready());
        assert(cache.want("https://evil.test/a.jpg",art::Kind::tile,1)==art::State::failed);
        assert(cache.want("",art::Kind::tile,1)==art::State::failed);
        assert(cache.want(box,art::Kind::tile,1)==art::State::loading);
        assert(cache.want(box,art::Kind::tile,2)==art::State::loading);
        const unsigned before=cache.generation();
        assert(cache.step());
        assert(cache.generation()==before+1);
        assert(stub.urls.size()==1&&stub.urls[0]=="https://img.nvidiagrid.net/apps/101611411/ZZ/GAME_BOX_ART_01_fixture.jpg;f=jpg;w=272");
        assert(cache.want(box,art::Kind::tile,3)==art::State::ready);
        assert(!cache.step());
        stub.fail=true;
        const char* other="https://img.nvidiagrid.net/apps/2/ZZ/GAME_BOX_ART_01_missing.jpg";
        assert(cache.want(other,art::Kind::tile,4)==art::State::loading);
        assert(cache.step());
        assert(cache.want(other,art::Kind::tile,5)==art::State::failed);
        assert(cache.want(box,art::Kind::tile,5)==art::State::ready);
        stub.fail=false;
        for(unsigned i=0;i<art::tileSlots+4;++i) {
            char name[160];std::snprintf(name,sizeof(name),"https://img.nvidiagrid.net/apps/%u/ZZ/GAME_BOX_ART_01_x.jpg",1000+i);
            cache.want(name,art::Kind::tile,10+i);
        }
        unsigned work=0;
        while(cache.step())++work;
        assert(work<=art::tileSlots);
        assert(cache.want(box,art::Kind::tile,100)==art::State::loading);
        const char* hero="https://img.nvidiagrid.net/apps/101611411/ZZ/HERO_IMAGE_01_fixture.jpg";
        assert(cache.want(hero,art::Kind::hero,101)==art::State::loading);
        while(cache.step()){}
        assert(cache.want(hero,art::Kind::hero,102)==art::State::ready);
        assert(stub.urls.back().find(";f=jpg;w=960")!=std::string::npos||stub.urls.back().find(";f=jpg;w=272")!=std::string::npos);
        cache.requestStop();
        assert(cache.want("https://img.nvidiagrid.net/apps/9/ZZ/GAME_BOX_ART_01_after.jpg",art::Kind::tile,200)==art::State::loading);
        assert(!cache.step());
    }
    {
        Stub blocking;
        blocking.waitForStop.store(true);
        art::Cache cache(fetch,&blocking);
        assert(cache.want(box,art::Kind::tile,1)==art::State::loading);
        pthread_t thread;
        assert(pthread_create(&thread,nullptr,&art::Cache::run,&cache)==0);
        while(!blocking.entered.load())usleep(1000);
        for(unsigned frame=2;frame<50;++frame)assert(cache.want(box,art::Kind::tile,frame)!=art::State::ready);
        cache.requestStop();
        assert(pthread_join(thread,nullptr)==0);
        assert(cache.want(box,art::Kind::tile,60)==art::State::failed);
    }
    {
        Stub live;
        art::Cache cache(fetch,&live);
        pthread_t thread;
        assert(pthread_create(&thread,nullptr,&art::Cache::run,&cache)==0);
        for(unsigned frame=1;frame<400;++frame) {
            char name[160];std::snprintf(name,sizeof(name),"https://img.nvidiagrid.net/apps/%u/ZZ/GAME_BOX_ART_01_y.jpg",frame%50);
            cache.want(name,art::Kind::tile,frame);
            usleep(200);
        }
        cache.requestStop();
        assert(pthread_join(thread,nullptr)==0);
    }
    {
        Stub paused;
        art::Cache cache(fetch,&paused);
        cache.setPaused(true);
        assert(cache.paused());
        assert(cache.want(box,art::Kind::tile,1)==art::State::loading);
        assert(!cache.step()&&paused.urls.empty());
        cache.setPaused(false);
        assert(cache.step()&&paused.urls.size()==1);
        assert(cache.want(box,art::Kind::tile,2)==art::State::ready);
    }
    {
        Stub inflight;
        inflight.waitForStop.store(true);
        art::Cache cache(fetch,&inflight);
        assert(cache.want(box,art::Kind::tile,1)==art::State::loading);
        pthread_t thread;
        assert(pthread_create(&thread,nullptr,&art::Cache::run,&cache)==0);
        while(!inflight.entered.load())usleep(1000);
        cache.setPaused(true);
        for(unsigned i=0;i<200&&inflight.calls.load()<2;++i) {
            usleep(2000);
            if(cache.want(box,art::Kind::tile,2)!=art::State::loading)break;
        }
        assert(cache.want(box,art::Kind::tile,3)==art::State::loading);
        assert(inflight.calls.load()==1);
        inflight.waitForStop.store(false);
        cache.setPaused(false);
        for(unsigned i=0;i<500&&cache.want(box,art::Kind::tile,4+i)!=art::State::ready;++i)usleep(2000);
        assert(cache.want(box,art::Kind::tile,1000)==art::State::ready&&inflight.calls.load()==2);
        cache.setPaused(true);
        cache.requestStop();
        assert(pthread_join(thread,nullptr)==0);
        assert(cache.stopping()&&!cache.step());
    }
    {
        struct Gate {
            std::atomic_bool entered{false}, sawCancel{false}, release{false};
            std::atomic_uint calls{0};
        } gate;
        const auto gated=[](void* context,const char*,unsigned char* buffer,std::size_t capacity,std::size_t& size,const std::atomic_bool& stop) noexcept {
            auto& g=*static_cast<Gate*>(context);
            const unsigned call=g.calls.fetch_add(1);
            if(call==0) {
                g.entered.store(true);
                while(!stop.load())usleep(500);
                g.sawCancel.store(true);
                while(!g.release.load())usleep(500);
                return false;
            }
            if(sizeof(jpeg)>capacity)return false;
            std::memcpy(buffer,jpeg,sizeof(jpeg));
            size=sizeof(jpeg);
            return true;
        };
        art::Cache cache(gated,&gate);
        assert(cache.want(box,art::Kind::tile,1)==art::State::loading);
        pthread_t thread;
        assert(pthread_create(&thread,nullptr,&art::Cache::run,&cache)==0);
        while(!gate.entered.load())usleep(500);
        cache.setPaused(true);
        while(!gate.sawCancel.load())usleep(500);
        cache.setPaused(false);
        gate.release.store(true);
        for(unsigned i=0;i<1000&&cache.want(box,art::Kind::tile,2+i)!=art::State::ready;++i) {
            assert(cache.want(box,art::Kind::tile,2+i)!=art::State::failed);
            usleep(1000);
        }
        assert(cache.want(box,art::Kind::tile,2000)==art::State::ready&&gate.calls.load()==2);
        cache.setPaused(true);
        cache.requestStop();
        cache.setPaused(false);
        assert(pthread_join(thread,nullptr)==0);
        assert(cache.stopping()&&!cache.step());
    }
    {
        Temp temp;Stub network;
        {
            art::Cache cache(fetch,&network,temp.root.c_str());
            assert(access(temp.root.c_str(),F_OK)!=0);
            const auto initial=cache.diskStats();assert(initial.enabled&&!initial.available&&initial.busy);
            cache.want(box,art::Kind::tile,1);assert(cache.step());
            assert(cache.want(box,art::Kind::tile,2)==art::State::ready&&network.calls==1);
            const auto stats=cache.diskStats();
            assert(stats.enabled&&stats.available&&!stats.busy&&!stats.error&&stats.count==1);
            assert(stats.bytes==sizeof(jpeg)+sizeof(art::DiskCache::Header));
        }
        network.fail=true;
        {
            art::Cache cache(fetch,&network,temp.root.c_str());
            cache.want(box,art::Kind::tile,1);assert(cache.step());
            assert(cache.want(box,art::Kind::tile,2)==art::State::ready&&network.calls==1);
            cache.want(box,art::Kind::hero,3);assert(cache.step());
            assert(cache.want(box,art::Kind::hero,4)==art::State::failed&&network.calls==2);
        }
        {
            art::Cache cache(fetch,&network,temp.root.c_str());cache.setDiskEnabled(false);
            cache.want(box,art::Kind::tile,1);assert(cache.step());
            assert(cache.want(box,art::Kind::tile,2)==art::State::failed&&network.calls==3);
            assert(!cache.diskStats().enabled&&!cache.diskStats().busy);
            cache.setDiskEnabled(true);assert(cache.step());
            assert(cache.want(box,art::Kind::tile,3)==art::State::ready&&network.calls==3);
        }
        {
            art::Cache cache(fetch,&network,temp.root.c_str());
            cache.setDiskEnabled(false);cache.setDiskEnabled(true);
            cache.want(box,art::Kind::tile,1);assert(cache.step());
            assert(cache.want(box,art::Kind::tile,2)==art::State::ready&&network.calls==3);
            cache.setDiskEnabled(false);cache.setPaused(true);cache.requestDiskClear();
            assert(cache.diskStats().busy);assert(cache.step());
            const auto cleared=cache.diskStats();
            assert(!cleared.enabled&&!cleared.busy&&cleared.count==0&&cleared.bytes==0);
            assert(cache.want(box,art::Kind::tile,3)==art::State::loading);
            cache.setDiskEnabled(true);cache.setPaused(false);network.fail=false;
            assert(cache.step());assert(cache.want(box,art::Kind::tile,4)==art::State::ready&&network.calls==4);
        }
        const auto slot=temp.root+"/slot-000.bin";
        assert(truncate(slot.c_str(),sizeof(art::DiskCache::Header)+20)==0);
        {
            art::Cache cache(fetch,&network,temp.root.c_str());cache.want(box,art::Kind::tile,1);
            assert(cache.step()&&cache.want(box,art::Kind::tile,2)==art::State::ready&&network.calls==5);
            assert(cache.diskStats().error&&cache.diskStats().count==1);
        }
        {
            art::DiskCache disk(temp.root.c_str());
            std::vector<unsigned char> buffer(art::DiskCache::slotBytes);
            disk.initialize(buffer.data(),buffer.size());
            const unsigned char invalid[]={1,2,3,4};
            assert(art::requestUrl(box,art::Kind::tile,url,sizeof(url)));
            disk.put(url,0,invalid,sizeof(invalid));
            art::Cache cache(fetch,&network,temp.root.c_str());cache.want(box,art::Kind::tile,1);
            assert(cache.step()&&cache.want(box,art::Kind::tile,2)==art::State::ready&&network.calls==6);
            assert(cache.diskStats().count==1&&cache.diskStats().error);
        }
    }
    {
        Temp temp;
        const auto oversized=[](void*,const char*,unsigned char* buffer,std::size_t capacity,std::size_t& size,const std::atomic_bool&) noexcept {
            size=art::DiskCache::payloadBytes+1;assert(size<=capacity);
            std::memset(buffer,0,size);std::memcpy(buffer,jpeg,sizeof(jpeg));return true;
        };
        art::Cache cache(oversized,nullptr,temp.root.c_str());cache.want(box,art::Kind::tile,1);
        assert(cache.step()&&cache.want(box,art::Kind::tile,2)==art::State::ready);
        assert(cache.diskStats().available&&cache.diskStats().count==0);
    }
    for(const char* root:{"/opennow-test-missing-parent-019811/artwork","/sys/opennow-artwork-test"}) {
        Stub network;art::Cache cache(fetch,&network,root);cache.want(box,art::Kind::tile,1);
        assert(cache.step()&&cache.want(box,art::Kind::tile,2)==art::State::ready);
        const auto stats=cache.diskStats();assert(stats.enabled&&!stats.available&&stats.error&&!stats.busy);
    }
    {
        Temp temp;
        struct Gate {std::atomic_bool entered{false},cancelled{false},release{false};std::atomic_uint calls{0};} gate;
        const auto gated=[](void* context,const char*,unsigned char* buffer,std::size_t,std::size_t& size,const std::atomic_bool& cancel) noexcept {
            auto& gate=*static_cast<Gate*>(context);gate.calls.fetch_add(1);gate.entered.store(true);
            while(!cancel.load())usleep(500);
            gate.cancelled.store(true);
            while(!gate.release.load())usleep(500);
            std::memcpy(buffer,jpeg,sizeof(jpeg));size=sizeof(jpeg);return true;
        };
        art::Cache cache(gated,&gate,temp.root.c_str());cache.want(box,art::Kind::tile,1);
        pthread_t worker;assert(pthread_create(&worker,nullptr,&art::Cache::run,&cache)==0);
        while(!gate.entered.load())usleep(500);
        cache.requestDiskClear();
        while(!gate.cancelled.load())usleep(500);
        assert(cache.diskStats().busy);cache.setPaused(true);gate.release.store(true);
        for(unsigned i=0;i<2000&&cache.diskStats().busy;++i)usleep(1000);
        assert(!cache.diskStats().busy&&cache.diskStats().count==0&&gate.calls==1);
        assert(cache.want(box,art::Kind::tile,2)==art::State::loading);
        cache.requestStop();assert(pthread_join(worker,nullptr)==0);
        Stub offline;offline.fail=true;art::Cache reopened(fetch,&offline,temp.root.c_str());
        reopened.want(box,art::Kind::tile,1);assert(reopened.step());
        assert(reopened.want(box,art::Kind::tile,2)==art::State::failed&&offline.calls==1);
    }
    {
        Temp temp;Stub network;art::Cache cache(fetch,&network,temp.root.c_str());
        pthread_t worker;assert(pthread_create(&worker,nullptr,&art::Cache::run,&cache)==0);
        for(unsigned frame=1;frame<400;++frame) {
            char name[160];std::snprintf(name,sizeof(name),"https://img.nvidiagrid.net/apps/%u/ZZ/GAME_BOX_ART_01_y.jpg",frame%50);
            cache.want(name,art::Kind::tile,frame);
            if(frame%29==0)cache.requestDiskClear();
            if(frame%11==0)cache.setDiskEnabled(frame%22==0);
            if(frame%13==0)cache.setPaused(frame%26==0);
            const auto stats=cache.diskStats();
            assert(stats.count<=128&&stats.bytes<=art::DiskCache::storedBudget);
            usleep(300);
        }
        cache.requestStop();assert(pthread_join(worker,nullptr)==0);
        assert(cache.stopping()&&!cache.step());
    }
    std::puts("Artwork URL, decode bounds, persistent cache, controls, lifetime and cancellation regressions passed");
}
