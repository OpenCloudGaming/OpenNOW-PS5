// SPDX-License-Identifier: GPL-3.0-or-later
#include "tile_store.hpp"
#include "files.hpp"
#include "launch_args.hpp"
#include "../vendor/cJSON.h"
#include "../../tooling/native/hash.hpp"
#include <cstdlib>
#include <memory>
#include <sys/file.h>

#ifdef OPENNOW_PS5
#include <sys/syscall.h>
extern "C" {
int sceKernelFstat(int,struct stat*);
int sceKernelFlock(int,int);
std::int64_t sceKernelLseek(int,std::int64_t,int);
}
#endif

namespace opennow::forwarder::store {
namespace {
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
Json json(cJSON* value) noexcept {return Json(value,cJSON_Delete);}
const char* string(const cJSON* value) noexcept {return cJSON_IsString(value)?value->valuestring:nullptr;}
const char* field(const cJSON* object,const char* name) noexcept {return string(cJSON_GetObjectItemCaseSensitive(object,name));}
bool same(const char* a,const char* b) noexcept {return a&&b&&!std::strcmp(a,b);}
bool pathValid(const Paths& paths) noexcept {
    if(!paths.root||paths.root[0]!='/'||std::strlen(paths.root)>700||
       paths.collisionRootCount>64||(paths.collisionRootCount&&!paths.collisionRoots))return false;
    for(unsigned i=0;i<paths.collisionRootCount;++i)
        if(!paths.collisionRoots[i]||paths.collisionRoots[i][0]!='/'||std::strlen(paths.collisionRoots[i])>700)return false;
    return true;
}
class Fd {
public:
    Fd()=default;
    explicit Fd(int fd) noexcept:fd_(fd){}
    ~Fd(){if(fd_>=0)accountFile::close(fd_);}
    Fd(const Fd&)=delete;
    Fd& operator=(const Fd&)=delete;
    int get() const noexcept {return fd_;}
    bool valid() const noexcept {return fd_>=0;}
    void reset(int fd) noexcept {if(fd_>=0)accountFile::close(fd_);fd_=fd;}
private:
    int fd_=-1;
};
bool statFile(int fd,struct stat& info) noexcept {
#ifdef OPENNOW_PS5
    return accountFile::checked(sceKernelFstat(fd,&info))==0;
#else
    return ::fstat(fd,&info)==0;
#endif
}
bool rewindFile(int fd) noexcept {
#ifdef OPENNOW_PS5
    return accountFile::checked(sceKernelLseek(fd,0,SEEK_SET))==0;
#else
    return ::lseek(fd,0,SEEK_SET)==0;
#endif
}
int openAt(int parent,const char* name,int flags) noexcept {
#ifdef OPENNOW_PS5
    std::int64_t value=SYS_openat;
    unsigned char failed;
    register std::int64_t mode asm("r10")=0;
    asm volatile("syscall; setc %1":"+a"(value),"=qm"(failed):"D"(parent),"S"(name),"d"(flags),"r"(mode):"rcx","r11","cc","memory");
    if(failed){errno=value<0?static_cast<unsigned>(value)&0xffff:static_cast<int>(value);return -1;}
    return static_cast<int>(accountFile::checked(value));
#else
    return ::openat(parent,name,flags);
#endif
}
int openTree(const char* path) noexcept {
    if(!path||!path[0]||std::strlen(path)>900){errno=EINVAL;return -1;}
    Fd parent(accountFile::open(path[0]=='/'?"/":".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW));
    if(!parent.valid())return -1;
    const char* at=path;
    while(*at) {
        while(*at=='/')++at;
        if(!*at)break;
        const char* end=std::strchr(at,'/');
        const std::size_t length=end?static_cast<std::size_t>(end-at):std::strlen(at);
        if(length>255||(length==1&&at[0]=='.')||(length==2&&at[0]=='.'&&at[1]=='.')){errno=EINVAL;return -1;}
        char part[256];std::memcpy(part,at,length);part[length]=0;
        const int next=openAt(parent.get(),part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if(next<0)return -1;
        parent.reset(next);at+=length;
    }
    return openAt(parent.get(),".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
}
class Guard {
public:
    explicit Guard(const char* root) noexcept:fd_(openTree(root)) {
        if(!fd_.valid())return;
        int rc;
        do {
#ifdef OPENNOW_PS5
            rc=static_cast<int>(accountFile::checked(sceKernelFlock(fd_.get(),LOCK_EX)));
#else
            rc=::flock(fd_.get(),LOCK_EX);
#endif
        } while(rc<0&&errno==EINTR);
        locked_=rc==0;
    }
    bool valid() const noexcept {return locked_;}
private:
    Fd fd_;
    bool locked_=false;
};
bool saveJson(cJSON* object,char* out,std::size_t size) noexcept {
    return object&&out&&size<2147483647U&&cJSON_PrintPreallocated(object,out,static_cast<int>(size),1);
}
bool set(cJSON* object,const char* name,const char* value) noexcept {
    auto* item=cJSON_CreateString(value);
    if(!item)return false;
    if(cJSON_ReplaceItemInObjectCaseSensitive(object,name,item))return true;
    cJSON_Delete(item);return false;
}
int openRegular(int parent,const char* name,std::size_t limit) noexcept {
    const int fd=openAt(parent,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return -1;
    struct stat info{};
    if(!statFile(fd,info)||!S_ISREG(info.st_mode)||info.st_size<=0||static_cast<std::uint64_t>(info.st_size)>limit) {
        accountFile::close(fd);errno=EINVAL;return -1;
    }
    return fd;
}
Json readJsonFd(int fd) noexcept {
    char text[16384];std::size_t used=0;
    if(fd<0||!rewindFile(fd))return json(nullptr);
    while(used<sizeof(text)) {
        const auto n=accountFile::read(fd,text+used,sizeof(text)-used);
        if(n<0&&errno==EINTR)continue;
        if(n<0)return json(nullptr);
        if(!n)break;
        used+=static_cast<std::size_t>(n);
    }
    if(!used||used==sizeof(text)||std::memchr(text,0,used))return json(nullptr);
    text[used]=0;
    if(std::strstr(text,"\\u0000"))return json(nullptr);
    return json(cJSON_ParseWithOpts(text,nullptr,1));
}
Json readJson(const char* directory,const char* name) noexcept {
    Fd dir(openTree(directory));
    if(!dir.valid())return json(nullptr);
    Fd file(openRegular(dir.get(),name,16383));
    return readJsonFd(file.get());
}
bool readMetadata(int forward,int param,const char* expected,Entry& entry) noexcept {
    auto document=readJsonFd(forward);
    if(!document||!same(field(document.get(),"target"),targetTitleId))return false;
    const char* format=field(document.get(),"format");
    if(format&&!same(format,"ps5-forwarder/1"))return false;
    const auto* args=cJSON_GetObjectItemCaseSensitive(document.get(),"args");
    if(!cJSON_IsArray(args))return false;
    const int count=cJSON_GetArraySize(args);
    if(count!=2&&count!=4)return false;
    const char* argv[4]{};
    for(int i=0;i<count;++i)if(!(argv[i]=string(cJSON_GetArrayItem(args,i))))return false;
    const auto launch=parseLaunchArgs(count,argv);
    if(launch.status!=LaunchArgsStatus::valid)return false;
    auto metadata=readJsonFd(param);
    const char* titleId=field(metadata.get(),"titleId");
    if(!store::validTileId(titleId)||(expected&&!same(titleId,expected)))return false;
    char content[96];std::snprintf(content,sizeof(content),"UP9000-%s_00-PS5FORWARDER0000",titleId);
    if(!same(field(metadata.get(),"contentId"),content))return false;
    const auto* localized=cJSON_GetObjectItemCaseSensitive(metadata.get(),"localizedParameters");
    const char* language=field(localized,"defaultLanguage");
    const char* title=language?field(cJSON_GetObjectItemCaseSensitive(localized,language),"titleName"):nullptr;
    if(!title||std::strlen(title)>=sizeof(entry.game.title))return false;
    entry={};
    std::snprintf(entry.titleId,sizeof(entry.titleId),"%s",titleId);
    std::snprintf(entry.game.id,sizeof(entry.game.id),"%s",launch.appId);
    std::snprintf(entry.game.store,sizeof(entry.game.store),"%s",launch.store);
    std::snprintf(entry.game.title,sizeof(entry.game.title),"%s",title);
    return validGame(entry.game);
}
bool readEntry(const char* root,const char* titleId,Entry& entry) noexcept {
    if(!store::validTileId(titleId))return false;
    char directory[1024];
    if(!files::join(directory,root,titleId))return false;
    Fd dir(openTree(directory));
    if(!dir.valid())return false;
    Fd sys(openAt(dir.get(),"sce_sys",O_RDONLY|O_DIRECTORY|O_NOFOLLOW));
    if(!sys.valid())return false;
    Fd forward(openRegular(dir.get(),"forwarder.json",16383)),param(openRegular(sys.get(),"param.json",16383));
    return readMetadata(forward.get(),param.get(),titleId,entry);
}

int indexOf(const char* titleId) noexcept {
    if(!titleId||std::strlen(titleId)!=9||std::strncmp(titleId,"PPSA",4))return -1;
    unsigned number=0;
    for(unsigned i=4;i<9;++i){if(titleId[i]<'0'||titleId[i]>'9')return -1;number=number*10+unsigned(titleId[i]-'0');}
    return number>=99200&&number<=99899?static_cast<int>(number-99200):-1;
}
bool inventory(const char* root,bool (&occupied)[700],const Game* game,Entry& existing) noexcept {
    files::Directory directory(root);
    if(!directory.valid())return !game||directory.error()==ENOENT||directory.error()==ENOTDIR;
    while(const char* name=directory.next()) {
        if(game&&(!std::strncmp(name,".opennow-stage-",15)||!std::strncmp(name,".opennow-remove-",16)||
                  !std::strncmp(name,".opennow-upload-",16)))continue;
        const int index=indexOf(name);
        if(index>=0)occupied[index]=true;
        char child[1024],sysPath[1024];
        if(!files::join(child,root,name)||!files::join(sysPath,child,"sce_sys")){if(game)return false;continue;}
        files::Directory folder(child);
        if(!folder.valid())continue;
        files::Directory sys(sysPath);
        if(sys.valid()) {
            auto param=readJson(sysPath,"param.json");
            char paramPath[1024];
            if(!files::join(paramPath,sysPath,"param.json")){if(game)return false;continue;}
            if(!param&&files::exists(paramPath)&&game){errno=EINVAL;return false;}
            if(param) {
                const int embedded=indexOf(field(param.get(),"titleId"));
                if(embedded>=0)occupied[embedded]=true;
            }
        }
        if(game&&existing.titleId[0]==0) {
            Entry candidate;
            if(readEntry(root,name,candidate)&&same(candidate.game.id,game->id)&&
               (!candidate.game.store[0]||!game->store[0]||same(candidate.game.store,game->store)))existing=candidate;
        }
    }
    return !game||directory.error()==0;
}
Result result(Code code,const char* titleId=nullptr,int error=0) noexcept {
    Result value;value.code=code;value.error=error;
    if(titleId)std::snprintf(value.titleId,sizeof(value.titleId),"%s",titleId);
    return value;
}

bool contents(int fd,const char* const* allowed,unsigned count) noexcept {
    files::Directory directory(openAt(fd,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW));
    if(!directory.valid())return false;
    unsigned seen=0;bool ok=true;
    while(const char* name=directory.next()) {
        unsigned index=0;
        while(index<count&&!same(name,allowed[index]))++index;
        if(index==count||(seen&(1U<<index))){ok=false;break;}
        seen|=1U<<index;
    }
    if(directory.error()){errno=directory.error();return false;}
    if(!ok||seen!=(1U<<count)-1){errno=EINVAL;return false;}
    return true;
}
struct StagedFile { const char* name; unsigned parent; std::size_t limit; unsigned mode; };
constexpr StagedFile stagedFiles[]={
    {"eboot.bin",0,2*1024*1024,0755},
    {"forwarder.json",0,16383,0644},
    {"libc.prx",2,2*1024*1024,0755},
    {"param.json",1,16383,0644},
    {"icon0.png",1,2*1024*1024,0644},
    {"pic0.dds",1,3840*2160+148,0644},
    {"pic1.dds",1,3840*2160+148,0644}
};
class Stage {
public:
    bool open(const char* path) noexcept {
        directories[0].reset(openTree(path));
        if(!directories[0].valid())return false;
        directories[1].reset(openAt(directories[0].get(),"sce_sys",O_RDONLY|O_DIRECTORY|O_NOFOLLOW));
        directories[2].reset(openAt(directories[0].get(),"sce_module",O_RDONLY|O_DIRECTORY|O_NOFOLLOW));
        if(!directories[1].valid()||!directories[2].valid())return false;
        const char* root[]={"eboot.bin","forwarder.json","sce_sys","sce_module"};
        const char* sys[]={"param.json","icon0.png","pic0.dds","pic1.dds"};
        const char* module[]={"libc.prx"};
        if(!contents(directories[0].get(),root,4)||!contents(directories[1].get(),sys,4)||!contents(directories[2].get(),module,1))return false;
        for(unsigned i=0;i<7;++i) {
            const auto& spec=stagedFiles[i];
            files[i].reset(openRegular(directories[spec.parent].get(),spec.name,spec.limit));
            if(!files[i].valid())return false;
            if(i>=5) {
                struct stat info{};
                if(!statFile(files[i].get(),info)||static_cast<std::uint64_t>(info.st_size)!=spec.limit){errno=EINVAL;return false;}
            }
        }
        return readMetadata(files[1].get(),files[3].get(),nullptr,entry);
    }
    Fd directories[3],files[7];
    Entry entry;
};
struct Program {const char* name; std::size_t size; const char* hash;};
constexpr Program programs[]={
    {"eboot.bin",143974,"3944ed19d258d348be0dda445959740919be4f727d6e6766e783b8a01c298331"},
    {"libc.prx",1284674,"e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"}
};
bool verifyProgram(int fd,const Program& program) noexcept {
    if(fd<0||!rewindFile(fd))return false;
    struct stat info{};
    if(!statFile(fd,info)||!S_ISREG(info.st_mode)||static_cast<std::uint64_t>(info.st_size)!=program.size){errno=EINVAL;return false;}
    auto bytes=std::unique_ptr<unsigned char,decltype(&std::free)>(static_cast<unsigned char*>(std::malloc(program.size+1)),std::free);
    if(!bytes){errno=ENOMEM;return false;}
    std::size_t used=0;
    while(used<=program.size) {
        const auto n=accountFile::read(fd,bytes.get()+used,program.size+1-used);
        if(n<0&&errno==EINTR)continue;
        if(n<0)return false;
        if(!n)break;
        used+=static_cast<std::size_t>(n);
    }
    if(used!=program.size){errno=EINVAL;return false;}
    const auto digest=ps5::crypto::sha256({bytes.get(),used});
    char hex[65];
    for(unsigned i=0;i<32;++i)std::snprintf(hex+i*2,3,"%02x",digest[i]);
    if(!same(hex,program.hash)){errno=EINVAL;return false;}
    return true;
}
bool verifyPrograms(const Stage& stage) noexcept {
    return verifyProgram(stage.files[0].get(),programs[0])&&verifyProgram(stage.files[2].get(),programs[1]);
}
std::uint32_t titleHash(const Game& game) noexcept {
    std::uint32_t hash=2166136261U;
    for(const char* value:{game.id,game.store}) {
        for(const char* p=value;*p;++p){hash^=static_cast<unsigned char>(*p);hash*=16777619U;}
        hash^=0;hash*=16777619U;
    }
    return hash;
}
int candidate(std::uint32_t hash,unsigned offset,char (&titleId)[10]) noexcept {
    const unsigned index=(hash%700+offset)%700;
    std::snprintf(titleId,sizeof(titleId),"PPSA%05u",99200+index);
    return store::validTileId(titleId)?static_cast<int>(index):-1;
}
bool collisions(const Paths& paths,bool (&occupied)[700],Entry& existing) noexcept {
    const char* defaults[]={"/user/appmeta","/system_data/priv/appmeta","/system_ex/app","/user/app","/data/etaHEN/games"};
    if(paths.collisionRoots) {
        for(unsigned i=0;i<paths.collisionRootCount;++i)
            if(!inventory(paths.collisionRoots[i],occupied,nullptr,existing))return false;
        return true;
    }
    for(const char* path:defaults)if(!inventory(path,occupied,nullptr,existing))return false;
    for(unsigned i=0;i<10;++i)for(unsigned j=0;j<3;++j) {
        char path[64];std::snprintf(path,sizeof(path),"/mnt/%s%u%s",i<8?"usb":"ext",i<8?i:i-8,j==0?"":(j==1?"/homebrew":"/etaHEN/games"));
        if(!inventory(path,occupied,nullptr,existing))return false;
    }
    return true;
}
bool copyFile(int from,const char* to,std::size_t limit,unsigned mode) noexcept {
    if(!rewindFile(from))return false;
    struct stat info{};
    if(!statFile(from,info)||!S_ISREG(info.st_mode)||info.st_size<=0||static_cast<std::uint64_t>(info.st_size)>limit){errno=EINVAL;return false;}
    int target=accountFile::open(to,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,mode);
    if(target<0)return false;
    unsigned char buffer[16384];std::size_t total=0;bool ok=accountFile::permissions(target,mode)==0;
    while(total<=limit) {
        const std::size_t remaining=limit-total+1;
        const auto n=accountFile::read(from,buffer,remaining<sizeof(buffer)?remaining:sizeof(buffer));
        if(n<0&&errno==EINTR)continue;
        if(n<0){ok=false;break;}
        if(!n)break;
        total+=static_cast<std::size_t>(n);
        if(total>limit||!files::write(&target,buffer,static_cast<std::size_t>(n))){ok=false;break;}
    }
    if(total!=static_cast<std::uint64_t>(info.st_size)){errno=EINVAL;ok=false;}
    return files::finish(target,ok&&total>0);
}
bool copyStage(const Stage& source,const char* destination) noexcept {
    char sys[1024],module[1024],to[1024];
    if(!files::join(sys,destination,"sce_sys")||!files::join(module,destination,"sce_module")||
       !files::mkdir(sys)||!files::mkdir(module))return false;
    const char* directories[]={destination,sys,module};
    for(unsigned i=0;i<7;++i) {
        const auto& spec=stagedFiles[i];
        if(!files::join(to,directories[spec.parent],spec.name)||!copyFile(source.files[i].get(),to,spec.limit,spec.mode))return false;
    }
    return files::syncDirectory(sys)&&files::syncDirectory(module);
}
bool removeEmpty(const char* path) noexcept {
#ifdef OPENNOW_PS5
    return accountFile::checked(sceKernelRmdir(path))==0;
#else
    return ::rmdir(path)==0;
#endif
}
bool sameDirectory(const char* path,const struct stat& expected) noexcept {
    Fd current(openTree(path));struct stat info{};
    return current.valid()&&statFile(current.get(),info)&&info.st_dev==expected.st_dev&&info.st_ino==expected.st_ino;
}
}



bool forwarderJson(const Game& game,char* out,std::size_t size) noexcept {
    if(!validGame(game))return false;
    auto root=json(cJSON_CreateObject());
    if(!root||!cJSON_AddStringToObject(root.get(),"format","ps5-forwarder/1")||
       !cJSON_AddStringToObject(root.get(),"target",targetTitleId)||
       !cJSON_AddStringToObject(root.get(),"creator","OpenNOW PS5"))return false;
    auto* args=cJSON_AddArrayToObject(root.get(),"args");
    if(!args)return false;
    const char* values[]={"--gfn-app-id",game.id,"--store",game.store};
    for(unsigned i=0;i<(game.store[0]?4U:2U);++i) {
        auto* value=cJSON_CreateString(values[i]);
        if(!value)return false;
        if(!cJSON_AddItemToArray(args,value)){cJSON_Delete(value);return false;}
    }
    return saveJson(root.get(),out,size);
}

bool paramJson(const Game& game,const char* titleId,char* out,std::size_t size) noexcept {
    if(!validGame(game)||!validTileId(titleId))return false;
    auto root=json(cJSON_Parse(R"({"ageLevel":{"default":0},"applicationCategoryType":0,"applicationDrmType":"free",
"attribute":0,"attribute2":0,"attribute3":0,"conceptId":"","contentBadgeType":1,"contentId":"",
"contentVersion":"01.000.000","downloadDataSize":256,"gameIntent":{"permittedIntents":[{"intentType":"launchActivity"}]},
"localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":""}},"masterVersion":"01.00",
"pubtools":{"creationDate":"2026-08-22 00:00:00","loudnessSnd0":"-28.00","toolVersion":"2.00"},
"requiredSystemSoftwareVersion":"0x0000000000000000","sdkVersion":"0x0000000000000000","titleId":"","versionFileUri":""})"));
    if(!root)return false;
    char content[96];std::snprintf(content,sizeof(content),"UP9000-%s_00-PS5FORWARDER0000",titleId);
    auto* localized=cJSON_GetObjectItemCaseSensitive(root.get(),"localizedParameters");
    auto* english=cJSON_GetObjectItemCaseSensitive(localized,"en-US");
    return set(root.get(),"titleId",titleId)&&set(root.get(),"conceptId",titleId+4)&&
        set(root.get(),"contentId",content)&&set(english,"titleName",game.title)&&saveJson(root.get(),out,size);
}

void initialTitleId(const Game& game,char (&titleId)[10]) noexcept {
    titleId[0]=0;
    if(!validGame(game))return;
    const auto hash=titleHash(game);
    for(unsigned i=0;i<700;++i) {
        if(candidate(hash,i,titleId)>=0)return;
    }
    titleId[0]=0;
}

bool verifyTemplate(const char* root) noexcept {
    if(!root||std::strlen(root)>700)return false;
    char modulePath[1024],ebootPath[1024],libcPath[1024];
    if(!files::join(modulePath,root,"sce_module")||!files::join(ebootPath,root,programs[0].name)||
       !files::join(libcPath,modulePath,programs[1].name))return false;
    files::Directory dir(root),module(modulePath);
    if(!dir.valid()||!module.valid())return false;
    Fd eboot(accountFile::open(ebootPath,O_RDONLY|O_NOFOLLOW|O_NONBLOCK));
    Fd libc(accountFile::open(libcPath,O_RDONLY|O_NOFOLLOW|O_NONBLOCK));
    return verifyProgram(eboot.get(),programs[0])&&verifyProgram(libc.get(),programs[1]);
}

Result install(const char* stagedFolder,const Paths& paths) noexcept {
    if(!pathValid(paths)||!stagedFolder||stagedFolder[0]!='/')return result(Code::invalidGame);
    Stage source;
    if(!source.open(stagedFolder))return result(Code::invalidGame,nullptr,errno);
    if(!verifyPrograms(source))return result(Code::templateError,nullptr,errno);
    if(!files::mkdir(paths.root))return result(Code::unavailable,nullptr,errno);
    Guard guard(paths.root);
    if(!guard.valid())return result(Code::unavailable,nullptr,errno);
    bool occupied[700]{};Entry existing;
    if(!inventory(paths.root,occupied,&source.entry.game,existing))return result(Code::ioError,nullptr,errno);
    if(existing.titleId[0])return result(Code::alreadyExists,existing.titleId);
    if(!collisions(paths,occupied,existing))return result(Code::ioError,nullptr,errno);
    const auto hash=titleHash(source.entry.game);
    char titleId[10]{},final[1024];
    bool reserved=false;
    for(unsigned i=0;i<700;++i) {
        const int index=candidate(hash,i,titleId);
        if(index<0||occupied[index])continue;
        if(!files::join(final,paths.root,titleId))return result(Code::ioError,nullptr,ENAMETOOLONG);
        if(accountFile::makeDirectory(final,0755)==0){reserved=true;break;}
        if(errno!=EEXIST)return result(Code::ioError,nullptr,errno);
    }
    if(!reserved)return result(Code::noFreeTitleId);
    Fd reservation(openTree(final));
    struct stat reservedInfo{};
    const bool owned=reservation.valid()&&statFile(reservation.get(),reservedInfo);
    bool ok=owned;
    char stage[1024],name[80];bool staged=false;
    if(ok)for(unsigned i=0;i<32;++i) {
        std::snprintf(name,sizeof(name),".opennow-stage-%s-%u-%u",titleId,static_cast<unsigned>(getpid()),i);
        if(!files::join(stage,paths.root,name))break;
        if(accountFile::makeDirectory(stage,0755)==0){staged=true;break;}
        if(errno!=EEXIST)break;
    }
    Code code=Code::ioError;
    if(staged&&copyStage(source,stage)) {
        Stage copied;
        ok=copied.open(stage);
        if(ok&&!verifyPrograms(copied)){ok=false;code=Code::templateError;}
        if(ok)ok=same(copied.entry.game.id,source.entry.game.id)&&same(copied.entry.game.store,source.entry.game.store)&&same(copied.entry.game.title,source.entry.game.title);
        char param[1024],body[4096];
        if(ok)ok=files::join(param,stage,"sce_sys/param.json")&&accountFile::remove(param)==0&&
            store::paramJson(source.entry.game,titleId,body,sizeof(body))&&files::writeFile(param,body,std::strlen(body))&&
            accountFile::syncParent(param)&&files::syncDirectory(stage);
        if(ok)ok=sameDirectory(final,reservedInfo);
        if(ok)ok=accountFile::rename(stage,final)==0&&files::syncDirectory(paths.root);
        if(ok)code=Code::created;
    }
    const int error=errno;
    if(staged&&code!=Code::created)files::erase(stage);
    if(code!=Code::created&&owned&&sameDirectory(final,reservedInfo))removeEmpty(final);
    return result(code,titleId,code==Code::created?0:error);
}

Result list(Visit visit,void* context,const Paths& paths) noexcept {
    if(!pathValid(paths))return result(Code::invalidGame);
    Guard guard(paths.root);
    if(!guard.valid())return result(errno==ENOENT?Code::listed:Code::unavailable,nullptr,errno==ENOENT?0:errno);
    files::Directory directory(paths.root);
    if(!directory.valid())return result(Code::unavailable,nullptr,directory.error());
    auto value=result(Code::listed);
    while(const char* name=directory.next()) {
        Entry entry;
        if(!readEntry(paths.root,name,entry))continue;
        ++value.count;
        if(visit&&!visit(context,entry))break;
    }
    if(directory.error())return result(Code::ioError,nullptr,directory.error());
    return value;
}

Result remove(const char* titleId,const Paths& paths) noexcept {
    if(!pathValid(paths)||!store::validTileId(titleId))return result(Code::invalidGame);
    Guard guard(paths.root);
    if(!guard.valid())return result(errno==ENOENT?Code::notFound:Code::unavailable,titleId,errno);
    char directory[1024],tomb[1024],name[80],owner[1024],marker[128],stored[128];
    if(!files::join(directory,paths.root,titleId))return result(Code::invalidGame);
    std::snprintf(name,sizeof(name),".opennow-remove-%s",titleId);
    if(!files::join(tomb,paths.root,name))return result(Code::invalidGame);
    std::snprintf(name,sizeof(name),".opennow-remove-%s.owner",titleId);
    if(!files::join(owner,paths.root,name))return result(Code::invalidGame);
    std::snprintf(marker,sizeof(marker),"OpenNOW PS5 removal %s\n",titleId);
    if(!files::exists(directory)) {
        if(!files::exists(tomb)&&!files::exists(owner))return result(Code::notFound,titleId);
        if(!files::readText(owner,stored,sizeof(stored))||!same(marker,stored))return result(Code::conflict,titleId);
        if(!files::erase(tomb)||accountFile::remove(owner)!=0||!files::syncDirectory(paths.root))return result(Code::ioError,titleId,errno);
        return result(Code::removed,titleId);
    }
    Entry entry;
    if(!readEntry(paths.root,titleId,entry))return result(Code::conflict,titleId);
    if(files::exists(tomb))return result(Code::conflict,titleId);
    if(files::exists(owner)) {
        if(!files::readText(owner,stored,sizeof(stored))||!same(marker,stored))return result(Code::conflict,titleId);
    } else if(!files::writeFile(owner,marker,std::strlen(marker)))return result(Code::ioError,titleId,errno);
    if(!files::syncDirectory(paths.root)||accountFile::rename(directory,tomb)!=0||!files::syncDirectory(paths.root)||!files::erase(tomb)||
       accountFile::remove(owner)!=0||!files::syncDirectory(paths.root))return result(Code::ioError,titleId,errno);
    return result(Code::removed,titleId);
}

}
