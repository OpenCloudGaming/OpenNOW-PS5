// SPDX-License-Identifier: GPL-3.0-or-later
#include "installer_client.hpp"
#include "installer_protocol.hpp"
#include "../account_file.hpp"
#include <atomic>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#ifdef OPENNOW_PS5
extern "C" {
int sceKernelOpen(const char*,int,unsigned);
int sceKernelClose(int);
std::int64_t sceKernelRead(int,void*,std::size_t);
int sceKernelFstat(int,struct stat*);
int sceNetConnect(int,const void*,std::uint32_t);
int sceNetSend(int,const void*,std::size_t,int);
int sceNetRecv(int,void*,std::size_t,int);
int sceNetSetsockopt(int,int,int,const void*,std::uint32_t);
int sceNetSocket(const char*,int,int,int);
int sceNetSocketClose(int);
unsigned long long sceKernelGetProcessTime();
}
#endif

namespace opennow::forwarder::installer {
namespace {
std::atomic_uint32_t nextId{1};
#ifdef OPENNOW_PS5
constexpr char helperPath[]="/app0/opennow-tile-installer.elf";
std::atomic_bool probeRunning{false},cachedAvailable{false};
std::atomic_uint64_t cacheUntil{0};
struct NetSockaddrIn {
    std::uint8_t length,family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint16_t virtualPort;
    std::uint8_t zero[6];
};
int connect(Code* error=nullptr) noexcept {
    if(error)*error=Code::transportError;
    const int socket=sceNetSocket("opennow_tile_installer",2,1,6);
    if(socket<0)return -1;
    constexpr int level=0xffff,timeout=wire::ioTimeoutUs;
    for(const int option:{0x1105,0x1106,0x1109}) {
        if(sceNetSetsockopt(socket,level,option,&timeout,sizeof(timeout))<0) {
            sceNetSocketClose(socket);return -1;
        }
    }
    constexpr std::uint16_t port=9021;
    const NetSockaddrIn address{sizeof(NetSockaddrIn),2,static_cast<std::uint16_t>((port<<8)|(port>>8)),0x0100007f,0,{0}};
    if(sceNetConnect(socket,&address,sizeof(address))<0){if(error)*error=Code::unavailable;sceNetSocketClose(socket);return -1;}
    return socket;
}
void* probe(void*) noexcept {
    bool present=false;
    const int helper=sceKernelOpen(helperPath,O_RDONLY|O_NOFOLLOW,0);
    if(helper>=0) {
        sceKernelClose(helper);
        const int socket=connect();
        present=socket>=0;
        if(socket>=0)sceNetSocketClose(socket);
    }
    cachedAvailable.store(present,std::memory_order_relaxed);
    cacheUntil.store(sceKernelGetProcessTime()+(present?30'000'000:5'000'000),std::memory_order_relaxed);
    probeRunning.store(false,std::memory_order_release);
    return nullptr;
}
std::int64_t readSocket(void* context,void* data,std::size_t size) noexcept {
    return sceNetRecv(*static_cast<int*>(context),data,size,0);
}
std::int64_t writeSocket(void* context,const void* data,std::size_t size) noexcept {
    return sceNetSend(*static_cast<int*>(context),data,size,0);
}
bool cancelled(const Job& job) noexcept {return job.cancel&&job.cancel->load(std::memory_order_relaxed);}
struct Stage {
    std::array<int,std::size(wire::files)> descriptors;
    std::array<std::uint64_t,std::size(wire::files)> lengths{};
    unsigned total=0;
    Stage() noexcept {descriptors.fill(-1);}
    ~Stage(){for(const int fd:descriptors)if(fd>=0)accountFile::close(fd);}
    Stage(const Stage&)=delete;
    Stage& operator=(const Stage&)=delete;
    Result open(const char* folder,const Job& job) noexcept {
        if(!folder||!folder[0]||strnlen(folder,900)==900)return wire::failure(EINVAL,Code::stagingError);
        for(unsigned i=0;i<descriptors.size();++i) {
            if(cancelled(job))return wire::failure(0,Code::cancelled);
            char path[1024];
            const int count=std::snprintf(path,sizeof(path),"%s/%s",folder,wire::files[i].path);
            if(count<0||static_cast<std::size_t>(count)>=sizeof(path))return wire::failure(ENAMETOOLONG,Code::stagingError);
            const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
            if(fd<0)return wire::failure(errno,Code::stagingError);
            descriptors[i]=fd;
            struct stat info{};
            if(accountFile::checked(sceKernelFstat(fd,&info))<0)return wire::failure(errno,Code::stagingError);
            if(!S_ISREG(info.st_mode)||info.st_size<=0||static_cast<std::uint64_t>(info.st_size)>wire::files[i].limit)
                return wire::failure(EINVAL,Code::stagingError);
            lengths[i]=static_cast<std::uint64_t>(info.st_size);
            total+=static_cast<unsigned>(lengths[i]);
        }
        Result result;result.code=Code::available;return result;
    }
};
bool upload(int helper,const wire::Io& io,const Job& job) noexcept {
    std::array<std::uint8_t,4096> bytes{};
    const auto read=[&](void* p,std::size_t n){return cancelled(job)?-1:accountFile::read(helper,p,n);};
    const auto write=[&](const void* p,std::size_t n){return cancelled(job)?-1:io.write(io.context,p,n);};
    if(!wire::transfer(bytes.data(),64,read))return false;
    if(std::memcmp(bytes.data(),"\x7f" "ELF\x02\x01\x01",7))return false;
    const std::uint64_t sectionOffset=wire::get32(bytes.data()+40)|(std::uint64_t(wire::get32(bytes.data()+44))<<32);
    const auto entrySize=wire::get16(bytes.data()+58),count=wire::get16(bytes.data()+60);
    if(sectionOffset<64||entrySize!=64||!count||sectionOffset>16*1024*1024)return false;
    const auto extent=sectionOffset+std::uint64_t(entrySize)*count;
    if(extent>16*1024*1024)return false;
    if(!wire::transfer(bytes.data(),64,write))return false;
    std::uint64_t remaining=extent-64;
    while(remaining) {
        const auto capacity=remaining<bytes.size()?static_cast<std::size_t>(remaining):bytes.size();
        const auto n=read(bytes.data(),capacity);
        if(n<=0||static_cast<std::uint64_t>(n)>capacity||
           !wire::transfer(bytes.data(),static_cast<std::size_t>(n),write))return false;
        remaining-=static_cast<std::uint64_t>(n);
    }
    return read(bytes.data(),1)==0;
}
Result sendFiles(const wire::Io& io,const wire::Request& request,const Stage& stage,const Job& job) noexcept {
    wire::Frame frame;
    std::array<std::uint8_t,wire::chunkSize> bytes{};
    unsigned completed=0;
    const auto failedSend=[&]() {
        if(cancelled(job))return wire::failure(0,Code::cancelled);
        bool replied=false;
        const auto reply=wire::receiveReply(io,request,nullptr,nullptr,&replied);
        if(replied&&!reply.ok())return reply;
        return wire::failure(EIO,Code::transportError);
    };
    const auto write=[&](const void* data,std::size_t size)->std::int64_t {
        return cancelled(job)?-1:io.write(io.context,data,size);
    };
    wire::encode(request,frame);
    if(!wire::transfer(frame.bytes.data(),wire::requestSize,write))return failedSend();
    if(job.progress)job.progress(job.context,{Phase::installing,completed,stage.total});
    for(unsigned i=0;i<std::size(wire::files);++i) {
        if(cancelled(job))return wire::failure(0,Code::cancelled);
        wire::File file;file.length=stage.lengths[i];std::strcpy(file.path,wire::files[i].path);
        wire::encode(file,request.id,frame);
        if(!wire::transfer(frame.bytes.data(),wire::fileSize,write))return failedSend();
        std::uint64_t remaining=file.length;
        while(remaining) {
            if(cancelled(job))return wire::failure(0,Code::cancelled);
            const auto capacity=remaining<bytes.size()?static_cast<std::size_t>(remaining):bytes.size();
            std::int64_t n;
            do {n=accountFile::read(stage.descriptors[i],bytes.data(),capacity);}while(n<0&&errno==EINTR&&!cancelled(job));
            if(cancelled(job))return wire::failure(0,Code::cancelled);
            if(n<=0||static_cast<std::uint64_t>(n)>capacity)return wire::failure(n<0?errno:EIO,Code::stagingError);
            if(!wire::transfer(bytes.data(),static_cast<std::size_t>(n),[&](const void* data,std::size_t size) {
                const auto sent=write(data,size);
                if(sent>0&&static_cast<std::uint64_t>(sent)<=size) {
                    completed+=static_cast<unsigned>(sent);
                    if(job.progress)job.progress(job.context,{Phase::installing,completed,stage.total});
                }
                return sent;
            }))return failedSend();
            remaining-=static_cast<std::uint64_t>(n);
        }
        std::int64_t extra;
        do {extra=accountFile::read(stage.descriptors[i],bytes.data(),1);}while(extra<0&&errno==EINTR&&!cancelled(job));
        if(cancelled(job))return wire::failure(0,Code::cancelled);
        if(extra!=0)return wire::failure(extra<0?errno:EIO,Code::stagingError);
    }
    if(cancelled(job))return wire::failure(0,Code::cancelled);
    wire::header(frame,wire::Kind::end,request.id);
    if(!wire::transfer(frame.bytes.data(),wire::endSize,write))return failedSend();
    return wire::receiveReply(io,request);
}
#endif
Result submit(wire::Operation operation,const char* argument,Visit visit=nullptr,void* context=nullptr,const Job& job={}) noexcept {
    wire::Request request;
    request.operation=operation;
    request.id=nextId.fetch_add(1,std::memory_order_relaxed);
    if(!request.id)request.id=nextId.fetch_add(1,std::memory_order_relaxed);
    if(operation==wire::Operation::remove&&argument) {
        const auto length=strnlen(argument,sizeof(request.titleId));
        if(length==sizeof(request.titleId)){Result r;r.code=Code::invalidGame;r.error=EINVAL;return r;}
        std::memcpy(request.titleId,argument,length);
    }
    if(!wire::valid(request)){Result r;r.code=Code::invalidGame;r.error=EINVAL;return r;}
#ifdef OPENNOW_PS5
    if(cancelled(job))return wire::failure(0,Code::cancelled);
    Stage stage;
    if(operation==wire::Operation::install) {
        const auto result=stage.open(argument,job);
        if(!result.ok())return result;
    }
    const int helper=accountFile::open(helperPath,O_RDONLY|O_NOFOLLOW);
    if(helper<0){Result r;r.code=Code::unavailable;return r;}
    Code connectionError=Code::transportError;
    int socket=connect(&connectionError);
    Result result=wire::failure(EIO,connectionError);
    if(socket>=0) {
        const wire::Io io{&socket,readSocket,writeSocket};
        if(upload(helper,io,job))result=operation==wire::Operation::install?sendFiles(io,request,stage,job):wire::exchange(io,request,visit,context);
        else result=wire::failure(cancelled(job)?0:EIO,cancelled(job)?Code::cancelled:Code::transportError);
        sceNetSocketClose(socket);
    }
    accountFile::close(helper);return result;
#else
    (void)visit;(void)context;(void)job;
    Result r;r.code=Code::unavailable;r.error=ENOSYS;return r;
#endif
}
}
bool available() noexcept {
#ifdef OPENNOW_PS5
    const auto now=sceKernelGetProcessTime();
    bool expected=false;
    if(now>=cacheUntil.load(std::memory_order_relaxed)&&probeRunning.compare_exchange_strong(expected,true,std::memory_order_acquire)) {
        pthread_attr_t attributes;
        const int initialized=pthread_attr_init(&attributes);
        pthread_t thread;
        const bool started=initialized==0&&pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED)==0&&
            pthread_create(&thread,&attributes,probe,nullptr)==0;
        if(initialized==0)pthread_attr_destroy(&attributes);
        if(!started) {
            cacheUntil.store(now+5'000'000,std::memory_order_relaxed);
            probeRunning.store(false,std::memory_order_release);
        }
    }
    return cachedAvailable.load(std::memory_order_relaxed);
#else
    return false;
#endif
}
Result install(const char* stagedFolder,const Job& job) noexcept {return submit(wire::Operation::install,stagedFolder,nullptr,nullptr,job);}
Result list(Visit visit,void* context) noexcept {return submit(wire::Operation::list,nullptr,visit,context);}
Result remove(const char* titleId) noexcept {return submit(wire::Operation::remove,titleId);}
}
