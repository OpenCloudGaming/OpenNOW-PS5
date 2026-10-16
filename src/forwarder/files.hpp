// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../account_file.hpp"
#include <cstdlib>
#include <dirent.h>

#ifdef OPENNOW_PS5
extern "C" {
int sceKernelGetdents(int,char*,int);
int sceKernelRmdir(const char*);
}
#endif

namespace opennow::forwarder::files {
inline constexpr unsigned directoryReadBytes=64*1024;
inline bool join(char (&out)[1024],const char* parent,const char* name) noexcept {
    const int n=std::snprintf(out,sizeof(out),"%s/%s",parent,name);
    return n>0&&static_cast<std::size_t>(n)<sizeof(out);
}
class Directory {
public:
    explicit Directory(const char* path) noexcept:Directory(accountFile::open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW)){}
    explicit Directory(int fd) noexcept:fd_(fd) {
        if(fd_<0){error_=errno;return;}
#ifndef OPENNOW_PS5
        dir_=fdopendir(fd_);
        if(!dir_){error_=errno;accountFile::close(fd_);fd_=-1;}
#endif
    }
    ~Directory() {
#ifdef OPENNOW_PS5
        std::free(buffer_);
        if(fd_>=0)accountFile::close(fd_);
#else
        if(dir_)closedir(dir_);
#endif
    }
    Directory(const Directory&)=delete;
    Directory& operator=(const Directory&)=delete;
    bool valid() const noexcept {return fd_>=0;}
    int error() const noexcept {return error_;}
    const char* next() noexcept {
        if(!valid())return nullptr;
        for(;;) {
#ifdef OPENNOW_PS5
            if(!buffer_) {
                buffer_=static_cast<char*>(std::malloc(directoryReadBytes));
                if(!buffer_){error_=ENOMEM;return nullptr;}
            }
            if(offset_==size_) {
                const int n=static_cast<int>(accountFile::checked(sceKernelGetdents(fd_,buffer_,directoryReadBytes)));
                if(n<0){error_=errno;return nullptr;}
                if(!n)return nullptr;
                if(static_cast<unsigned>(n)>directoryReadBytes){error_=EIO;return nullptr;}
                size_=static_cast<unsigned>(n);offset_=0;
            }
            if(size_-offset_<offsetof(dirent,d_name)+1){error_=EIO;return nullptr;}
            const auto* entry=reinterpret_cast<const dirent*>(buffer_+offset_);
            const auto bytes=entry->d_reclen;
            if(bytes<offsetof(dirent,d_name)+1||bytes>size_-offset_||
               entry->d_namlen>=bytes - offsetof(dirent,d_name)||
               !std::memchr(entry->d_name,0,bytes - offsetof(dirent,d_name))){error_=EIO;return nullptr;}
            offset_+=bytes;
#else
            errno=0;
            const auto* entry=readdir(dir_);
            if(!entry){error_=errno;return nullptr;}
#endif
            if(!std::strcmp(entry->d_name,".")||!std::strcmp(entry->d_name,".."))continue;
            return entry->d_name;
        }
    }
private:
    int fd_=-1,error_=0;
#ifdef OPENNOW_PS5
    char* buffer_=nullptr;
    unsigned offset_=0,size_=0;
#else
    DIR* dir_=nullptr;
#endif
};
inline bool exists(const char* path) noexcept {
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return errno!=ENOENT;
    accountFile::close(fd);return true;
}
inline bool mkdir(const char* path) noexcept {
    if(accountFile::makeDirectory(path,0755)==0)return true;
    if(errno!=EEXIST)return false;
    Directory dir(path);return dir.valid();
}
inline bool syncDirectory(const char* path) noexcept {
    const int fd=accountFile::open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(fd<0)return false;
    const bool ok=accountFile::sync(fd)==0;
    return accountFile::close(fd)==0&&ok;
}
inline bool write(void* context,const void* data,std::size_t size) noexcept {
    const int fd=*static_cast<const int*>(context);
    const auto* bytes=static_cast<const unsigned char*>(data);
    while(size) {
        const auto n=accountFile::write(fd,bytes,size);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return false;
        bytes+=n;size-=static_cast<std::size_t>(n);
    }
    return true;
}
inline bool finish(int fd,bool ok) noexcept {
    if(ok)ok=accountFile::sync(fd)==0;
    return accountFile::close(fd)==0&&ok;
}
inline bool writeFile(const char* path,const void* data,std::size_t size) noexcept {
    int fd=accountFile::open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
    return fd>=0&&finish(fd,write(&fd,data,size));
}
inline bool copy(const char* source,const char* target,std::size_t limit) noexcept {
    const int from=accountFile::open(source,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(from<0)return false;
    int to=accountFile::open(target,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
    if(to<0){accountFile::close(from);return false;}
    unsigned char buffer[16384];
    std::size_t total=0;
    bool ok=true;
    for(;;) {
        const auto n=accountFile::read(from,buffer,sizeof(buffer));
        if(n<0&&errno==EINTR)continue;
        if(n<0){ok=false;break;}
        if(!n)break;
        total+=static_cast<std::size_t>(n);
        if(total>limit||!write(&to,buffer,static_cast<std::size_t>(n))){ok=false;break;}
    }
    if(accountFile::close(from)!=0)ok=false;
    return finish(to,ok&&total>0);
}
inline bool readText(const char* path,char* buffer,std::size_t size) noexcept {
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return false;
    std::size_t used=0;
    bool ok=true;
    while(used<size) {
        const auto n=accountFile::read(fd,buffer+used,size-used);
        if(n<0&&errno==EINTR)continue;
        if(n<0){ok=false;break;}
        if(!n)break;
        used+=static_cast<std::size_t>(n);
    }
    if(accountFile::close(fd)!=0)ok=false;
    if(!ok||used==0||used==size||std::memchr(buffer,0,used))return false;
    buffer[used]=0;return true;
}
inline bool erase(const char* path,unsigned depth=0) noexcept {
    if(depth>8){errno=ELOOP;return false;}
    Directory directory(path);
    if(!directory.valid()) {
        if(directory.error()==ENOENT)return true;
        if(directory.error()!=ENOTDIR&&directory.error()!=ELOOP)return false;
        return accountFile::remove(path)==0;
    }
    while(const char* name=directory.next()) {
        char child[1024];
        if(!join(child,path,name)||!erase(child,depth+1))return false;
    }
    if(directory.error())return false;
#ifdef OPENNOW_PS5
    return accountFile::checked(sceKernelRmdir(path))==0;
#else
    return ::rmdir(path)==0;
#endif
}
}
