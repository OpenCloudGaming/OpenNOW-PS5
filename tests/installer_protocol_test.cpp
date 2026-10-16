// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>
#define OPENNOW_PS5 1
#include "../src/forwarder/installer_client.cpp"

namespace test {
using namespace opennow::forwarder;
namespace wire=installer::wire;
struct Buffer {
    std::vector<std::uint8_t> input,output;
    std::size_t offset=0,readChunk=3,writeChunk=7,writeLimit=SIZE_MAX;
    static std::int64_t read(void* context,void* bytes,std::size_t size) noexcept {
        auto& b=*static_cast<Buffer*>(context);
        const auto n=std::min({size,b.readChunk,b.input.size()-b.offset});
        if(n)std::memcpy(bytes,b.input.data()+b.offset,n);
        b.offset+=n;return static_cast<std::int64_t>(n);
    }
    static std::int64_t write(void* context,const void* bytes,std::size_t size) noexcept {
        auto& b=*static_cast<Buffer*>(context);
        if(b.output.size()>=b.writeLimit)return -1;
        const auto n=std::min({size,b.writeChunk,b.writeLimit-b.output.size()});
        const auto* p=static_cast<const std::uint8_t*>(bytes);
        b.output.insert(b.output.end(),p,p+n);return static_cast<std::int64_t>(n);
    }
    wire::Io io() noexcept {return {this,read,write};}
    void append(const wire::Frame& f) {input.insert(input.end(),f.bytes.begin(),f.bytes.begin()+wire::size(wire::kind(f)));}
};
wire::Request request(wire::Operation operation=wire::Operation::list,std::uint32_t id=0x12345678) {
    wire::Request r;r.operation=operation;r.id=id;
    if(operation==wire::Operation::remove)std::strcpy(r.titleId,"PPSA99500");
    return r;
}
Entry entry(unsigned index=0) {
    Entry e;std::snprintf(e.titleId,sizeof(e.titleId),"CUSA%05u",10000+index);
    std::strcpy(e.game.id,"123");std::strcpy(e.game.title,"Test game");std::strcpy(e.game.store,"Steam");
    return e;
}
Result result(Code code=Code::listed) {Result r;r.code=code;return r;}
void appendResult(Buffer& b,const Result& r,std::uint32_t id=0x12345678) {
    wire::Frame f;assert(wire::encode(r,id,f));b.append(f);
}
void appendEntry(Buffer& b,const Entry& e,std::uint32_t id=0x12345678) {
    wire::Frame f;assert(wire::encode(e,id,f));b.append(f);
}
wire::File file(unsigned index,std::uint64_t length=1) {
    wire::File f;f.length=length;std::strcpy(f.path,wire::files[index].path);return f;
}
unsigned visits=0;
bool cancelVisit(void*,const Entry&) noexcept {++visits;return false;}
bool visit(void*,const Entry& e) noexcept {assert(wire::valid(e));++visits;return true;}

std::vector<std::uint8_t> helper,sent,replies;
std::array<std::vector<std::uint8_t>,std::size(wire::files)> staged;
std::array<std::size_t,std::size(wire::files)> offsets{},statSizes{};
std::size_t helperOffset=0,replyOffset=0,sendLimit=SIZE_MAX,sendChunk=3001,stageReadChunk=wire::chunkSize,maxStageRead=0;
std::size_t readFailureOffset=0,stopProgress=SIZE_MAX,cancelAfterSend=SIZE_MAX;
int missingFile=-1,statFailure=-1,readFailure=-1,nonRegular=-1,optionFailure=0,connectResult=0;
bool readEof=false;
std::atomic_bool helperPresent{true},connectBlocked{false},cancelUpload{false};
std::atomic_uint opens{0},closes{0},sockets{0},socketCloses{0},connects{0},options{0},credentialWrites{0},stageOpens{0},stageCloses{0};
std::atomic_uint64_t clock{1};
std::vector<Progress> progress;
constexpr char stagedFolder[]="/sandbox/app-local-tile-stage/job-1";
void makeHelper() {
    helper.assign(128,0);std::memcpy(helper.data(),"\x7f" "ELF\x02\x01\x01",7);
    wire::put64(helper.data()+40,64);wire::put16(helper.data()+58,64);wire::put16(helper.data()+60,1);
}
void resetNative(Code code=Code::created) {
    assert(!installer::probeRunning.load());
    helperPresent=true;helperOffset=replyOffset=0;sent.clear();replies.clear();optionFailure=connectResult=0;
    missingFile=statFailure=readFailure=nonRegular=-1;readFailureOffset=0;readEof=false;
    sendLimit=stopProgress=cancelAfterSend=SIZE_MAX;sendChunk=3001;stageReadChunk=wire::chunkSize;maxStageRead=0;
    cancelUpload=false;progress.clear();offsets.fill(0);makeHelper();
    const unsigned sizes[]={2*wire::chunkSize+17,wire::chunkSize+3,11,wire::chunkSize+1,2*wire::chunkSize+5,2*wire::chunkSize+7,31};
    for(unsigned i=0;i<staged.size();++i) {
        staged[i].resize(sizes[i]);statSizes[i]=sizes[i];
        for(unsigned j=0;j<sizes[i];++j)staged[i][j]=static_cast<std::uint8_t>(i*19+j);
    }
    auto r=result(code);if(code==Code::created||code==Code::alreadyExists||code==Code::removed)std::strcpy(r.titleId,"PPSA99500");
    wire::Frame f;assert(wire::encode(r,installer::nextId.load(),f));
    replies.assign(f.bytes.begin(),f.bytes.begin()+wire::resultSize);
}
void finishProbe() {
    for(unsigned i=0;i<5000&&installer::probeRunning.load(std::memory_order_acquire);++i)usleep(1000);
    assert(!installer::probeRunning.load(std::memory_order_acquire));
}
unsigned sentData() {
    std::size_t at=helper.size()+wire::requestSize;
    unsigned total=0;
    for(unsigned i=0;i<staged.size();++i) {
        if(sent.size()<at+wire::fileSize)break;
        assert(wire::get16(sent.data()+at+6)==static_cast<unsigned>(wire::Kind::file));
        const auto length=wire::get64(sent.data()+at+16);at+=wire::fileSize;
        const auto n=std::min(static_cast<std::size_t>(length),sent.size()-at);
        total+=static_cast<unsigned>(n);at+=n;
        if(n<length)break;
    }
    return total;
}
void onProgress(void*,const Progress& p) noexcept {
    assert(p.phase==Phase::installing&&p.completed==sentData());
    unsigned total=0;for(const auto n:statSizes)total+=static_cast<unsigned>(n);
    assert(p.total==total&&p.completed<=p.total);
    if(!progress.empty())assert(p.completed>progress.back().completed);
    progress.push_back(p);
    if(p.completed>=stopProgress)cancelUpload=true;
}
Job job() {return {&cancelUpload,onProgress,nullptr};}
void checkUpload(std::uint32_t id) {
    assert(std::equal(helper.begin(),helper.end(),sent.begin()));
    Buffer b;b.input.assign(sent.begin()+helper.size(),sent.end());
    wire::Frame frame;wire::Request decoded;
    assert(wire::receive(b.io(),frame)&&wire::decode(frame,decoded));
    assert(decoded.operation==wire::Operation::install&&decoded.id==id&&!decoded.titleId[0]);
    wire::Upload upload{id};
    for(unsigned i=0;i<staged.size();++i) {
        wire::File f;assert(wire::receive(b.io(),frame)&&upload.accept(frame,f));
        assert(!std::strcmp(f.path,wire::files[i].path)&&f.length==staged[i].size());
        assert(std::equal(staged[i].begin(),staged[i].end(),b.input.begin()+static_cast<std::ptrdiff_t>(b.offset)));
        b.offset+=staged[i].size();
    }
    assert(wire::receive(b.io(),frame)&&upload.complete(frame)&&b.offset==b.input.size());
    assert(!progress.empty()&&progress.front().completed==0&&progress.back().completed==progress.back().total);
    assert(maxStageRead==wire::chunkSize);
    assert(std::search(sent.begin(),sent.end(),stagedFolder,stagedFolder+std::strlen(stagedFolder))==sent.end());
}

void protocolTests() {
    wire::Frame frame;
    auto req=request(wire::Operation::install);
    assert(wire::encode(req,frame));
    const std::uint8_t golden[]={'O','N','T','I',2,0,1,0,32,0,0,0,0x78,0x56,0x34,0x12,1,0,0,0};
    assert(!std::memcmp(frame.bytes.data(),golden,sizeof(golden))&&wire::zero(frame.bytes.data()+20,12));
    assert(wire::encode(file(0,0x010203),req.id,frame));
    const std::uint8_t fileGolden[]={'O','N','T','I',2,0,4,0,64,0,0,0,0x78,0x56,0x34,0x12,3,2,1,0,0,0,0,0};
    assert(!std::memcmp(frame.bytes.data(),fileGolden,sizeof(fileGolden)));
    assert(!std::memcmp(frame.bytes.data()+24,"eboot.bin",10)&&wire::zero(frame.bytes.data()+34,30));
    wire::put64(frame.bytes.data(),UINT64_MAX);assert(wire::get64(frame.bytes.data())==UINT64_MAX);
    for(const char* title:{"","PPSA99082","PPSA99100","PPSA99203","PPSA1234","PPSA123456","PPSA12x45","/PPSA99500","XXXX99500"})assert(!wire::validTitleId(title));
    assert(wire::validTitleId("PPSA99500")&&wire::validTitleId("CUSA12345")&&wire::validTitleId("LAPY12345"));
    auto bad=req;bad.id=0;assert(!wire::valid(bad));bad=req;bad.operation=static_cast<wire::Operation>(4);assert(!wire::valid(bad));
    for(const auto op:{wire::Operation::install,wire::Operation::list}) {
        bad=request(op);bad.titleId[0]='a';assert(!wire::valid(bad));
    }
    for(const auto code:wire::codes) {
        auto value=result(code);value.error=EACCES;
        assert(wire::encode(value,req.id,frame));Result decoded;assert(wire::decode(frame,decoded));
        assert(decoded.code==code&&decoded.error==EACCES);
    }
    for(unsigned field=0;field<11;++field) {
        assert(wire::encode(req,frame));
        switch(field) {
        case 0:frame.bytes[0]=0;break;
        case 1:wire::put16(frame.bytes.data()+4,1);break;
        case 2:wire::put16(frame.bytes.data()+6,6);break;
        case 3:wire::put32(frame.bytes.data()+8,wire::entrySize+1);break;
        case 4:wire::put32(frame.bytes.data()+8,wire::requestSize-1);break;
        case 5:wire::put32(frame.bytes.data()+12,0);break;
        case 6:wire::put32(frame.bytes.data()+16,4);break;
        case 7:frame.bytes[20]='x';break;
        case 8:std::memset(frame.bytes.data()+20,'a',10);break;
        case 9:frame.bytes[30]=1;break;
        case 10:frame.bytes[31]=1;break;
        }
        wire::Request decoded;assert(!wire::decode(frame,decoded));
    }
    for(const auto k:{wire::Kind::request,wire::Kind::file,wire::Kind::end,wire::Kind::result,wire::Kind::entry}) {
        wire::header(frame,k,req.id);Buffer full;full.append(frame);
        for(std::size_t cut=0;cut<full.input.size();++cut) {
            Buffer b;b.input.assign(full.input.begin(),full.input.begin()+static_cast<std::ptrdiff_t>(cut));
            assert(!wire::receive(b.io(),frame));
        }
    }
    wire::Upload upload{req.id};wire::Frame end;wire::header(end,wire::Kind::end,req.id);
    assert(!upload.complete(end));
    for(unsigned i=0;i<std::size(wire::files);++i) {
        auto f=file(i,wire::files[i].limit);assert(wire::valid(f)&&wire::encode(f,req.id,frame));
        assert(wire::get64(frame.bytes.data()+16)==wire::files[i].limit);
        assert(!std::strcmp(reinterpret_cast<const char*>(frame.bytes.data()+24),f.path));
        wire::File decoded;assert(wire::decode(frame,decoded)&&decoded.length==f.length&&!std::strcmp(decoded.path,f.path));
        auto wrong=frame;wire::put32(wrong.bytes.data()+12,req.id+1);assert(!upload.accept(wrong,decoded));
        assert(upload.accept(frame,decoded)&&!upload.accept(frame,decoded));
        if(i+1<std::size(wire::files))assert(!upload.complete(end));
        for(const auto length:{std::uint64_t{0},wire::files[i].limit+1,UINT64_MAX}) {
            f.length=length;assert(!wire::valid(f)&&!wire::encode(f,req.id,wrong));
            wire::put64(frame.bytes.data()+16,length);assert(!wire::decode(frame,decoded));
        }
    }
    assert(upload.complete(end));wire::put32(end.bytes.data()+12,req.id+1);assert(!upload.complete(end));
    for(const char* path:{"",".","..","/eboot.bin","../eboot.bin","sce_sys/../eboot.bin","sce_sys//icon0.png","sce_sys/icon0.png/","unknown","sce_sys\\icon0.png"}) {
        auto f=file(0);std::strcpy(f.path,path);assert(!wire::valid(f)&&!wire::encode(f,req.id,frame));
    }
    assert(wire::encode(file(0),req.id,frame));frame.bytes[63]=1;wire::File decoded;assert(!wire::decode(frame,decoded));
    std::memset(frame.bytes.data()+24,'a',40);assert(!wire::decode(frame,decoded));
    Buffer entries;appendEntry(entries,entry());appendEntry(entries,entry(1));auto listed=result();listed.count=2;appendResult(entries,listed);
    const auto complete=entries.input;const auto listing=request();visits=0;
    auto value=wire::exchange(entries.io(),listing,cancelVisit);assert(value.code==Code::cancelled&&value.count==1&&visits==1&&entries.offset==complete.size());
    for(std::size_t cut=0;cut<complete.size();++cut) {
        Buffer b;b.input.assign(complete.begin(),complete.begin()+static_cast<std::ptrdiff_t>(cut));assert(!wire::exchange(b.io(),listing).ok());
    }
    Buffer duplicate;appendEntry(duplicate,entry());appendEntry(duplicate,entry());appendResult(duplicate,listed);
    assert(wire::exchange(duplicate.io(),listing).code==Code::protocolError);
    Buffer mismatch;appendResult(mismatch,listed,listing.id+1);assert(wire::exchange(mismatch.io(),listing).code==Code::protocolError);
    Buffer wrongCount;appendResult(wrongCount,listed);assert(wire::exchange(wrongCount.io(),listing).code==Code::protocolError);
    for(const auto code:{Code::uploadError,Code::protocolError,Code::conflict}) {
        Buffer remote;auto failure=result(code);failure.error=EACCES;appendResult(remote,failure);remote.writeLimit=10;
        value=wire::exchange(remote.io(),req);assert(value.code==code&&value.error==EACCES);
    }
    Buffer writeFailure;writeFailure.writeLimit=10;assert(wire::exchange(writeFailure.io(),req).code==Code::transportError);
    Buffer tooMany;for(unsigned i=0;i<=wire::maxEntries;++i)appendEntry(tooMany,entry(i));
    assert(wire::exchange(tooMany.io(),listing).code==Code::protocolError);
    for(unsigned field=0;field<6;++field) {
        assert(wire::encode(result(),listing.id,frame));
        switch(field) {
        case 0:wire::put32(frame.bytes.data()+16,99);break;
        case 1:wire::put32(frame.bytes.data()+20,UINT32_MAX);break;
        case 2:wire::put32(frame.bytes.data()+24,wire::maxEntries+1);break;
        case 3:std::memcpy(frame.bytes.data()+28,"PPSA99082",10);break;
        case 4:frame.bytes[38]=1;break;
        case 5:std::memset(frame.bytes.data()+28,'a',10);break;
        }
        Buffer b;b.append(frame);assert(wire::exchange(b.io(),listing).code==Code::protocolError);
    }
    for(unsigned field=0;field<4;++field) {
        assert(wire::encode(entry(),listing.id,frame));
        if(field==0)std::memcpy(frame.bytes.data()+16,"PPSA99082",10);
        if(field==1)frame.bytes[650]=2;
        if(field==2)frame.bytes[652]=1;
        if(field==3)std::memset(frame.bytes.data()+26,'a',96);
        Buffer b;b.append(frame);assert(wire::exchange(b.io(),listing).code==Code::protocolError);
    }
    for(const char* id:{"abc","0","-1","+1","1e3","2147483648","99999999999999999999999999999999999999"}) {
        assert(wire::encode(entry(),listing.id,frame));
        std::memset(frame.bytes.data()+26,0,96);std::memcpy(frame.bytes.data()+26,id,std::strlen(id));
        Entry invalid;assert(!wire::decode(frame,invalid));
        Buffer b;b.append(frame);visits=0;
        assert(wire::exchange(b.io(),listing,visit).code==Code::protocolError&&visits==0);
    }
    for(const char control:{'\n','\t','\x1f','\x7f'}) {
        assert(wire::encode(entry(),listing.id,frame));frame.bytes[282]=static_cast<std::uint8_t>(control);
        Entry invalid;assert(!wire::decode(frame,invalid));
    }
    auto maximumId=entry();std::strcpy(maximumId.game.id,"2147483647");
    assert(wire::encode(maximumId,listing.id,frame));Entry validMaximumId;assert(wire::decode(frame,validMaximumId));
    std::uint8_t bytes[4]{};
    assert(!wire::transfer(bytes,sizeof(bytes),[](void*,std::size_t n){return static_cast<std::int64_t>(n+1);}));
    assert(!wire::transfer(bytes,sizeof(bytes),[](void*,std::size_t){return -1;}));
}

void nativeTests() {
    resetNative();const auto expectedId=installer::nextId.load();
    auto value=installer::install(stagedFolder,job());assert(value.code==Code::created);checkUpload(expectedId);
    resetNative(Code::alreadyExists);stageReadChunk=5;sendChunk=7;
    assert(installer::install(stagedFolder,job()).code==Code::alreadyExists);
    resetNative(Code::removed);assert(installer::remove("PPSA99500").code==Code::removed);
    resetNative();Buffer nativeEntries;const auto listId=installer::nextId.load();auto listed=result();listed.count=2;
    appendEntry(nativeEntries,entry(),listId);appendEntry(nativeEntries,entry(1),listId);appendResult(nativeEntries,listed,listId);
    replies=nativeEntries.input;visits=0;value=installer::list(cancelVisit,nullptr);
    assert(value.code==Code::cancelled&&value.count==1&&visits==1&&replyOffset==replies.size()&&sent.size()==helper.size()+wire::requestSize);
    resetNative();auto expectedOpens=opens.load();
    assert(installer::install(nullptr).code==Code::stagingError&&installer::remove("PPSA99082").code==Code::invalidGame);
    assert(opens==expectedOpens);
    for(unsigned i=0;i<std::size(wire::files);++i) {
        resetNative();missingFile=static_cast<int>(i);assert(installer::install(stagedFolder).code==Code::stagingError&&sent.empty());
        resetNative();statFailure=static_cast<int>(i);value=installer::install(stagedFolder);assert(value.code==Code::stagingError&&value.error==EACCES&&sent.empty());
        resetNative();statSizes[i]=0;assert(installer::install(stagedFolder).code==Code::stagingError&&sent.empty());
        resetNative();statSizes[i]=static_cast<std::size_t>(wire::files[i].limit+1);assert(installer::install(stagedFolder).code==Code::stagingError&&sent.empty());
        resetNative();nonRegular=static_cast<int>(i);assert(installer::install(stagedFolder).code==Code::stagingError&&sent.empty());
    }
    for(const bool eof:{false,true}) {
        resetNative();readFailure=4;readFailureOffset=wire::chunkSize;readEof=eof;
        value=installer::install(stagedFolder,job());assert(value.code==Code::stagingError&&value.error==EIO&&progress.back().completed<progress.back().total);
    }
    resetNative();staged[0].push_back(1);assert(installer::install(stagedFolder,job()).code==Code::stagingError);
    resetNative();cancelUpload=true;expectedOpens=opens.load();
    assert(installer::install(stagedFolder,job()).code==Code::cancelled&&opens==expectedOpens&&sent.empty());
    for(const auto after:{std::size_t{1},helper.size()+1,helper.size()+wire::requestSize+1}) {
        resetNative();cancelAfterSend=after;
        assert(installer::install(stagedFolder,job()).code==Code::cancelled&&replyOffset==0&&sentData()==0);
    }
    for(const auto stop:{std::size_t{0},std::size_t{1},wire::chunkSize+1}) {
        resetNative();stopProgress=stop;assert(installer::install(stagedFolder,job()).code==Code::cancelled);
        assert(progress.back().completed>=stop&&progress.back().completed<progress.back().total&&replyOffset==0);
    }
    resetNative();stopProgress=0;for(const auto n:statSizes)stopProgress+=n;
    assert(installer::install(stagedFolder,job()).code==Code::cancelled&&progress.back().completed==progress.back().total);
    assert(wire::get16(sent.data()+helper.size()+6)==static_cast<unsigned>(wire::Kind::request));
    for(const auto limit:{helper.size()+std::size_t{10},helper.size()+wire::requestSize+wire::fileSize+std::size_t{100}}) {
        for(const auto code:{Code::uploadError,Code::protocolError}) {
            resetNative(code);sendLimit=limit;value=installer::install(stagedFolder,job());
            assert(value.code==code&&replyOffset==replies.size());
        }
        resetNative();sendLimit=limit;replies.clear();assert(installer::install(stagedFolder,job()).code==Code::transportError);
        assert(progress.empty()||progress.back().completed==sentData());
    }
    resetNative();replies[0]=0;assert(installer::install(stagedFolder).code==Code::protocolError);
    resetNative();wire::put32(replies.data()+12,installer::nextId.load()+1);assert(installer::install(stagedFolder).code==Code::protocolError);
    resetNative();replies.resize(20);assert(installer::install(stagedFolder).code==Code::transportError);
    resetNative();helper.push_back(0xff);assert(installer::install(stagedFolder).code==Code::transportError&&sent.size()==128);
    resetNative();helper.resize(63);assert(installer::install(stagedFolder).code==Code::transportError&&sent.empty());
    resetNative();helper[0]=0;assert(installer::install(stagedFolder).code==Code::transportError&&sent.empty());
    resetNative();wire::put64(helper.data()+40,16*1024*1024);assert(installer::install(stagedFolder).code==Code::transportError&&sent.empty());
    resetNative();optionFailure=0x1106;assert(installer::install(stagedFolder).code==Code::transportError&&sent.empty());
    resetNative();connectResult=-1;assert(installer::install(stagedFolder).code==Code::unavailable&&sent.empty());
    resetNative();helperPresent=false;assert(installer::install(stagedFolder).code==Code::unavailable);
    resetNative();connectBlocked=true;installer::available();assert(installer::probeRunning.load());
    connectBlocked=false;finishProbe();assert(installer::available()&&sent.empty());
    expectedOpens=opens.load();const auto expectedConnects=connects.load();
    for(unsigned i=0;i<10000;++i)assert(installer::available());
    assert(opens==expectedOpens&&connects==expectedConnects);
    test::clock+=31'000'000;helperPresent=false;installer::available();finishProbe();assert(!installer::available());
    expectedOpens=opens.load();for(unsigned i=0;i<10000;++i)assert(!installer::available());assert(opens==expectedOpens);
    assert(socketCloses==sockets&&closes+2==opens&&stageOpens==stageCloses);
}
}

