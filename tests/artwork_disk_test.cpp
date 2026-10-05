// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/artwork_disk.hpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef OPENNOW_PS5
namespace {
int openError=0,writeError=0,syncError=0,renameError=0,readError=0;
bool shortIO=false,interruptRead=false,interruptWrite=false,zeroWrite=false;
unsigned opens=0,reads=0,writes=0,renames=0,removes=0,permissions=0,syncs=0;
std::int64_t native(std::int64_t value) {return value<0?static_cast<std::int32_t>(0x80020000U|errno):value;}
}
extern "C" int sceKernelOpen(const char* path,int flags,unsigned mode) {
    ++opens;assert(flags&O_NOFOLLOW);
    if(flags&O_CREAT)assert(mode==0600&&(flags&O_EXCL));
    if(openError){errno=openError;return static_cast<int>(native(-1));}
    return static_cast<int>(native(::open(path,flags,mode)));
}
extern "C" std::int64_t sceKernelRead(int fd,void* data,std::size_t size) {
    ++reads;
    if(interruptRead){interruptRead=false;errno=EINTR;return native(-1);}
    if(readError){errno=readError;return native(-1);}
    return native(::read(fd,data,shortIO?std::min(size,std::size_t(19)):size));
}
extern "C" std::int64_t sceKernelWrite(int fd,const void* data,std::size_t size) {
    ++writes;
    if(interruptWrite){interruptWrite=false;errno=EINTR;return native(-1);}
    if(writeError){errno=writeError;return native(-1);}
    if(zeroWrite)return 0;
    return native(::write(fd,data,shortIO?std::min(size,std::size_t(23)):size));
}
extern "C" int sceKernelClose(int fd) {return static_cast<int>(native(::close(fd)));}
extern "C" int sceKernelFsync(int fd) {
    ++syncs;
    if(syncError){errno=syncError;return static_cast<int>(native(-1));}
    return static_cast<int>(native(::fsync(fd)));
}
extern "C" int sceKernelFchmod(int fd,unsigned mode) {
    ++permissions;assert(mode==0600||mode==0700);
    return static_cast<int>(native(::fchmod(fd,mode)));
}
extern "C" int sceKernelRename(const char* from,const char* to) {
    ++renames;
    if(renameError){errno=renameError;return static_cast<int>(native(-1));}
    return static_cast<int>(native(::rename(from,to)));
}
extern "C" int sceKernelUnlink(const char* path) {++removes;return static_cast<int>(native(::unlink(path)));}
extern "C" int sceKernelMkdir(const char* path,unsigned mode) {
    assert(mode==0700);return static_cast<int>(native(::mkdir(path,mode)));
}
#endif

namespace {
using opennow::art::DiskCache;
struct Temp {
    char parent[64]="/tmp/opennow-artwork-XXXXXX";
    std::string root;
    Temp(){assert(mkdtemp(parent));root=std::string(parent)+"/artwork";}
    ~Temp(){DiskCache disk(root.c_str());disk.clear();assert(rmdir(root.c_str())==0);assert(rmdir(parent)==0);}
    std::string slot(unsigned i=0) const {
        char name[32];std::snprintf(name,sizeof(name),"/slot-%03u.bin",i);return root+name;
    }
};
std::vector<unsigned char> scratch(DiskCache::slotBytes),data(1024,0x63);
std::string url(unsigned i){return "https://img.nvidiagrid.net/apps/"+std::to_string(i)+"/art.jpg;f=jpg;w=272";}
bool hit(DiskCache& disk,unsigned i,unsigned kind=0) {
    std::size_t size=0;
    return disk.get(url(i).c_str(),kind,scratch.data(),scratch.size(),size)&&size==data.size()&&
        !std::memcmp(data.data(),scratch.data(),size);
}
void put(DiskCache& disk,unsigned i,unsigned kind=0){disk.put(url(i).c_str(),kind,data.data(),data.size());}
std::vector<unsigned char> contents(const std::string& path) {
    const int fd=::open(path.c_str(),O_RDONLY);assert(fd>=0);
    std::vector<unsigned char> result(DiskCache::slotBytes+1);
    const auto n=::read(fd,result.data(),result.size());assert(n>=0);result.resize(n);assert(::close(fd)==0);return result;
}
void writeFile(const std::string& path,const unsigned char* bytes,std::size_t size) {
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
    assert(::write(fd,bytes,size)==static_cast<ssize_t>(size));assert(::close(fd)==0);
}
}

