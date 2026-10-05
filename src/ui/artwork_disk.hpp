// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../account_file.hpp"
#include <algorithm>
#include <limits>

namespace opennow::art {
class DiskCache {
public:
    static constexpr unsigned slots=128;
    static constexpr std::size_t slotBytes=512*1024;
    static constexpr std::size_t budgetBytes=64*1024*1024;
    static constexpr std::size_t storedBudget=budgetBytes-slotBytes;
    struct Header {
        char magic[8];
        std::uint32_t version,kind,length,checksum;
        std::uint64_t access;
        char url[224];
    };
    static_assert(sizeof(Header)==256);
    static constexpr std::size_t payloadBytes=slotBytes-sizeof(Header);

    explicit DiskCache(const char* root) noexcept {
        if(root&&*root&&std::strlen(root)<sizeof(root_))std::snprintf(root_,sizeof(root_),"%s",root);
        invalidRoot_=root&&!configured();
    }
    bool configured() const noexcept {return root_[0]!=0;}
    bool available() const noexcept {return available_;}
    bool error() const noexcept {return error_;}
    std::size_t bytes() const noexcept {return bytes_;}
    std::size_t count() const noexcept {return count_;}

    void initialize(unsigned char* scratch,std::size_t capacity) noexcept {
        available_=false;error_=invalidRoot_;bytes_=count_=0;clock_=0;
        for(auto& entry:entries_)entry={};
        if(!directory())return;
        char path[1024];temporary(path);
        if(!remove(path))return;
        for(unsigned i=0;i<slots;++i) {
            Header header{};
            const auto result=read(i,header,scratch,capacity);
            if(result==Read::io){error_=true;return;}
            if(result==Read::invalid) {
                error_=true;filename(i,path);
                if(!remove(path))return;
            }
            if(result!=Read::valid)continue;
            entries_[i]=header;bytes_+=sizeof(Header)+header.length;++count_;
            clock_=std::max(clock_,header.access);
        }
        while(bytes_>storedBudget)if(!erase(oldest(slots)))return;
        available_=true;
    }

    bool get(const char* url,unsigned kind,unsigned char* data,std::size_t capacity,std::size_t& size) noexcept {
        size=0;
        if(!available_)return false;
        for(unsigned i=0;i<slots;++i) {
            if(!entries_[i].length||entries_[i].kind!=kind||std::strcmp(entries_[i].url,url))continue;
            Header header{};
            const auto result=read(i,header,data,capacity);
            if(result!=Read::valid||header.kind!=kind||std::strcmp(header.url,url)) {
                error_=true;
                if(result==Read::io)available_=false;
                else if(!erase(i))available_=false;
                return false;
            }
            size=header.length;
            store(i,url,kind,data,size);
            return true;
        }
        return false;
    }

    void put(const char* url,unsigned kind,const unsigned char* data,std::size_t size) noexcept {
        if(!available_||!size||size>payloadBytes||std::strlen(url)>=sizeof(Header::url)||kind>1)return;
        unsigned chosen=slots;
        for(unsigned i=0;i<slots;++i) {
            if(entries_[i].length&&entries_[i].kind==kind&&!std::strcmp(entries_[i].url,url)){chosen=i;break;}
            if(!entries_[i].length&&chosen==slots)chosen=i;
        }
        if(chosen==slots)chosen=oldest(slots);
        store(chosen,url,kind,data,size);
    }

    void discard(const char* url,unsigned kind) noexcept {
        for(unsigned i=0;i<slots;++i)
            if(entries_[i].length&&entries_[i].kind==kind&&!std::strcmp(entries_[i].url,url)) {
                error_=true;
                if(!erase(i))available_=false;
                return;
            }
    }

    void clear() noexcept {
        error_=invalidRoot_;available_=false;
        if(!directory())return;
        bool ok=true;
        for(unsigned i=0;i<slots;++i)if(!erase(i))ok=false;
        char path[1024];temporary(path);
        if(!remove(path))ok=false;
        if(!syncDirectory())ok=false;
        available_=ok;error_=!ok;
    }

private:
    enum class Read {valid,missing,invalid,io};
    char root_[960]{};
    Header entries_[slots]{};
    std::uint64_t clock_=0;
    std::size_t bytes_=0,count_=0;
    bool available_=false,error_=false,invalidRoot_=false;

