// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder/image.hpp"
#include <algorithm>
#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>
#define main installer_payload_main
#include "../tooling/tile_installer/main.cpp"
#undef main

namespace streamTest {
namespace fs=std::filesystem;
using namespace opennow;
using namespace opennow::forwarder;
namespace wire=installer::wire;
constexpr std::uint32_t requestId=0x12345678;

std::string readFile(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);assert(file.good());
    return {std::istreambuf_iterator<char>(file),{}};
}
void writeFile(const fs::path& path,const std::string& bytes) {
    std::ofstream file(path,std::ios::binary);file.write(bytes.data(),bytes.size());assert(file.good());
}
void equalFiles(const fs::path& first,const fs::path& second) {
    std::ifstream a(first,std::ios::binary),b(second,std::ios::binary);assert(a.good()&&b.good());
    char left[wire::chunkSize],right[wire::chunkSize];
    while(a&&b) {
        a.read(left,sizeof(left));b.read(right,sizeof(right));
        assert(a.gcount()==b.gcount()&&!std::memcmp(left,right,static_cast<std::size_t>(a.gcount())));
    }
    assert(a.eof()&&b.eof());
}
struct Fixture {
    fs::path base,root,source,collision;
    std::string rootText,collisionText;
    const char* collisions[1]{};
    Paths paths;
    Game game;
    char titleId[10]{};
    Fixture() {
        char temporary[]="/tmp/opennow-installer-stream-XXXXXX";assert(mkdtemp(temporary));base=temporary;
        root=base/"homebrew";source=base/"source";collision=base/"appmeta";
        fs::create_directory(root);fs::create_directory(collision);
        fs::create_directories(source/"sce_sys");fs::create_directory(source/"sce_module");
        assert(store::verifyTemplate("assets/forwarder-template"));
        fs::copy_file("assets/forwarder-template/eboot.bin",source/"eboot.bin");
        fs::copy_file("assets/forwarder-template/sce_module/libc.prx",source/"sce_module/libc.prx");
        std::strcpy(game.id,"12345");std::strcpy(game.store,"XBOX");std::strcpy(game.title,"Stream fixture \"game\"");
        store::initialTitleId(game,titleId);
        char text[4096];
        assert(store::forwarderJson(game,text,sizeof(text)));writeFile(source/"forwarder.json",text);
        assert(store::paramJson(game,titleId,text,sizeof(text)));writeFile(source/"sce_sys/param.json",text);
        std::vector<std::uint32_t> pixels(512*512);
        for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x)
            pixels[y*512+x]=0xff000000U|(x&255)|((y&255)<<8)|(((x+y)&255)<<16);
        int icon=accountFile::open((source/"sce_sys/icon0.png").c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
        assert(icon>=0&&files::finish(icon,encodeIconPng(pixels.data(),512,512,files::write,&icon)));
        int background=accountFile::open((source/"sce_sys/pic0.dds").c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
        assert(background>=0&&files::finish(background,encodeBackgroundDds(pixels.data(),512,512,files::write,&background)));
        assert(fs::file_size(source/"sce_sys/pic0.dds")==3840*2160+148);
        fs::copy_file(source/"sce_sys/pic0.dds",source/"sce_sys/pic1.dds");
        rootText=root.string();collisionText=collision.string();collisions[0]=collisionText.c_str();
        paths.root=rootText.c_str();paths.collisionRoots=collisions;paths.collisionRootCount=1;
    }
    ~Fixture(){fs::remove_all(base);}
    void empty() const {assert(fs::is_empty(root));}
    void clean() const {
        for(const auto& entry:fs::directory_iterator(root))
            assert(entry.path().filename().string().rfind(".opennow-",0)!=0);
    }
};

struct Endpoint {
    int fd=-1;
    std::size_t received=0,largestRead=0;
    static std::int64_t read(void* context,void* bytes,std::size_t count) noexcept {
        auto& endpoint=*static_cast<Endpoint*>(context);
        endpoint.largestRead=std::max(endpoint.largestRead,count);
        ssize_t n;
        do {n=recv(endpoint.fd,bytes,std::min(count,std::size_t{8191}),0);} while(n<0&&errno==EINTR);
        if(n>0)endpoint.received+=static_cast<std::size_t>(n);
        return n;
    }
    static std::int64_t write(void* context,const void* bytes,std::size_t count) noexcept {
        const auto& endpoint=*static_cast<Endpoint*>(context);
        ssize_t n;
        do {n=send(endpoint.fd,bytes,std::min(count,std::size_t{12289}),MSG_NOSIGNAL);} while(n<0&&errno==EINTR);
        return n;
    }
    wire::Io io() noexcept {return {this,read,write};}
};
struct Connection {
    Endpoint client,server;
    std::thread worker;
    bool served=false;
    explicit Connection(const Paths& paths) {
        int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
        client.fd=sockets[0];server.fd=sockets[1];
        const timeval timeout{5,0};
        for(const int fd:sockets) {
            assert(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
            assert(setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))==0);
        }
        worker=std::thread([this,paths] {
            served=serve(server.io(),paths);assert(close(server.fd)==0);
        });
    }
    ~Connection(){disconnect();finish();}
    void disconnect() {
        if(client.fd>=0){shutdown(client.fd,SHUT_RDWR);assert(close(client.fd)==0);client.fd=-1;}
    }
    void finish(){if(worker.joinable())worker.join();}
    void halfClose(){assert(shutdown(client.fd,SHUT_WR)==0);}
    void bytes(const void* data,std::size_t count) {
        assert(wire::transfer(static_cast<const unsigned char*>(data),count,[&](const void* p,std::size_t n) {
            return Endpoint::write(&client,p,n);
        }));
    }
    void frame(const wire::Frame& value){bytes(value.bytes.data(),wire::size(wire::kind(value)));}
};

