// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "account_file.hpp"
#include "stream/stream_settings.hpp"
#include <array>
#include <cstdint>
#include <limits>

namespace opennow::settingsFile {
enum class Status { missing, loaded, corrupt, unreadable };
struct Saved {
    bool hasSettings=false;
    StreamSettings settings{};
    bool artworkCache=true;
};
using Record=std::array<unsigned char,48>;
inline std::uint32_t read32(const unsigned char* bytes) noexcept {
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|
           (std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
inline void write32(unsigned char* bytes,std::uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i)bytes[i]=static_cast<unsigned char>(value>>(8*i));
}
inline std::uint32_t checksum(const unsigned char* bytes,std::size_t size) noexcept {
    std::uint32_t hash=2166136261U;
    for(std::size_t i=0;i<size;++i){hash^=bytes[i];hash*=16777619U;}
    return hash;
}
inline Status load(const char* path,Saved& out) noexcept {
    out=Saved{};
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW);
    if(fd<0)return errno==ENOENT?Status::missing:Status::unreadable;
    std::array<unsigned char,49> record{};
    std::size_t offset=0;
    int error=0;
    while(offset<record.size()) {
        const auto n=accountFile::read(fd,record.data()+offset,record.size()-offset);
        if(n<0&&errno==EINTR)continue;
        if(n<0){error=errno;break;}
        if(n==0)break;
        offset+=static_cast<std::size_t>(n);
    }
    if(accountFile::close(fd)!=0&&!error)error=errno;
    if(error){errno=error;return Status::unreadable;}
    if((offset!=16&&offset!=48)||std::memcmp(record.data(),"ONST",4)!=0||
       read32(record.data()+offset-4)!=checksum(record.data(),offset-4))
        return Status::corrupt;
    const auto version=read32(record.data()+4);
    if(version==1&&offset==16) {
        const auto profile=read32(record.data()+8);
        if(profile>=static_cast<std::uint32_t>(StreamProfile::count))return Status::corrupt;
        out={true,settingsFor(static_cast<StreamProfile>(profile)),true};
        return Status::loaded;
    }
    if(version!=2||offset!=48)return Status::corrupt;
    const auto hasSettings=read32(record.data()+8);
    const auto artworkCache=read32(record.data()+12);
    const auto mode=read32(record.data()+32);
    const auto quality=read32(record.data()+36);
    const auto network=read32(record.data()+40);
    if(hasSettings>1||artworkCache>1||mode>static_cast<std::uint32_t>(VideoMode::hevcMain10HdrHardware)||
       quality>static_cast<std::uint32_t>(QualityMode::clarity)||network>static_cast<std::uint32_t>(NetworkPolicy::fixed))
        return Status::corrupt;
    for(std::size_t i=16;i<32;i+=4)
        if(read32(record.data()+i)>static_cast<std::uint32_t>(std::numeric_limits<int>::max()))return Status::corrupt;
    StreamSettings settings{static_cast<int>(read32(record.data()+16)),static_cast<int>(read32(record.data()+20)),
        static_cast<int>(read32(record.data()+24)),static_cast<int>(read32(record.data()+28)),
        static_cast<VideoMode>(mode),static_cast<QualityMode>(quality),static_cast<NetworkPolicy>(network)};
    if(validateSettings(settings)!=SettingsError::none)return Status::corrupt;
    out={hasSettings==1,settings,artworkCache==1};
    return Status::loaded;
}
inline int save(const char* path,const Saved& saved) noexcept {
    if(validateSettings(saved.settings)!=SettingsError::none)return EINVAL;
    char temporary[1024];
    if(std::snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=static_cast<int>(sizeof(temporary)))return ENAMETOOLONG;
    Record record{'O','N','S','T'};
    write32(record.data()+4,2);
    write32(record.data()+8,saved.hasSettings?1:0);
    write32(record.data()+12,saved.artworkCache?1:0);
    write32(record.data()+16,static_cast<std::uint32_t>(saved.settings.width));
    write32(record.data()+20,static_cast<std::uint32_t>(saved.settings.height));
    write32(record.data()+24,static_cast<std::uint32_t>(saved.settings.fps));
    write32(record.data()+28,static_cast<std::uint32_t>(saved.settings.bitrate_kbps));
    write32(record.data()+32,static_cast<std::uint32_t>(saved.settings.mode));
    write32(record.data()+36,static_cast<std::uint32_t>(saved.settings.quality));
    write32(record.data()+40,static_cast<std::uint32_t>(saved.settings.network));
    write32(record.data()+44,checksum(record.data(),44));
    const int fd=accountFile::open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0)return errno;
    int error=accountFile::permissions(fd,0600)==0?0:errno;
    std::size_t offset=0;
    while(!error&&offset<record.size()) {
        const auto n=accountFile::write(fd,record.data()+offset,record.size()-offset);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)error=n<0?errno:EIO;
        else offset+=static_cast<std::size_t>(n);
    }
    if(!error&&accountFile::sync(fd)!=0)error=errno;
    if(accountFile::close(fd)!=0&&!error)error=errno;
    if(!error&&accountFile::rename(temporary,path)!=0)error=errno;
    if(error){accountFile::remove(temporary);return error;}
    std::snprintf(temporary,sizeof(temporary),"%s",path);
    auto* slash=std::strrchr(temporary,'/');
    if(!slash)std::snprintf(temporary,sizeof(temporary),".");
    else if(slash==temporary)slash[1]=0;
    else *slash=0;
    const int parent=accountFile::open(temporary,O_RDONLY|O_DIRECTORY);
    if(parent<0)return errno;
    if(accountFile::sync(parent)!=0)error=errno;
    if(accountFile::close(parent)!=0&&!error)error=errno;
    return error;
}
}