    void filename(unsigned slot,char* path) const noexcept {
        std::snprintf(path,1024,"%s/slot-%03u.bin",root_,slot);
    }
    void temporary(char* path) const noexcept {std::snprintf(path,1024,"%s/pending.tmp",root_);}
    bool directory() noexcept {
        if(!configured())return false;
        if(accountFile::makeDirectory(root_,0700)!=0&&errno!=EEXIST){error_=true;return false;}
        const int fd=accountFile::open(root_,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if(fd<0){error_=true;return false;}
        bool ok=accountFile::permissions(fd,0700)==0;
        if(accountFile::close(fd)!=0)ok=false;
        if(!ok)error_=true;
        return ok;
    }
    bool syncDirectory() noexcept {
        const int fd=accountFile::open(root_,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if(fd<0){error_=true;return false;}
        bool ok=accountFile::sync(fd)==0;
        if(accountFile::close(fd)!=0)ok=false;
        if(!ok)error_=true;
        return ok;
    }
    bool remove(const char* path) noexcept {
        if(accountFile::remove(path)==0||errno==ENOENT)return true;
        error_=true;return false;
    }
    bool erase(unsigned slot) noexcept {
        char path[1024];filename(slot,path);
        if(!remove(path))return false;
        if(entries_[slot].length){bytes_-=sizeof(Header)+entries_[slot].length;--count_;}
        entries_[slot]={};
        return true;
    }
    unsigned oldest(unsigned excluded) const noexcept {
        unsigned chosen=slots;
        for(unsigned i=0;i<slots;++i)
            if(i!=excluded&&entries_[i].length&&(chosen==slots||entries_[i].access<entries_[chosen].access))chosen=i;
        return chosen;
    }
    static std::uint32_t checksum(Header header,const unsigned char* data) noexcept {
        header.checksum=0;
        std::uint32_t hash=2166136261U;
        const auto* bytes=reinterpret_cast<const unsigned char*>(&header);
        for(std::size_t i=0;i<sizeof(header);++i)hash=(hash^bytes[i])*16777619U;
        for(std::size_t i=0;i<header.length;++i)hash=(hash^data[i])*16777619U;
        return hash;
    }
    static bool readAll(int fd,void* destination,std::size_t size,bool& io) noexcept {
        auto* data=static_cast<unsigned char*>(destination);
        while(size) {
            const auto n=accountFile::read(fd,data,size);
            if(n<0&&errno==EINTR)continue;
            if(n<=0){io=n<0;return false;}
            data+=n;size-=static_cast<std::size_t>(n);
        }
        return true;
    }
    static bool writeAll(int fd,const void* source,std::size_t size) noexcept {
        const auto* data=static_cast<const unsigned char*>(source);
        while(size) {
            const auto n=accountFile::write(fd,data,size);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)return false;
            data+=n;size-=static_cast<std::size_t>(n);
        }
        return true;
    }
    Read read(unsigned slot,Header& header,unsigned char* data,std::size_t capacity) noexcept {
        char path[1024];filename(slot,path);
        const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
        if(fd<0)return errno==ENOENT?Read::missing:errno==ELOOP?Read::invalid:Read::io;
        bool io=false;
        bool ok=readAll(fd,&header,sizeof(header),io)&&
            !std::memcmp(header.magic,"ONART001",8)&&header.version==1&&header.kind<=1&&
            header.length&&header.length<=payloadBytes&&header.length<=capacity&&
            header.access&&header.access<std::numeric_limits<std::uint64_t>::max()-slots&&
            header.url[0]&&std::memchr(header.url,0,sizeof(header.url));
        if(ok)ok=readAll(fd,data,header.length,io);
        if(ok) {
            unsigned char extra;
            std::int64_t n;
            do {n=accountFile::read(fd,&extra,1);}while(n<0&&errno==EINTR);
            io=n<0;ok=n==0&&header.checksum==checksum(header,data);
        }
        if(accountFile::close(fd)!=0)io=true;
        return io?Read::io:ok?Read::valid:Read::invalid;
    }
    void store(unsigned slot,const char* url,unsigned kind,const unsigned char* data,std::size_t size) noexcept {
        Header header{};
        std::memcpy(header.magic,"ONART001",8);header.version=1;header.kind=kind;
        header.length=static_cast<std::uint32_t>(size);header.access=++clock_;
        std::snprintf(header.url,sizeof(header.url),"%s",url);header.checksum=checksum(header,data);
        char temp[1024],path[1024];temporary(temp);filename(slot,path);
        const int fd=accountFile::open(temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0){error_=true;available_=false;return;}
        bool ok=accountFile::permissions(fd,0600)==0&&writeAll(fd,&header,sizeof(header))&&
            writeAll(fd,data,size)&&accountFile::sync(fd)==0;
        if(accountFile::close(fd)!=0)ok=false;
        const auto previous=entries_[slot].length?sizeof(Header)+entries_[slot].length:0;
        while(ok&&bytes_-previous+sizeof(Header)+size>storedBudget)ok=erase(oldest(slot));
        if(ok)ok=accountFile::rename(temp,path)==0;
        if(ok) {
            bytes_=bytes_-previous+sizeof(Header)+size;
            if(!entries_[slot].length)++count_;
            entries_[slot]=header;
            ok=syncDirectory();
        } else remove(temp);
        if(!ok){error_=true;available_=false;}
    }
};
}
