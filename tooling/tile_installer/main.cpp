// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../src/forwarder/installer_protocol.hpp"
#include "../../src/forwarder/files.hpp"
#include "../../src/forwarder/tile_store.hpp"
#include <cstdio>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
using namespace opennow::forwarder;
namespace wire=installer::wire;
Result failure(Code code,int error) noexcept {
    Result result;result.code=code;result.error=error;return result;
}
class Upload {
public:
    explicit Upload(const Paths& paths) noexcept:root_(paths.root){}
    ~Upload(){if(created_)cleanup();}
    Upload(const Upload&)=delete;
    Upload& operator=(const Upload&)=delete;
    bool create(std::uint32_t id) noexcept {
        if(!root_||root_[0]!='/'||std::strlen(root_)>700){errno=EINVAL;return false;}
        if(!files::mkdir(root_))return false;
        for(unsigned attempt=0;attempt<32;++attempt) {
            char name[96];
            std::snprintf(name,sizeof(name),".opennow-upload-%u-%u-%u",id,static_cast<unsigned>(getpid()),attempt);
            if(!files::join(path_,root_,name)){errno=ENAMETOOLONG;return false;}
            if(opennow::accountFile::makeDirectory(path_,0700)==0){created_=true;break;}
            if(errno!=EEXIST)return false;
        }
        if(!created_){errno=EEXIST;return false;}
        char child[1024];
        for(const char* name:{"sce_sys","sce_module"}) {
            if(!files::join(child,path_,name)){errno=ENAMETOOLONG;return false;}
            if(opennow::accountFile::makeDirectory(child,0700)!=0)return false;
        }
        return files::syncDirectory(root_);
    }
    int open(const char* name) const noexcept {
        char file[1024];
        if(!files::join(file,path_,name)){errno=ENAMETOOLONG;return -1;}
        return opennow::accountFile::open(file,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    }
    bool sync() const noexcept {
        char child[1024];
        for(const char* name:{"sce_sys","sce_module"}) {
            if(!files::join(child,path_,name)){errno=ENAMETOOLONG;return false;}
            if(!files::syncDirectory(child))return false;
        }
        return files::syncDirectory(path_)&&files::syncDirectory(root_);
    }
    bool cleanup() noexcept {
        if(!created_)return true;
        if(!files::erase(path_))return false;
        created_=false;
        return files::syncDirectory(root_);
    }
    const char* path() const noexcept {return path_;}
private:
    const char* root_;
    char path_[1024]{};
    bool created_=false;
};
Result receiveInstall(const wire::Io& io,const wire::Request& request,const Paths& paths) noexcept {
    Upload upload(paths);
    int storageError=upload.create(request.id)?0:(errno?errno:EIO);
    wire::Upload incoming{request.id};
    unsigned char bytes[wire::chunkSize];
    for(;;) {
        wire::Frame frame;
        if(!wire::receive(io,frame)||wire::id(frame)!=request.id)return failure(Code::protocolError,EPROTO);
        if(wire::kind(frame)==wire::Kind::end) {
            if(!incoming.complete(frame))return failure(Code::protocolError,EPROTO);
            break;
        }
        wire::File file;
        if(!incoming.accept(frame,file))return failure(Code::protocolError,EPROTO);
        int fd=-1;
        if(!storageError) {
            fd=upload.open(file.path);
            if(fd<0)storageError=errno?errno:EIO;
        }
        bool complete=true;
        for(std::uint64_t remaining=file.length;remaining;) {
            const auto count=static_cast<std::size_t>(remaining<sizeof(bytes)?remaining:sizeof(bytes));
            if(!wire::transfer(bytes,count,[&](void* data,std::size_t size){return io.read(io.context,data,size);})) {
                complete=false;break;
            }
            remaining-=count;
            if(!storageError&&!files::write(&fd,bytes,count))storageError=errno?errno:EIO;
        }
        if(fd>=0) {
            if(!storageError&&complete&&opennow::accountFile::sync(fd)!=0)storageError=errno?errno:EIO;
            if(opennow::accountFile::close(fd)!=0&&!storageError)storageError=errno?errno:EIO;
        }
        if(!complete)return failure(Code::protocolError,EPROTO);
    }
    if(!storageError&&!upload.sync())storageError=errno?errno:EIO;
    Result result;
    if(storageError)result=failure(Code::uploadError,storageError);
    else {
        result=store::install(upload.path(),paths);
        if(result.code==Code::invalidGame)result.code=Code::protocolError;
    }
    if(!upload.cleanup())return failure(Code::uploadError,errno?errno:EIO);
    return result;
}
struct Listing {
    const wire::Io& io;
    std::uint32_t id;
    unsigned count=0;
    int error=0;
    bool connected=true;
};
bool emit(void* context,const Entry& entry) noexcept {
    auto& listing=*static_cast<Listing*>(context);
    if(listing.count==wire::maxEntries){listing.error=EOVERFLOW;return false;}
    wire::Frame frame;
    if(!wire::encode(entry,listing.id,frame)){listing.error=EPROTO;return false;}
    if(!wire::send(listing.io,frame)){listing.connected=false;return false;}
    ++listing.count;return true;
}
Result execute(const wire::Request& request,const wire::Io& io,const Paths& paths,bool& connected) noexcept {
    switch(request.operation) {
    case wire::Operation::install:return receiveInstall(io,request,paths);
    case wire::Operation::list: {
        Listing listing{io,request.id};
        auto result=store::list(emit,&listing,paths);
        connected=listing.connected;
        if(listing.error)result=wire::failure(listing.error);
        result.count=listing.count;return result;
    }
    case wire::Operation::remove:return store::remove(request.titleId,paths);
    }
    return wire::failure(EINVAL);
}
bool serve(const wire::Io& io,const Paths& paths={}) noexcept {
    wire::Frame frame;wire::Request request;
    if(!wire::receive(io,frame)||!wire::decode(frame,request))return false;
    bool connected=true;
    auto result=execute(request,io,paths,connected);
    if(result.code==Code::unavailable)result.code=Code::ioError;
    return connected&&wire::encode(result,request.id,frame)&&wire::send(io,frame);
}
std::int64_t readInput(void*,void* bytes,std::size_t size) noexcept {return read(STDIN_FILENO,bytes,size);}
std::int64_t writeOutput(void*,const void* bytes,std::size_t size) noexcept {return write(STDOUT_FILENO,bytes,size);}
}
int main() {
    const timeval timeout{installer::wire::ioTimeoutUs/1'000'000,installer::wire::ioTimeoutUs%1'000'000};
    if(setsockopt(STDIN_FILENO,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))!=0||
       setsockopt(STDOUT_FILENO,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))!=0)return 1;
    return serve({nullptr,readInput,writeOutput})?0:1;
}
