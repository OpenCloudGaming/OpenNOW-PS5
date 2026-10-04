// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "account_file.hpp"
#include "stream/stream_settings.hpp"
#include <cstdint>

namespace opennow::settingsFile {
enum class Status { missing, loaded, corrupt, unreadable };
struct Saved {
    bool hasProfile=false;
    StreamProfile profile=StreamProfile::quality;
};
struct Record {
    char magic[4];
    std::uint32_t version;
    std::int32_t profile;
    std::uint32_t checksum;
};
inline std::uint32_t checksum(const Record& record) noexcept {
    std::uint32_t hash=2166136261U;
    const auto* bytes=reinterpret_cast<const unsigned char*>(&record);
    for(std::size_t i=0;i<offsetof(Record,checksum);++i){hash^=bytes[i];hash*=16777619U;}
    return hash;
}
inline Status load(const char* path,Saved& out) noexcept {
    out=Saved{};
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW);
    if(fd<0)return errno==ENOENT?Status::missing:Status::unreadable;
    Record record{};
    std::size_t offset=0;
    bool ok=true;
    while(offset<sizeof(record)) {
        const auto n=accountFile::read(fd,reinterpret_cast<char*>(&record)+offset,sizeof(record)-offset);
        if(n<=0){ok=false;break;}
        offset+=static_cast<std::size_t>(n);
    }
    char extra=0;
    if(ok&&accountFile::read(fd,&extra,1)!=0)ok=false;
    accountFile::close(fd);
    if(!ok||std::memcmp(record.magic,"ONST",4)!=0||record.version!=1||record.checksum!=checksum(record)||
       record.profile<0||record.profile>=static_cast<std::int32_t>(StreamProfile::count))
        return Status::corrupt;
    out.hasProfile=true;
    out.profile=static_cast<StreamProfile>(record.profile);
    return Status::loaded;
}
inline int save(const char* path,const Saved& saved) noexcept {
    if(!saved.hasProfile)return EINVAL;
    char temporary[1024];
    if(std::snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=static_cast<int>(sizeof(temporary)))return ENAMETOOLONG;
    Record record{{'O','N','S','T'},1,static_cast<std::int32_t>(saved.profile),0};
    record.checksum=checksum(record);
    const int fd=accountFile::open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0)return errno;
    int error=0;
    std::size_t offset=0;
    while(!error&&offset<sizeof(record)) {
        const auto n=accountFile::write(fd,reinterpret_cast<const char*>(&record)+offset,sizeof(record)-offset);
        if(n<=0)error=n<0?errno:EIO;
        else offset+=static_cast<std::size_t>(n);
    }
    if(!error&&accountFile::sync(fd)!=0)error=errno;
    if(accountFile::close(fd)!=0&&!error)error=errno;
    if(!error&&accountFile::rename(temporary,path)!=0)error=errno;
    if(error){accountFile::remove(temporary);return error;}
    return accountFile::syncParent(path)?0:errno;
}
inline int clear(const char* path) noexcept {
    char temporary[1024];
    if(std::snprintf(temporary,sizeof(temporary),"%s.tmp",path)<static_cast<int>(sizeof(temporary)))accountFile::remove(temporary);
    if(accountFile::remove(path)!=0)return errno==ENOENT?0:errno;
    return accountFile::syncParent(path)?0:errno;
}
}