wire::Request request(wire::Operation operation=wire::Operation::install,const char* titleId=nullptr) {
    wire::Request value;value.id=requestId;value.operation=operation;
    if(titleId)std::snprintf(value.titleId,sizeof(value.titleId),"%s",titleId);
    return value;
}
wire::Frame fileFrame(unsigned index,std::uint64_t length=1) {
    wire::File file;file.length=length;std::strcpy(file.path,wire::files[index].path);
    wire::Frame frame;assert(wire::encode(file,requestId,frame));return frame;
}
void begin(Connection& connection,const wire::Request& value=request()) {
    wire::Frame frame;assert(wire::encode(value,frame));connection.frame(frame);
}
void end(Connection& connection,std::uint32_t id=requestId) {
    wire::Frame frame;wire::header(frame,wire::Kind::end,id);connection.frame(frame);
}
Result reply(Connection& connection,const wire::Request& value=request(),Visit visit=nullptr,void* context=nullptr) {
    const auto result=wire::receiveReply(connection.client.io(),value,visit,context);
    connection.finish();assert(connection.served);return result;
}
void protocolError(Connection& connection,const Fixture& fixture) {
    connection.halfClose();const auto result=reply(connection);
    assert(result.code==Code::protocolError&&result.error==EPROTO);fixture.empty();
}
void upload(Connection& connection,const Fixture& fixture) {
    begin(connection);
    unsigned char bytes[wire::chunkSize];
    for(unsigned i=0;i<std::size(wire::files);++i) {
        const auto path=fixture.source/wire::files[i].path;
        connection.frame(fileFrame(i,fs::file_size(path)));
        std::ifstream input(path,std::ios::binary);assert(input.good());
        while(input) {
            input.read(reinterpret_cast<char*>(bytes),sizeof(bytes));
            if(input.gcount())connection.bytes(bytes,static_cast<std::size_t>(input.gcount()));
        }
        assert(input.eof());
    }
    end(connection);
}
std::size_t uploadSize(const Fixture& fixture) {
    std::size_t size=wire::requestSize+std::size(wire::files)*wire::fileSize+wire::endSize;
    for(const auto& spec:wire::files)size+=fs::file_size(fixture.source/spec.path);
    return size;
}
bool collect(void* context,const Entry& entry) noexcept {
    static_cast<std::vector<Entry>*>(context)->push_back(entry);return true;
}
Result command(const Fixture& fixture,wire::Operation operation,const char* titleId=nullptr,std::vector<Entry>* entries=nullptr) {
    Connection connection(fixture.paths);const auto value=request(operation,titleId);begin(connection,value);
    return reply(connection,value,entries?collect:nullptr,entries);
}