int main() {
    {
        Temp temp;
        DiskCache first(temp.root.c_str());assert(!first.available());
        first.initialize(scratch.data(),scratch.size());assert(first.available()&&!first.error());
        put(first,1);assert(first.count()==1&&first.bytes()==sizeof(DiskCache::Header)+data.size());
        struct stat info{};assert(stat(temp.root.c_str(),&info)==0&&(info.st_mode&0777)==0700);
        assert(stat(temp.slot().c_str(),&info)==0&&(info.st_mode&0777)==0600);
        DiskCache second(temp.root.c_str());second.initialize(scratch.data(),scratch.size());
        assert(hit(second,1)&&!hit(second,2)&&!hit(second,1,1));
        put(second,1,1);assert(second.count()==2&&hit(second,1,1));
        const auto sibling=std::string(temp.parent)+"/account.bin";
        const auto settings=std::string(temp.parent)+"/settings.bin";
        const auto unrelated=temp.root+"/unrelated.bin";
        for(const auto& path:{sibling,settings,unrelated})writeFile(path,data.data(),data.size());
        second.clear();assert(second.count()==0&&second.bytes()==0&&second.available());
        second.clear();
        for(const auto& path:{sibling,settings,unrelated}){assert(contents(path)==data);assert(unlink(path.c_str())==0);}
    }
    {
        Temp temp;DiskCache disk(temp.root.c_str());disk.initialize(scratch.data(),scratch.size());
        for(unsigned i=0;i<DiskCache::slots;++i)put(disk,i);
        assert(disk.count()==128);assert(hit(disk,0));
        DiskCache reopened(temp.root.c_str());reopened.initialize(scratch.data(),scratch.size());
        put(reopened,128);assert(reopened.count()==128&&hit(reopened,0)&&!hit(reopened,1)&&hit(reopened,128));
        std::size_t physical=0;
        for(unsigned i=0;i<DiskCache::slots;++i)physical+=contents(temp.slot(i)).size();
        assert(physical==reopened.bytes()&&physical<=DiskCache::storedBudget);
    }
    {
        Temp temp;DiskCache disk(temp.root.c_str());disk.initialize(scratch.data(),scratch.size());
        std::vector<unsigned char> large(DiskCache::payloadBytes+1,1);
        disk.put(url(0).c_str(),0,large.data(),large.size());assert(disk.count()==0);
        large.pop_back();
        for(unsigned i=0;i<130;++i)disk.put(url(i).c_str(),0,large.data(),large.size());
        assert(disk.count()==127&&disk.bytes()==DiskCache::storedBudget);
        std::size_t size=0;assert(!disk.get(url(0).c_str(),0,scratch.data(),scratch.size(),size));
        assert(disk.get(url(129).c_str(),0,scratch.data(),scratch.size(),size)&&size==large.size());
        assert(disk.bytes()+DiskCache::slotBytes<=DiskCache::budgetBytes);
    }
    for(unsigned fault=0;fault<9;++fault) {
        Temp temp;DiskCache disk(temp.root.c_str());disk.initialize(scratch.data(),scratch.size());put(disk,0);
        auto record=contents(temp.slot());
        switch(fault) {
            case 0:record[0]^=1;break;
            case 1:record[8]^=1;break;
            case 2:record[12]=2;break;
            case 3:record[16]^=1;break;
            case 4:record[32]^=1;break;
            case 5:record.back()^=1;break;
            case 6:record.resize(20);break;
            case 7:record.pop_back();break;
            case 8:record.push_back(1);break;
        }
        writeFile(temp.slot(),record.data(),record.size());
        DiskCache restarted(temp.root.c_str());restarted.initialize(scratch.data(),scratch.size());
        assert(restarted.available()&&restarted.error()&&restarted.count()==0&&!hit(restarted,0));
        put(restarted,0);assert(hit(restarted,0));
    }
    {
        Temp temp;DiskCache disk(temp.root.c_str());disk.initialize(scratch.data(),scratch.size());put(disk,0);
        auto record=contents(temp.slot());record.back()^=1;writeFile(temp.slot(),record.data(),record.size());
        assert(!hit(disk,0)&&disk.count()==0);
        writeFile(temp.root+"/pending.tmp",data.data(),data.size());
        disk.initialize(scratch.data(),scratch.size());assert(access((temp.root+"/pending.tmp").c_str(),F_OK)!=0);
        const auto sibling=std::string(temp.parent)+"/account.bin";writeFile(sibling,data.data(),data.size());
        assert(symlink(sibling.c_str(),temp.slot().c_str())==0);
        disk.initialize(scratch.data(),scratch.size());assert(disk.error()&&disk.count()==0&&contents(sibling)==data);
        assert(unlink(sibling.c_str())==0);
    }
    {
        DiskCache absent("/opennow-test-missing-parent-019811/artwork");
        absent.initialize(scratch.data(),scratch.size());assert(!absent.available()&&absent.error());
        DiskCache readonly("/sys/opennow-artwork-test");
        readonly.initialize(scratch.data(),scratch.size());assert(!readonly.available()&&readonly.error());
        DiskCache disabled(nullptr);disabled.initialize(scratch.data(),scratch.size());assert(!disabled.available()&&!disabled.error());
        DiskCache invalid("");invalid.initialize(scratch.data(),scratch.size());assert(!invalid.available()&&invalid.error());
        const std::string tooLong(1024,'a');DiskCache oversized(tooLong.c_str());
        oversized.initialize(scratch.data(),scratch.size());assert(!oversized.available()&&oversized.error());
    }
#ifdef OPENNOW_PS5
    {
        Temp temp;DiskCache disk(temp.root.c_str());disk.initialize(scratch.data(),scratch.size());
        shortIO=true;interruptWrite=true;put(disk,0);interruptRead=true;assert(hit(disk,0));shortIO=false;
        const auto original=contents(temp.slot());
        for(unsigned fault=0;fault<6;++fault) {
            disk.initialize(scratch.data(),scratch.size());
            switch(fault) {
                case 0:writeError=ENOSPC;break;
                case 1:writeError=EROFS;break;
                case 2:syncError=EIO;break;
                case 3:renameError=EIO;break;
                case 4:openError=EACCES;break;
                case 5:zeroWrite=true;break;
            }
            put(disk,0);assert(!disk.available()&&disk.error());
            openError=writeError=syncError=renameError=0;zeroWrite=false;
            assert(contents(temp.slot())==original);
            assert(access((temp.root+"/pending.tmp").c_str(),F_OK)!=0);
        }
        disk.initialize(scratch.data(),scratch.size());readError=EIO;
        assert(!hit(disk,0)&&!disk.available());readError=0;
        assert(contents(temp.slot())==original);
        disk.initialize(scratch.data(),scratch.size());assert(hit(disk,0));
    }
    assert(opens&&reads&&writes&&renames&&removes&&permissions&&syncs);
#endif
    std::puts("Artwork disk persistence, LRU, budget, corruption, permissions, atomicity and native facade passed");
}