extern "C" {
int sceKernelOpen(const char* path,int flags,unsigned) {
    if(!std::strcmp(path,"/app0/opennow-tile-installer.elf")) {
        assert(flags==(O_RDONLY|O_NOFOLLOW));++test::opens;return test::helperPresent?10:-1;
    }
    assert(flags==(O_RDONLY|O_NOFOLLOW|O_NONBLOCK));
    for(unsigned i=0;i<std::size(test::wire::files);++i) {
        char expected[1024];std::snprintf(expected,sizeof(expected),"%s/%s",test::stagedFolder,test::wire::files[i].path);
        if(!std::strcmp(path,expected)) {
            if(test::missingFile==static_cast<int>(i))return static_cast<int>(0x80020002U);
            ++test::stageOpens;return 100+static_cast<int>(i);
        }
    }
    assert(false);return -1;
}
int sceKernelClose(int fd) {
    if(fd==10)++test::closes;
    else {assert(fd>=100&&fd<107);++test::stageCloses;}
    return 0;
}
int sceKernelFstat(int fd,struct stat* info) {
    assert(fd>=100&&fd<107);const auto i=static_cast<unsigned>(fd-100);
    if(test::statFailure==static_cast<int>(i))return static_cast<int>(0x8002000dU);
    info->st_mode=test::nonRegular==static_cast<int>(i)?S_IFDIR:S_IFREG;
    info->st_size=static_cast<off_t>(test::statSizes[i]);return 0;
}
std::int64_t sceKernelRead(int fd,void* bytes,std::size_t size) {
    if(fd==10) {
        const auto n=std::min({size,std::size_t{5},test::helper.size()-test::helperOffset});
        if(n)std::memcpy(bytes,test::helper.data()+test::helperOffset,n);
        test::helperOffset+=n;return static_cast<std::int64_t>(n);
    }
    assert(fd>=100&&fd<107&&size<=test::wire::chunkSize);const auto i=static_cast<unsigned>(fd-100);
    test::maxStageRead=std::max(test::maxStageRead,size);
    if(test::readFailure==static_cast<int>(i)&&test::offsets[i]>=test::readFailureOffset)
        return test::readEof?0:static_cast<int>(0x80020005U);
    const auto n=std::min({size,test::stageReadChunk,test::staged[i].size()-test::offsets[i]});
    if(n)std::memcpy(bytes,test::staged[i].data()+test::offsets[i],n);
    test::offsets[i]+=n;return static_cast<std::int64_t>(n);
}
int sceNetSocket(const char*,int domain,int type,int protocol) {assert(domain==2&&type==1&&protocol==6);++test::sockets;return 20;}
int sceNetSocketClose(int fd) {assert(fd==20);++test::socketCloses;return 0;}
int sceNetSetsockopt(int fd,int level,int option,const void* value,std::uint32_t size) {
    assert(fd==20&&level==0xffff&&size==sizeof(int)&&*static_cast<const int*>(value)==5'000'000);
    assert(option==0x1105||option==0x1106||option==0x1109);++test::options;
    return option==test::optionFailure?-1:0;
}
int sceNetConnect(int fd,const void* address,std::uint32_t size) {
    assert(fd==20&&size==16);const auto* p=static_cast<const std::uint8_t*>(address);
    assert(p[0]==16&&p[1]==2&&p[2]==0x23&&p[3]==0x3d&&p[4]==127&&p[5]==0&&p[6]==0&&p[7]==1);
    ++test::connects;while(test::connectBlocked.load())usleep(1000);
    return test::connectResult;
}
int sceNetSend(int fd,const void* bytes,std::size_t size,int flags) {
    assert(fd==20&&!flags);
    if(test::sent.size()>=test::sendLimit)return -1;
    const auto n=std::min({size,test::sendChunk,test::sendLimit-test::sent.size()});const auto* p=static_cast<const std::uint8_t*>(bytes);
    test::sent.insert(test::sent.end(),p,p+n);
    if(test::sent.size()>=test::cancelAfterSend)test::cancelUpload=true;
    return static_cast<int>(n);
}
int sceNetRecv(int fd,void* bytes,std::size_t size,int flags) {
    assert(fd==20&&!flags);const auto n=std::min({size,std::size_t{3},test::replies.size()-test::replyOffset});
    if(n)std::memcpy(bytes,test::replies.data()+test::replyOffset,n);
    test::replyOffset+=n;return static_cast<int>(n);
}
unsigned long long sceKernelGetProcessTime() {return test::clock.load();}
int seteuid(uid_t) noexcept {++test::credentialWrites;return -1;}
int setuid(uid_t) noexcept {++test::credentialWrites;return -1;}
int setgid(gid_t) noexcept {++test::credentialWrites;return -1;}
int setegid(gid_t) noexcept {++test::credentialWrites;return -1;}
}

int main() {
    const auto uid=getuid(),euid=geteuid();const auto gid=getgid(),egid=getegid();
    test::protocolTests();test::nativeTests();
    assert(!test::credentialWrites&&uid==getuid()&&euid==geteuid()&&gid==getgid()&&egid==getegid());
    std::puts("installer protocol/client tests passed");
}