void lifecycle(Fixture& fixture) {
    assert(command(fixture,wire::Operation::list).count==0);
    Connection first(fixture.paths);upload(first,fixture);const auto created=reply(first);
    assert(created.code==Code::created&&!std::strcmp(created.titleId,fixture.titleId));
    assert(first.server.received==uploadSize(fixture)&&first.server.largestRead==wire::chunkSize);fixture.clean();
    for(const auto& spec:wire::files)
        equalFiles(fixture.root/created.titleId/spec.path,fixture.source/spec.path);
    const auto metadata=fixture.root/created.titleId/"forwarder.json";
    const std::string preserved=R"({"target":"PPSA99082","args":["--gfn-app-id","12345"],"creator":"External manager"})";
    writeFile(metadata,preserved);
    Connection second(fixture.paths);upload(second,fixture);const auto existing=reply(second);
    assert(existing.code==Code::alreadyExists&&!std::strcmp(created.titleId,existing.titleId));
    assert(readFile(metadata)==preserved);fixture.clean();
    std::vector<Entry> entries;
    const auto listed=command(fixture,wire::Operation::list,nullptr,&entries);
    assert(listed.code==Code::listed&&listed.count==1&&entries.size()==1);
    assert(!std::strcmp(entries[0].titleId,created.titleId)&&!std::strcmp(entries[0].game.id,fixture.game.id));
    assert(command(fixture,wire::Operation::remove,created.titleId).code==Code::removed);fixture.empty();
    assert(command(fixture,wire::Operation::remove,created.titleId).code==Code::notFound);

    fs::create_directory(fixture.collision/fixture.titleId);
    Connection collided(fixture.paths);upload(collided,fixture);const auto allocated=reply(collided);
    assert(allocated.code==Code::created&&std::strcmp(allocated.titleId,fixture.titleId));fixture.clean();
    assert(command(fixture,wire::Operation::remove,allocated.titleId).code==Code::removed);
    fs::remove(fixture.collision/fixture.titleId);fixture.empty();
    fs::create_directory(fixture.root/fixture.titleId);writeFile(fixture.root/fixture.titleId/"foreign","preserve");
    assert(command(fixture,wire::Operation::remove,fixture.titleId).code==Code::conflict);
    assert(readFile(fixture.root/fixture.titleId/"foreign")=="preserve");
    fs::remove_all(fixture.root/fixture.titleId);fixture.empty();
}

void malformed(Fixture& fixture) {
    for(unsigned field=0;field<15;++field) {
        Connection connection(fixture.paths);begin(connection);auto frame=fileFrame(0);
        auto* p=frame.bytes.data();
        switch(field) {
        case 0:std::memset(p+24,0,40);std::memcpy(p+24,"foreign",7);break;
        case 1:std::memset(p+24,0,40);std::memcpy(p+24,"../eboot.bin",12);break;
        case 2:std::memset(p+24,0,40);std::memcpy(p+24,"/eboot.bin",10);break;
        case 3:std::memset(p+24,0,40);std::memcpy(p+24,"sce_sys/../eboot.bin",20);break;
        case 4:std::memset(p+24,'a',40);break;
        case 5:p[63]=1;break;
        case 6:wire::put64(p+16,0);break;
        case 7:wire::put64(p+16,wire::files[0].limit+1);break;
        case 8:wire::put64(p+16,UINT64_MAX);break;
        case 9:wire::put32(p+12,requestId+1);break;
        case 10:p[0]='X';break;
        case 11:wire::put16(p+4,1);break;
        case 12:wire::put32(p+8,wire::fileSize+1);break;
        case 13:wire::put32(p+8,wire::fileSize-1);break;
        case 14:wire::put16(p+6,99);break;
        }
        const bool validHeader=wire::validHeader(frame);
        connection.bytes(p,validHeader?wire::fileSize:wire::headerSize);protocolError(connection,fixture);
        assert(connection.server.received==wire::requestSize+(validHeader?wire::fileSize:wire::headerSize));
    }
    for(unsigned index=0;index<std::size(wire::files);++index) {
        Connection duplicate(fixture.paths);begin(duplicate);duplicate.frame(fileFrame(index));duplicate.bytes("x",1);
        duplicate.frame(fileFrame(index));protocolError(duplicate,fixture);
        Connection missing(fixture.paths);begin(missing);
        for(unsigned other=0;other<std::size(wire::files);++other)if(other!=index) {
            missing.frame(fileFrame(other));missing.bytes("x",1);
        }
        end(missing);protocolError(missing,fixture);
        Connection tooLarge(fixture.paths);begin(tooLarge);auto frame=fileFrame(index);
        wire::put64(frame.bytes.data()+16,wire::files[index].limit+1);tooLarge.frame(frame);protocolError(tooLarge,fixture);
    }
    Connection wrongEnd(fixture.paths);begin(wrongEnd);
    for(unsigned i=0;i<std::size(wire::files);++i){wrongEnd.frame(fileFrame(i));wrongEnd.bytes("x",1);}
    end(wrongEnd,requestId+1);protocolError(wrongEnd,fixture);
    Connection wrongKind(fixture.paths);begin(wrongKind);begin(wrongKind);protocolError(wrongKind,fixture);
}

