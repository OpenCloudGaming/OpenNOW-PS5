// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_file.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifdef __linux__
#include <sys/inotify.h>
#endif

using namespace opennow;
namespace {
using Bytes=std::vector<unsigned char>;

void writeRaw(const std::string& path,const Bytes& bytes) {
    FILE* file=std::fopen(path.c_str(),"wb");
    assert(file);
    if(!bytes.empty())assert(std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size());
    assert(std::fclose(file)==0);
}

Bytes readRaw(const std::string& path) {
    FILE* file=std::fopen(path.c_str(),"rb");
    assert(file);
    Bytes bytes;
    for(int byte=std::fgetc(file);byte!=EOF;byte=std::fgetc(file))bytes.push_back(static_cast<unsigned char>(byte));
    assert(!std::ferror(file));
    assert(std::fclose(file)==0);
    return bytes;
}

void append32(Bytes& bytes,std::uint32_t value) {
    for(unsigned i=0;i<4;++i){bytes.push_back(static_cast<unsigned char>(value&255));value>>=8;}
}

void set32(Bytes& bytes,std::size_t offset,std::uint32_t value) {
    for(unsigned i=0;i<4;++i){bytes.at(offset+i)=static_cast<unsigned char>(value&255);value>>=8;}
}

void sign(Bytes& bytes) {
    std::uint32_t hash=2166136261U;
    for(std::size_t i=0;i<bytes.size()-4;++i)hash=(hash^bytes[i])*16777619U;
    set32(bytes,bytes.size()-4,hash);
}

Bytes legacy(std::uint32_t profile) {
    Bytes bytes{'O','N','S','T'};
    for(auto value:{1U,profile,0U})append32(bytes,value);
    sign(bytes);
    return bytes;
}

Bytes current(const settingsFile::Saved& saved) {
    Bytes bytes{'O','N','S','T'};
    for(auto value:{2U,saved.hasSettings?1U:0U,saved.artworkCache?1U:0U,
                   static_cast<std::uint32_t>(saved.settings.width),static_cast<std::uint32_t>(saved.settings.height),
                   static_cast<std::uint32_t>(saved.settings.fps),static_cast<std::uint32_t>(saved.settings.bitrate_kbps),
                   static_cast<std::uint32_t>(saved.settings.mode),static_cast<std::uint32_t>(saved.settings.quality),
                   static_cast<std::uint32_t>(saved.settings.network),0U})append32(bytes,value);
    sign(bytes);
    return bytes;
}

void assertDefault(const settingsFile::Saved& saved) {
    assert(!saved.hasSettings&&saved.settings==StreamSettings{}&&saved.artworkCache);
}

void assertCorrupt(const std::string& path,const Bytes& bytes) {
    writeRaw(path,bytes);
    settingsFile::Saved saved{true,settingsFor(StreamProfile::native_hdr120),false};
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::corrupt);
    assertDefault(saved);
}

void assertRoundtrip(const std::string& path,const settingsFile::Saved& expected) {
    assert(settingsFile::save(path.c_str(),expected)==0);
    assert(readRaw(path)==current(expected));
    settingsFile::Saved saved;
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::loaded);
    assert(saved.hasSettings==expected.hasSettings&&saved.settings==expected.settings&&saved.artworkCache==expected.artworkCache);
    struct stat info{};
    assert(::stat(path.c_str(),&info)==0&&(info.st_mode&0777)==0600&&info.st_size==48);
    assert(::lstat((path+".tmp").c_str(),&info)==-1&&errno==ENOENT);
}
}

