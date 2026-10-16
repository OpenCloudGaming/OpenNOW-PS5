// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder.hpp"
#include "../app_storage.h"
#include "files.hpp"
#include "image.hpp"
#include "installer_client.hpp"
#include "tile_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <pthread.h>

namespace opennow::forwarder {
namespace {
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
struct Guard {
    Guard(){pthread_mutex_lock(&mutex);}
    ~Guard(){pthread_mutex_unlock(&mutex);}
};
Result result(Code code,int error=0) noexcept {
    Result value;value.code=code;value.error=error;return value;
}
bool cancelled(const Job& job) noexcept {return job.cancel&&job.cancel->load();}
void progress(const Job& job,Phase phase,unsigned n,unsigned total=6) noexcept {
    if(job.progress)job.progress(job.context,{phase,n,total});
}
bool bounded(const char* value,std::size_t capacity) noexcept {return std::memchr(value,0,capacity)!=nullptr;}
bool fetch(const Game& game,art::Kind kind,unsigned char* compressed,std::uint32_t* pixels,
           unsigned width,unsigned height,const Options& options,const std::atomic_bool& stop) noexcept {
    char url[224];
    const char* artwork=kind==art::Kind::tile?game.art:(game.hero[0]?game.hero:game.art);
    std::size_t size=0;
    return options.fetch&&art::requestUrl(artwork,kind,url,sizeof(url))&&
        options.fetch(options.fetchContext,url,compressed,art::maxCompressedBytes,size,stop)&&
        art::decode(compressed,size,width,height,pixels);
}
bool writeImage(const char* stage,const char* name,const std::uint32_t* pixels,bool icon,const Job& job) noexcept {
    char path[1024];
    if(!files::join(path,stage,name))return false;
    int fd=accountFile::open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
    if(fd<0)return false;
    const bool ok=icon?encodeIconPng(pixels,512,512,files::write,&fd):
        encodeBackgroundDds(pixels,art::heroWidth,art::heroHeight,files::write,&fd,job.cancel);
    return files::finish(fd,ok);
}
bool writeStage(const char* stage,const Game& game,const Options& options,const Job& job) noexcept {
    char path[1024],from[1024],sys[1024],module[1024],body[4096],titleId[10];
    if(!files::join(sys,stage,"sce_sys")||!files::join(module,stage,"sce_module")||
       !files::mkdir(sys)||!files::mkdir(module))return false;
    for(const char* name:{"eboot.bin","sce_module/libc.prx"}) {
        if(!files::join(path,stage,name)||!files::join(from,options.paths.templateRoot,name)||
           !files::copy(from,path,2*1024*1024))return false;
    }
    if(!store::forwarderJson(game,body,sizeof(body))||!files::join(path,stage,"forwarder.json")||
       !files::writeFile(path,body,std::strlen(body)))return false;
    store::initialTitleId(game,titleId);
    if(!store::paramJson(game,titleId,body,sizeof(body))||!files::join(path,sys,"param.json")||
       !files::writeFile(path,body,std::strlen(body)))return false;
    auto compressed=std::unique_ptr<unsigned char,decltype(&std::free)>(static_cast<unsigned char*>(std::malloc(art::maxCompressedBytes)),std::free);
    auto pixels=std::unique_ptr<std::uint32_t,decltype(&std::free)>(static_cast<std::uint32_t*>(std::malloc(art::heroWidth*art::heroHeight*4)),std::free);
    if(!compressed||!pixels){errno=ENOMEM;return false;}
    const std::atomic_bool neverStop{false};
    const auto& stop=job.cancel?*job.cancel:neverStop;
    progress(job,Phase::downloadingIcon,1);
    if(!fetch(game,art::Kind::tile,compressed.get(),pixels.get(),512,512,options,stop)||cancelled(job)||
       !writeImage(stage,"sce_sys/icon0.png",pixels.get(),true,job))return false;
    progress(job,Phase::downloadingBackground,2);
    if(!fetch(game,art::Kind::hero,compressed.get(),pixels.get(),art::heroWidth,art::heroHeight,options,stop)||cancelled(job))return false;
    progress(job,Phase::writing,3);
    if(!writeImage(stage,"sce_sys/pic0.dds",pixels.get(),false,job)||cancelled(job)||
       !files::join(from,stage,"sce_sys/pic0.dds")||!files::join(path,stage,"sce_sys/pic1.dds")||
       !files::copy(from,path,3840*2160+148))return false;
    return files::syncDirectory(sys)&&files::syncDirectory(module)&&files::syncDirectory(stage);
}
struct Existing {const Game& game;Result value;};
bool findExisting(void* context,const Entry& entry) noexcept {
    auto& found=*static_cast<Existing*>(context);
    if(!std::strcmp(found.game.id,entry.game.id)&&
       (!found.game.store[0]||!entry.game.store[0]||!std::strcmp(found.game.store,entry.game.store))) {
        found.value.code=Code::alreadyExists;
        std::snprintf(found.value.titleId,sizeof(found.value.titleId),"%s",entry.titleId);
    }
    return true;
}
}

bool available() noexcept {return installer::available();}
bool validTileId(const char* titleId) noexcept {return store::validTileId(titleId);}
bool paramJson(const Game& game,const char* titleId,char* out,std::size_t size) noexcept {
    return store::paramJson(game,titleId,out,size);
}
bool forwarderJson(const Game& game,char* out,std::size_t size) noexcept {return store::forwarderJson(game,out,size);}

Result list(Visit visit,void* context,const Paths& paths) noexcept {
#ifdef OPENNOW_PS5
    (void)paths;
    return installer::list(visit,context);
#else
    return store::list(visit,context,paths);
#endif
}
Result remove(const char* titleId,const Paths& paths) noexcept {
#ifdef OPENNOW_PS5
    (void)paths;
    return installer::remove(titleId);
#else
    return store::remove(titleId,paths);
#endif
}

Result create(const Game& game,const Options& requested,const Job& job) noexcept {
    Guard guard;
    Options options=requested;
#ifdef OPENNOW_PS5
    options.paths.templateRoot=opennow_app_file("assets/forwarder-template");
    const char* stageRoot=opennow_storage_file("tile-stage");
#else
    if(!options.paths.templateRoot)options.paths.templateRoot=opennow_app_file("assets/forwarder-template");
    const char* stageRoot=options.paths.stageRoot?options.paths.stageRoot:opennow_storage_file("tile-stage");
#endif
    if(!store::validGame(game)||!bounded(game.art,sizeof(game.art))||!bounded(game.hero,sizeof(game.hero))||
       !stageRoot||stageRoot[0]!='/'||std::strlen(stageRoot)>700)return result(Code::invalidGame);
    if(cancelled(job))return result(Code::cancelled);
    progress(job,Phase::checking,0);
#ifndef OPENNOW_PS5
    if(!options.paths.root||!files::mkdir(options.paths.root))return result(Code::unavailable,errno);
#endif
    Existing existing{game,{}};
    const auto inventory=list(findExisting,&existing,options.paths);
    if(!inventory.ok()&&inventory.code!=Code::notFound)return inventory;
    if(existing.value.code==Code::alreadyExists)return existing.value;
    if(!store::verifyTemplate(options.paths.templateRoot))return result(Code::templateError,errno);
    if(!files::mkdir(stageRoot))return result(Code::stagingError,errno);
    static unsigned sequence=0;
    char stage[1024],name[64]{};
    bool staged=false;
    for(unsigned i=0;i<32;++i) {
        std::snprintf(name,sizeof(name),"job-%u-%u",static_cast<unsigned>(getpid()),++sequence);
        if(!files::join(stage,stageRoot,name))break;
        if(accountFile::makeDirectory(stage,0755)==0){staged=true;break;}
        if(errno!=EEXIST)break;
    }
    if(!staged)return result(Code::stagingError,errno);
    auto value=result(Code::artworkError);
    bool handedOff=false;
    if(writeStage(stage,game,options,job)) {
        progress(job,Phase::publishing,4);
        if(!files::syncDirectory(stageRoot))value=result(Code::stagingError,errno);
        else if(!cancelled(job)) {
            progress(job,Phase::installing,0,0);
            if(!cancelled(job)) {
                handedOff=true;
#ifdef OPENNOW_PS5
                value=installer::install(stage,job);
#else
                value=store::install(stage,options.paths);
#endif
            }
        }
    } else value.error=errno;
    if(cancelled(job)&&!value.ok()&&!handedOff)value=result(Code::cancelled);
    const int error=errno;
    (void)files::erase(stage);
    (void)files::syncDirectory(stageRoot);
    errno=error;
    if(value.ok())progress(job,Phase::complete,6);
    return value;
}

const char* message(Code code) noexcept {
    switch(code) {
    case Code::created:return "Tile created. Rescan homebrew in ShadowMountPlus.";
    case Code::alreadyExists:return "This game's home-screen tile already exists.";
    case Code::removed:return "Tile folder removed. Refresh ShadowMountPlus to remove its registration.";
    case Code::notFound:return "The tile folder is already absent.";
    case Code::listed:return "Home-screen tiles loaded.";
    case Code::available:return "Home-screen forwarding is available.";
    case Code::unavailable:return "Tile installer unavailable. Check elfldr on port 9021.";
    case Code::invalidGame:return "The selected game or tile title ID is invalid.";
    case Code::noFreeTitleId:return "No unused forwarder title ID is available.";
    case Code::conflict:return "The folder is not a recognized OpenNOW tile; nothing was removed.";
    case Code::cancelled:return "Tile creation cancelled.";
    case Code::artworkError:return "Could not download or write the game's artwork. Check connection and free space.";
    case Code::templateError:return "The shared forwarder template is missing or invalid.";
    case Code::ioError:return "The tile installer could not finish the operation in /data/homebrew.";
    case Code::stagingError:return "Could not prepare or read the staged tile files in OpenNOW storage.";
    case Code::transportError:return "The tile installer connection failed or timed out. Check elfldr on port 9021.";
    case Code::protocolError:return "The tile installer rejected the upload or returned an invalid reply.";
    case Code::uploadError:return "The tile installer could not write or clean its upload folder in /data/homebrew.";
    }
    return "Forwarder operation failed.";
}
}