void disconnects(Fixture& fixture) {
    wire::Frame requestFrame;assert(wire::encode(request(),requestFrame));
    for(const std::size_t cut:{std::size_t{0},wire::headerSize-1,wire::requestSize-1}) {
        Connection connection(fixture.paths);connection.bytes(requestFrame.bytes.data(),cut);
        connection.disconnect();connection.finish();assert(!connection.served);fixture.empty();
    }
    const auto frame=fileFrame(0);
    for(std::size_t cut=0;cut<wire::fileSize;++cut) {
        Connection connection(fixture.paths);begin(connection);connection.bytes(frame.bytes.data(),cut);
        protocolError(connection,fixture);
    }
    for(unsigned index=0;index<std::size(wire::files);++index) {
        Connection connection(fixture.paths);begin(connection);connection.frame(fileFrame(index,3));connection.bytes("xx",2);
        protocolError(connection,fixture);
    }
    Connection noEnd(fixture.paths);begin(noEnd);
    for(unsigned index=0;index<std::size(wire::files);++index){noEnd.frame(fileFrame(index));noEnd.bytes("x",1);}
    protocolError(noEnd,fixture);
    Connection disconnected(fixture.paths);begin(disconnected);disconnected.frame(fileFrame(0,wire::chunkSize+1));
    const std::string partial(wire::chunkSize,'x');disconnected.bytes(partial.data(),partial.size());
    disconnected.disconnect();disconnected.finish();assert(!disconnected.served);fixture.empty();

    const auto reserved=fixture.root/(".opennow-upload-"+std::to_string(requestId)+"-"+std::to_string(getpid())+"-0");
    fs::create_directory(reserved);writeFile(reserved/"foreign","never delete");
    Connection exclusive(fixture.paths);begin(exclusive);end(exclusive);exclusive.halfClose();
    assert(reply(exclusive).code==Code::protocolError);
    assert(readFile(reserved/"foreign")=="never delete"&&std::distance(fs::directory_iterator(fixture.root),fs::directory_iterator{})==1);
    fs::remove_all(reserved);fixture.empty();
}

void storageFailures(Fixture& fixture) {
    const auto blocked=fixture.base/"blocked";writeFile(blocked,"not a directory");const auto blockedText=blocked.string();
    auto paths=fixture.paths;paths.root=blockedText.c_str();
    Connection unavailable(paths);upload(unavailable,fixture);const auto failed=reply(unavailable);
    assert(failed.code==Code::uploadError&&failed.error==ENOTDIR);
    assert(unavailable.server.received==uploadSize(fixture)&&readFile(blocked)=="not a directory");fixture.empty();
    for(const auto operation:{wire::Operation::list,wire::Operation::remove}) {
        Connection destination(paths);const auto value=request(operation,operation==wire::Operation::remove?fixture.titleId:nullptr);
        begin(destination,value);const auto result=reply(destination,value);
        assert(result.code==Code::ioError&&result.error==ENOTDIR);
    }
    Connection bounded(paths);begin(bounded);auto oversized=fileFrame(0);
    wire::put64(oversized.bytes.data()+16,UINT64_MAX);bounded.frame(oversized);bounded.halfClose();
    assert(reply(bounded).code==Code::protocolError);
    assert(bounded.server.received==wire::requestSize+wire::fileSize);

    struct rlimit oldLimit;assert(getrlimit(RLIMIT_FSIZE,&oldLimit)==0);
    auto limited=oldLimit;limited.rlim_cur=wire::chunkSize;
    const auto oldSignal=std::signal(SIGXFSZ,SIG_IGN);assert(setrlimit(RLIMIT_FSIZE,&limited)==0);
    Connection unwritable(fixture.paths);upload(unwritable,fixture);const auto writeFailure=reply(unwritable);
    assert(setrlimit(RLIMIT_FSIZE,&oldLimit)==0);std::signal(SIGXFSZ,oldSignal);
    assert(writeFailure.code==Code::uploadError&&writeFailure.error==EFBIG);
    assert(unwritable.server.received==uploadSize(fixture));fixture.empty();
}

void rejectedContents(Fixture& fixture) {
    const auto metadata=fixture.source/"forwarder.json";const auto original=readFile(metadata);
    writeFile(metadata,R"({"target":"PPSA00001","args":["--gfn-app-id","12345"]})");
    Connection foreign(fixture.paths);upload(foreign,fixture);assert(reply(foreign).code==Code::protocolError);fixture.empty();
    writeFile(metadata,original);
    const auto program=fixture.source/"eboot.bin";const auto pinned=readFile(program);auto corrupt=pinned;corrupt[0]^=1;
    writeFile(program,corrupt);
    Connection executable(fixture.paths);upload(executable,fixture);assert(reply(executable).code==Code::templateError);fixture.empty();
    writeFile(program,pinned);
}
}

int main() {
    using namespace streamTest;
    Fixture fixture;lifecycle(fixture);malformed(fixture);disconnects(fixture);storageFailures(fixture);rejectedContents(fixture);
    std::puts("installer stream tests passed: real dispatcher/store, bounded uploads, idempotence, list/remove, malformed frames, disconnect cleanup and drained write failures");
}