int main() {
    char root[]="/tmp/opennow-settings-XXXXXX";
    assert(mkdtemp(root));
    const std::string path=std::string(root)+"/settings.bin";
    const std::string account=std::string(root)+"/account.bin";
    writeRaw(account,{'t','o','k','e','n'});
#ifdef __linux__
    const int notifications=::inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
    assert(notifications>=0);
    const int watch=::inotify_add_watch(notifications,root,IN_ALL_EVENTS);
    assert(watch>=0);
#endif

    settingsFile::Saved saved{true,settingsFor(StreamProfile::native_hdr120),false};
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::missing);
    assertDefault(saved);

    for(std::uint32_t profile=0;profile<static_cast<std::uint32_t>(StreamProfile::count);++profile) {
        const auto bytes=legacy(profile);
        writeRaw(path,bytes);
        assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::loaded);
        assert(saved.hasSettings&&saved.settings==settingsFor(static_cast<StreamProfile>(profile))&&saved.artworkCache);
        assert(readRaw(path)==bytes);
        assertRoundtrip(path,saved);
    }

    assertRoundtrip(path,{true,{}});
    for(auto mode:{VideoMode::h264Software,VideoMode::h264Hardware,VideoMode::hevcMain10SdrHardware,VideoMode::hevcMain10HdrHardware})
        for(auto quality:{QualityMode::original,QualityMode::adaptive,QualityMode::clarity})
            for(auto network:{NetworkPolicy::adaptive,NetworkPolicy::fixed})
                for(bool cache:{false,true}) {
                    const settingsFile::Saved candidate{true,{1600,900,45,42000,mode,quality,network},cache};
                    if(mode==VideoMode::h264Hardware&&network==NetworkPolicy::adaptive) {
                        assert(settingsFile::save(path.c_str(),candidate)==EINVAL);
                        assertCorrupt(path,current(candidate));
                    } else assertRoundtrip(path,candidate);
                }
    assertRoundtrip(path,{true,{3840,2160,120,100000,VideoMode::hevcMain10HdrHardware,QualityMode::clarity,NetworkPolicy::fixed},false});
    for(auto mode:{VideoMode::h264Software,VideoMode::h264Hardware}) {
        const auto network=mode==VideoMode::h264Hardware?NetworkPolicy::fixed:NetworkPolicy::adaptive;
        assertRoundtrip(path,{true,{320,180,30,4000,mode,QualityMode::original,network},true});
        assertRoundtrip(path,{true,mode==VideoMode::h264Software?StreamSettings{1920,1080,60,100000}:StreamSettings{3840,2160,120,100000,mode,QualityMode::original,network},true});
    }
    for(bool cache:{false,true}) {
        assertRoundtrip(path,{true,settingsFor(StreamProfile::native_hdr120),cache});
        assertRoundtrip(path,{false,{},cache});
        assertRoundtrip(path,{false,{1600,900,45,42000},cache});
    }

    const auto v1=legacy(static_cast<std::uint32_t>(StreamProfile::smooth));
    const auto v2=current({true,{1600,900,45,42000,VideoMode::hevcMain10SdrHardware,QualityMode::clarity,NetworkPolicy::fixed},false});
    for(const auto& valid:{v1,v2}) {
        for(std::size_t size=0;size<valid.size();++size)
            assertCorrupt(path,Bytes(valid.begin(),valid.begin()+size));
        auto bytes=valid;
        bytes.push_back(0);
        assertCorrupt(path,bytes);
        bytes.resize(4096);
        assertCorrupt(path,bytes);
        for(std::size_t i=0;i<valid.size();++i) {
            bytes=valid;
            bytes[i]^=1;
            assertCorrupt(path,bytes);
        }
        bytes=valid;
        bytes[0]='X';
        sign(bytes);
        assertCorrupt(path,bytes);
        for(auto version:{0U,3U,0xffffffffU}) {
            bytes=valid;
            set32(bytes,4,version);
            sign(bytes);
            assertCorrupt(path,bytes);
        }
        bytes=valid;
        set32(bytes,4,valid.size()==16?2:1);
        sign(bytes);
        assertCorrupt(path,bytes);
    }
    for(auto profile:{static_cast<std::uint32_t>(StreamProfile::count),1000U,0xffffffffU})
        assertCorrupt(path,legacy(profile));

    for(std::size_t offset:{8,12,32,36,40}) {
        const std::uint32_t firstInvalid=offset==32?4:offset==36?3:2;
        for(auto value:{firstInvalid,256U,0x80000000U,0xffffffffU}) {
            auto bytes=v2;
            set32(bytes,offset,value);
            sign(bytes);
            assertCorrupt(path,bytes);
        }
    }
    struct InvalidField {std::size_t offset;std::uint32_t value;};
    for(auto field:{InvalidField{16,0},{16,318},{16,321},{16,3842},{16,0xffffffffU},{16,0x80000000U},
                    {20,0},{20,178},{20,181},{20,2162},{20,0xffffffffU},{20,0x80000000U},
                    {24,0},{24,29},{24,121},{24,0xffffffffU},{24,0x80000000U},
                    {28,0},{28,3999},{28,100001},{28,0xffffffffU},{28,0x80000000U}}) {
        for(bool hasSettings:{false,true}) {
            auto bytes=v2;
            set32(bytes,8,hasSettings?1:0);
            set32(bytes,field.offset,field.value);
            sign(bytes);
            assertCorrupt(path,bytes);
        }
    }
    for(auto field:{InvalidField{16,1922},{20,1082},{24,61}}) {
        auto bytes=v2;
        set32(bytes,32,0);
        set32(bytes,field.offset,field.value);
        sign(bytes);
        assertCorrupt(path,bytes);
    }

    auto invalidNetwork=StreamSettings{};
    invalidNetwork.mode=VideoMode::h264Hardware;
    assertCorrupt(path,current({false,invalidNetwork,false}));
    assertRoundtrip(path,{true,{},false});
    const auto before=readRaw(path);
    std::vector<StreamSettings> invalid;
    auto settings=StreamSettings{};
    settings.mode=static_cast<VideoMode>(-1);invalid.push_back(settings);
    settings={};settings.mode=static_cast<VideoMode>(4);invalid.push_back(settings);
    settings={};settings.quality=static_cast<QualityMode>(-1);invalid.push_back(settings);
    settings={};settings.quality=static_cast<QualityMode>(3);invalid.push_back(settings);
    settings={};settings.network=static_cast<NetworkPolicy>(-1);invalid.push_back(settings);
    settings={};settings.network=static_cast<NetworkPolicy>(2);invalid.push_back(settings);
    invalid.push_back(invalidNetwork);
    settings={};settings.width=-1;invalid.push_back(settings);
    settings={};settings.height=2160;invalid.push_back(settings);
    settings={};settings.fps=120;invalid.push_back(settings);
    settings={};settings.bitrate_kbps=100001;invalid.push_back(settings);
    for(const auto& bad:invalid)
        for(bool hasSettings:{false,true}) {
            assert(settingsFile::save(path.c_str(),{hasSettings,bad,false})==EINVAL);
            assert(readRaw(path)==before);
        }

    const std::string missingDirectory=std::string(root)+"/missing/settings.bin";
    assert(settingsFile::save(missingDirectory.c_str(),{true,{}})==ENOENT);
    assert(settingsFile::load(missingDirectory.c_str(),saved)==settingsFile::Status::missing);
    assertDefault(saved);
    assert(settingsFile::save(std::string(1024,'x').c_str(),{true,{}})==ENAMETOOLONG);
    assert(settingsFile::load(root,saved)==settingsFile::Status::unreadable&&errno==EISDIR);
    assertDefault(saved);

    writeRaw(path+".tmp",{'o','l','d'});
    assert(::chmod((path+".tmp").c_str(),0666)==0);
    assertRoundtrip(path,{true,{},false});
    assert(::symlink(account.c_str(),(path+".tmp").c_str())==0);
    assert(settingsFile::save(path.c_str(),{true,{}})==ELOOP);
    assert(readRaw(path)==before);
    assert(::unlink((path+".tmp").c_str())==0);

    assert(::unlink(path.c_str())==0);
    assert(::symlink(account.c_str(),path.c_str())==0);
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::unreadable&&errno==ELOOP);
    assertDefault(saved);
    assertRoundtrip(path,{false,{},false});
    assert(::unlink(path.c_str())==0);
    assert(::mkdir(path.c_str(),0700)==0);
    assert(settingsFile::save(path.c_str(),{true,{}})==EISDIR);
    struct stat info{};
    assert(::lstat((path+".tmp").c_str(),&info)==-1&&errno==ENOENT);
    assert(::rmdir(path.c_str())==0);

#ifdef __linux__
    alignas(inotify_event) char events[16384];
    for(;;) {
        const auto size=::read(notifications,events,sizeof(events));
        if(size<0){assert(errno==EAGAIN);break;}
        assert(size>0);
        for(std::size_t offset=0;offset<static_cast<std::size_t>(size);) {
            const auto* event=reinterpret_cast<const inotify_event*>(events+offset);
            assert(!(event->mask&IN_Q_OVERFLOW));
            assert(!event->len||std::strcmp(event->name,"account.bin")!=0);
            offset+=sizeof(inotify_event)+event->len;
        }
    }
    assert(::inotify_rm_watch(notifications,watch)==0);
    assert(::close(notifications)==0);
#endif
    assert(readRaw(account)==Bytes({'t','o','k','e','n'}));
    assert(::unlink(account.c_str())==0);
    assert(::rmdir(root)==0);
    std::puts("V1 migration, V2 settings/cache/reset, corruption, atomic saves and account isolation passed");
}
