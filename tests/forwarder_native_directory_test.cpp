// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder/files.hpp"
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
struct Handle {std::string path;bool read=false;};
std::map<int,Handle> handles;
std::set<std::string> directories,files;
unsigned reads=0,closes=0,unlinks=0;
int nextFd=7,readError=0,malformed=0;
int nativeError(int error){return static_cast<int>(0x80020000U|static_cast<unsigned>(error));}
std::vector<std::string> names(const std::string& path) {
    std::vector<std::string> result{".",".."};
    for(const auto* entries:{&directories,&files})for(const auto& entry:*entries) {
        const auto slash=entry.rfind('/');
        if(entry.substr(0,slash)==path)result.push_back(entry.substr(slash+1));
    }
    return result;
}
void reset() {
    assert(handles.empty());directories={"/nonempty","/empty","/tree","/tree/sub"};
    files={"/nonempty/tile","/tree/sub/param.json","/tree/program.bin"};
    reads=closes=unlinks=0;readError=malformed=0;
}
}
extern "C" int sceKernelOpen(const char* path,int flags,unsigned mode) {
    assert(flags==(O_RDONLY|O_DIRECTORY|O_NOFOLLOW)&&mode==0);
    if(!directories.contains(path))return nativeError(files.contains(path)?ENOTDIR:ENOENT);
    const int fd=nextFd++;handles.emplace(fd,Handle{path});return fd;
}
extern "C" int sceKernelClose(int fd) {assert(handles.erase(fd)==1);++closes;return 0;}
extern "C" int sceKernelGetdents(int fd,char* data,int capacity) {
    auto& handle=handles.at(fd);++reads;
    if(readError)return nativeError(readError);
    const auto entries=names(handle.path);
    if(entries.size()==2)return 0;
    if(capacity<65536)return nativeError(EINVAL);
    if(handle.read)return 0;
    handle.read=true;
    if(malformed==1)return capacity+1;
    if(malformed==2)return 4;
    std::memset(data,0,capacity);
    std::size_t offset=0;
    for(unsigned i=0;i<entries.size();++i) {
        auto* entry=reinterpret_cast<dirent*>(data+offset);
        const auto& name=entries[i];
        const auto length=i+1==entries.size()?65536-offset:(offsetof(dirent,d_name)+name.size()+1+3)&~std::size_t{3};
        entry->d_fileno=i+1;entry->d_reclen=static_cast<std::uint16_t>(length);
        entry->d_namlen=static_cast<std::uint8_t>(name.size());
        std::memcpy(entry->d_name,name.c_str(),name.size()+1);offset+=length;
    }
    auto* first=reinterpret_cast<dirent*>(data);
    if(malformed==3)first->d_reclen=0;
    if(malformed==4)first->d_namlen=255;
    if(malformed==5)std::memset(first->d_name,'x',first->d_reclen - offsetof(dirent,d_name));
    return 65536;
}
extern "C" int sceKernelUnlink(const char* path) {
    if(!files.erase(path))return nativeError(ENOENT);
    ++unlinks;return 0;
}
extern "C" int sceKernelRmdir(const char* path) {
    if(!directories.contains(path))return nativeError(ENOENT);
    if(names(path).size()!=2)return nativeError(ENOTEMPTY);
    directories.erase(path);return 0;
}

int main() {
    namespace fs=opennow::forwarder::files;
    namespace account=opennow::accountFile;
    reset();
    {
        const int fd=account::open("/nonempty",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        char oldBuffer[8192];errno=0;
        assert(account::checked(sceKernelGetdents(fd,oldBuffer,sizeof(oldBuffer)))==-1&&errno==EINVAL);
        assert(account::close(fd)==0);
    }
    {
        fs::Directory directory("/nonempty");
        assert(directory.valid()&&!std::strcmp(directory.next(),"tile"));
        assert(!directory.next()&&directory.error()==0);
    }
    {
        fs::Directory empty("/empty");assert(empty.valid()&&!empty.next()&&empty.error()==0);
    }
    {
        const int fd=account::open("/nonempty",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        fs::Directory pinned(fd);assert(!std::strcmp(pinned.next(),"tile"));
    }
    for(int invalid=1;invalid<=5;++invalid) {
        malformed=invalid;fs::Directory directory("/nonempty");
        assert(!directory.next()&&directory.error()==EIO);
    }
    malformed=0;readError=EACCES;
    {
        fs::Directory directory("/nonempty");assert(!directory.next()&&directory.error()==EACCES);
        assert(!fs::erase("/tree")&&unlinks==0&&directories.contains("/tree"));
    }
    readError=0;
    assert(fs::erase("/tree")&&!directories.contains("/tree")&&unlinks==2);
    assert(handles.empty());
    std::puts("native directory tests passed: 8KiB EINVAL reproduction, 64KiB pages, pinned descriptors, malformed records and cleanup");
}
