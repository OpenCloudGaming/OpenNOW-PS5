// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_renderer.hpp"
#include "gfn.hpp"
#include "app_storage.h"
#include "http.hpp"
#include "random.hpp"
#include "cloud.hpp"
#include "catalog_search.hpp"
#include "ui/tv_ui.hpp"
#include "stream/native/gpu_presenter.hpp"
#ifndef OPENNOW_HOST_PREVIEW
#include "stream/stream.hpp"
#endif
#include "vendor/qrcodegen.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <string_view>
extern "C" {
#include "platform/ps5-pad.h"
int sceUserServiceGetInitialUser(int*);
int sceNetInit();
int sceKernelUsleep(unsigned);
unsigned long long sceKernelGetProcessTime();
int sceSystemServiceLoadExec(const char*,const char**);
int scePthreadJoin(void*,void**);
int scePadClose(int);
int scePthreadCreate(void**,const void*,void* (*)(void*),void*,const char*);
int scePthreadAttrInit(void**);
int scePthreadAttrSetstacksize(void**,std::size_t);
int scePthreadAttrDestroy(void**);
}
namespace {
using opennow::State;
using ps5::demo::Canvas;
opennow::View published;
opennow::CloudView publishedCloud;
PS5_PadData publishedPad{};
#ifndef OPENNOW_HOST_PREVIEW
opennow::Media media;
#endif
bool publishedStream=false;
bool publishedSession=false;
bool publishedStreamFailure=false;
opennow::StreamProfile publishedProfile=opennow::StreamProfile::quality;
pthread_mutex_t viewMutex=PTHREAD_MUTEX_INITIALIZER;
std::atomic_int command{0};
opennow::Http* activeHttp=nullptr; // Set before UI loop; lifetime is the process.
int storageError=0;
int pad=-1;
unsigned lastButtons=0;
opennow::CatalogSearch searchInput;
char pendingSearch[128]{};
unsigned publishedProfileMask=0;
struct Request { int index=-1; int profile=-1; };
Request pendingRequest;
bool detailOpen=false;
unsigned libraryFocus=0, focusRevision=~0U;
opennow::StreamProfile detailProfile=opennow::StreamProfile::quality;
void submit(int code,Request payload) noexcept {
    pthread_mutex_lock(&viewMutex);pendingRequest=payload;command.store(code);pthread_mutex_unlock(&viewMutex);
}
int takeCommand(Request& payload) noexcept {
    pthread_mutex_lock(&viewMutex);
    const int action=command.exchange(0);
    payload=pendingRequest;pendingRequest=Request{};
    pthread_mutex_unlock(&viewMutex);
    return action;
}
bool acceptProfile(int requested,unsigned mask,opennow::StreamProfile& profile) noexcept {
    if(requested<0||requested>=static_cast<int>(opennow::StreamProfile::count)||!(mask&(1U<<requested)))return false;
    profile=static_cast<opennow::StreamProfile>(requested);
    return true;
}
void publish(const opennow::View& v) {
    pthread_mutex_lock(&viewMutex); published=v; pthread_mutex_unlock(&viewMutex);
}
void* worker(void*) {
    if (storageError!=0) {
        opennow::View v;v.state=State::failed;
        std::snprintf(v.message,sizeof(v.message),"Persistent storage unavailable (code %d)",storageError);
        publish(v);
        while (command.exchange(0)!=10) sceKernelUsleep(10000);
        ps5::demo::requestStop();return nullptr;
    }
    auto& http=*activeHttp;
    // One process-lifetime worker owns these large bounded objects.
    static opennow::Login login(opennow::Http::request,&http,opennow::appStorage::accountPath());
    unsigned char random[16]; char id[37]{};
    if (!opennow::randomBytes(random,sizeof(random)) || !http.ready()) {
        opennow::View v; v.state=State::failed;
        std::snprintf(v.message,sizeof(v.message),"Unable to initialize secure networking"); publish(v); return nullptr;
    }
    random[6]=(random[6]&15)|64; random[8]=(random[8]&63)|128;
    std::snprintf(id,sizeof(id),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7],random[8],random[9],random[10],random[11],random[12],random[13],random[14],random[15]);
    static opennow::Cloud cloud(opennow::Http::request,&http);
#ifndef OPENNOW_HOST_PREVIEW
    static opennow::Stream stream(media);
#endif
    opennow::View restoring; restoring.state=State::requesting;
    std::snprintf(restoring.message,sizeof(restoring.message),"Checking saved NVIDIA login...");
    publish(restoring);
    login.restore(id,sceKernelGetProcessTime()/1000000);
    bool catalogLoaded=false,streamAttempted=false;
    char streamFailure[192]{};
#ifndef OPENNOW_HOST_PREVIEW
    auto profile=opennow::gpu::bestProfile();
#else
    auto profile=opennow::StreamProfile::quality;
#endif
    unsigned profileMask=0;
    for(unsigned i=0;i<static_cast<unsigned>(opennow::StreamProfile::count);++i) {
        const auto candidate=static_cast<opennow::StreamProfile>(i);
        if(!opennow::settingsFor(candidate).hardware||opennow::gpu::profileAvailable(candidate))profileMask|=1U<<i;
    }
    pthread_mutex_lock(&viewMutex);publishedProfileMask=profileMask;pthread_mutex_unlock(&viewMutex);
    for (;;) {
        Request request;
        const int action=takeCommand(request);
        if(action==10||action==11) {
            http.cancelled.store(false);
#ifndef OPENNOW_HOST_PREVIEW
            stream.stop();
#endif
            streamAttempted=false;
            const bool stopped=!*cloud.session().id||cloud.stop(login.cloudToken(),id);
            if(action==10){ps5::demo::requestStop();return nullptr;}
            if(stopped) {
                login.cancel();cloud.reset();catalogLoaded=false;streamFailure[0]=0;
            }
        }
        if (action==2) {
            http.cancelled.store(false);
#ifndef OPENNOW_HOST_PREVIEW
            stream.stop();
#endif
            streamAttempted=false;
            cloud.stop(login.cloudToken(),id);
        }
        if (action==1) {
            http.cancelled.store(false);
            if(!*cloud.session().id) {cloud.reset();catalogLoaded=false;}
            streamAttempted=false;streamFailure[0]=0;
            opennow::View v; v.state=State::requesting;
            std::snprintf(v.message,sizeof(v.message),"Contacting NVIDIA securely..."); publish(v);
            if (!login.restore(id,sceKernelGetProcessTime()/1000000))
                login.begin(id,sceKernelGetProcessTime()/1000000);
        }
        const auto now=sceKernelGetProcessTime();
        // Renew between games: blocking login HTTPS must not stall media processing.
#ifndef OPENNOW_HOST_PREVIEW
        if (!stream.active()) login.tick(now/1000000);
#else
        login.tick(now/1000000);
#endif
        if(login.view().state==State::authenticated) {
            if(!catalogLoaded){publish(login.view());pthread_mutex_lock(&viewMutex);publishedCloud.state=opennow::CloudState::loading;std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Loading NVIDIA catalog...");pthread_mutex_unlock(&viewMutex);cloud.load(login.cloudToken(),id,"");catalogLoaded=true;}
            if(action==9&&cloud.view().state==opennow::CloudState::catalog) {
                do {profile=opennow::nextProfile(profile);} while(opennow::settingsFor(profile).hardware&&!opennow::gpu::profileAvailable(profile));
            }
            if(action==14&&cloud.view().state==opennow::CloudState::catalog) {
                acceptProfile(request.profile,profileMask,profile);
            }
            if(action==5&&cloud.view().state==opennow::CloudState::catalog){
                if(request.index>=0)cloud.focus(static_cast<unsigned>(request.index));
                acceptProfile(request.profile,profileMask,profile);
                streamFailure[0]=0;
                pthread_mutex_lock(&viewMutex);
                publishedCloud.state=opennow::CloudState::starting;
                std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Starting cloud session...");
                pthread_mutex_unlock(&viewMutex);
                cloud.launch(login.cloudToken(),id,now/1000000,profile);streamAttempted=false;
            }
            if(action==6&&!*cloud.session().id) {streamFailure[0]=0;cloud.load(login.cloudToken(),id,"",false);}
            if(action==7&&!*cloud.session().id&&cloud.view().hasNext) {streamFailure[0]=0;cloud.load(login.cloudToken(),id,"",true);}
            if(action==8&&!*cloud.session().id){
                streamFailure[0]=0;
                char search[128];
                pthread_mutex_lock(&viewMutex);std::memcpy(search,pendingSearch,sizeof(search));pthread_mutex_unlock(&viewMutex);
                cloud.load(login.cloudToken(),id,search,false);
            }
            if(const int pending=command.load();pending==2||pending==10||pending==11)continue;
            cloud.tick(login.cloudToken(),id,now/1000000);
            if(const int pending=command.load();pending==2||pending==10||pending==11)continue;
#ifndef OPENNOW_HOST_PREVIEW
            if(cloud.view().state==opennow::CloudState::ready&&!streamAttempted){streamAttempted=true;stream.start(cloud.session(),id);}
            if(stream.active()){
                stream.tick(now);PS5_PadData padState{};pthread_mutex_lock(&viewMutex);padState=publishedPad;pthread_mutex_unlock(&viewMutex);stream.input(padState,now);
            }
            if(stream.failed()) {
                std::snprintf(streamFailure,sizeof(streamFailure),"%s",stream.status());
                stream.stop();streamAttempted=false;
                cloud.stop(login.cloudToken(),id);
            }
#endif
        }
        pthread_mutex_lock(&viewMutex);publishedCloud=cloud.view();publishedProfile=profile;
        publishedSession=*cloud.session().id!=0;
        publishedStreamFailure=*streamFailure!=0;
        if(*streamFailure) {
            if(publishedSession)std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"%.75s | Cleanup: %.100s",streamFailure,cloud.view().message);
            else std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"%s",streamFailure);
        }
#ifndef OPENNOW_HOST_PREVIEW
        publishedStream=stream.active();
        if(streamAttempted)std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"%s",stream.status());
#endif
        pthread_mutex_unlock(&viewMutex);
        // A cancellation during a blocking request must win over its response.
        if (command.load()==2||command.load()==10||command.load()==11) continue;
        publish(login.view());
        sceKernelUsleep(publishedStream?2000:100000);
    }
}
bool draw(ps5::demo::Canvas& c) noexcept {
    using opennow::ui::Screen;
    using opennow::CloudState;
    if (pad<0) {
        int user=-1;
        if (sceUserServiceGetInitialUser(&user)==0) pad=scePadOpen(user,0,0,nullptr);
    }
    PS5_PadData data{};
    unsigned pressed=0;
    if (pad>=0 && scePadReadState(pad,&data)==0 && data.connected) {
        pressed=data.buttons & ~lastButtons; lastButtons=data.buttons;
    } else lastButtons=0;
    opennow::View v;opennow::CloudView cv;bool streaming=false,sessionOwned=false,streamFailed=false;opennow::StreamProfile profile;unsigned profileMask=0;
    pthread_mutex_lock(&viewMutex);v=published;cv=publishedCloud;publishedPad=data;streaming=publishedStream;sessionOwned=publishedSession;streamFailed=publishedStreamFailure;profile=publishedProfile;profileMask=publishedProfileMask;pthread_mutex_unlock(&viewMutex);
    const bool input=pressed!=0;
    if(cv.revision!=focusRevision){focusRevision=cv.revision;libraryFocus=0;detailOpen=false;}
    if(cv.count&&libraryFocus>=cv.count)libraryFocus=cv.count-1;
    if(v.state!=State::authenticated||streaming||sessionOwned){searchInput.open=false;detailOpen=false;}
    if(cv.state!=CloudState::catalog||!cv.count)detailOpen=false;
    if(searchInput.open) {
        if(pressed&PS5_PAD_BUTTON_LEFT)searchInput.move(-1,0);
        if(pressed&PS5_PAD_BUTTON_RIGHT)searchInput.move(1,0);
        if(pressed&PS5_PAD_BUTTON_UP)searchInput.move(0,-1);
        if(pressed&PS5_PAD_BUTTON_DOWN)searchInput.move(0,1);
        if(pressed&PS5_PAD_BUTTON_CROSS)searchInput.append();
        if(pressed&PS5_PAD_BUTTON_SQUARE)searchInput.erase();
        if(pressed&PS5_PAD_BUTTON_TRIANGLE)searchInput.text[0]=0;
        if(pressed&PS5_PAD_BUTTON_CIRCLE)searchInput.open=false;
        else if(pressed&PS5_PAD_BUTTON_OPTIONS) {
            pthread_mutex_lock(&viewMutex);std::memcpy(pendingSearch,searchInput.text,sizeof(pendingSearch));pthread_mutex_unlock(&viewMutex);
            command.store(8);searchInput.open=false;
        }
        pressed=0;
    } else if(detailOpen&&(pressed&PS5_PAD_BUTTON_CIRCLE)) {
        detailOpen=false;pressed&=~PS5_PAD_BUTTON_CIRCLE;
    } else if(!detailOpen&&v.state==State::authenticated&&!streaming&&!sessionOwned&&
              (cv.state==CloudState::catalog||cv.state==CloudState::failed)&&
              (pressed&PS5_PAD_BUTTON_TRIANGLE)) {
        searchInput.open=true;pressed=0;
    }
    const unsigned signOutButtons=PS5_PAD_BUTTON_L1|PS5_PAD_BUTTON_R1;
    if(!streaming&&(pressed&PS5_PAD_BUTTON_CIRCLE)&&activeHttp) {
        activeHttp->cancelled.store(true);command.store(10);pressed=0;
    } else if(!streaming&&v.state==State::authenticated&&
        (data.buttons&signOutButtons)==signOutButtons&&(pressed&signOutButtons)&&activeHttp) {
        activeHttp->cancelled.store(true);command.store(11);pressed=0;detailOpen=false;
    } else if ((streaming||sessionOwned||cv.state==CloudState::starting||cv.state==CloudState::queued)&&
        (data.buttons&PS5_PAD_BUTTON_OPTIONS)&&(pressed&PS5_PAD_BUTTON_TOUCH_PAD) && activeHttp) {
        activeHttp->cancelled.store(true); command.store(2);
    } else if (!streaming && (pressed&PS5_PAD_BUTTON_CROSS) && v.state!=State::waiting && v.state!=State::requesting && v.state!=State::authenticated) command.store(1);
    const auto launch=[&](int requestedProfile){
        submit(5,{static_cast<int>(libraryFocus),requestedProfile});detailOpen=false;
    };
    if(v.state==State::authenticated&&!streaming&&!sessionOwned&&
       (cv.state==CloudState::catalog||cv.state==CloudState::failed)) {
        if(detailOpen) {
            opennow::StreamProfile options[static_cast<unsigned>(opennow::StreamProfile::count)];
            const unsigned count=opennow::ui::availableProfiles(profileMask,options,static_cast<unsigned>(opennow::StreamProfile::count));
            unsigned current=0;
            for(unsigned i=0;i<count;++i)if(options[i]==detailProfile)current=i;
            unsigned next=current;
            if((pressed&PS5_PAD_BUTTON_UP)&&next>0)--next;
            if((pressed&PS5_PAD_BUTTON_DOWN)&&next+1<count)++next;
            if((pressed&PS5_PAD_BUTTON_L1)&&count)next=(next+1)%count;
            if(count&&next!=current) {
                detailProfile=options[next];
                submit(14,{-1,static_cast<int>(detailProfile)});
            }
            unsigned siblings[16];
            const unsigned siblingCount=opennow::ui::storeSiblings(cv,libraryFocus,siblings,16);
            for(unsigned i=0;i<siblingCount;++i)if(siblings[i]==libraryFocus) {
                if((pressed&PS5_PAD_BUTTON_LEFT)&&i>0)libraryFocus=siblings[i-1];
                else if((pressed&PS5_PAD_BUTTON_RIGHT)&&i+1<siblingCount)libraryFocus=siblings[i+1];
                break;
            }
            if(pressed&PS5_PAD_BUTTON_CROSS)launch(count?static_cast<int>(detailProfile):-1);
        } else {
            if(cv.state==CloudState::catalog&&(pressed&PS5_PAD_BUTTON_L1))command.store(9);
            if(cv.state==CloudState::catalog&&!streamFailed&&cv.count) {
                const unsigned columns=opennow::ui::gridColumns;
                if((pressed&PS5_PAD_BUTTON_LEFT)&&libraryFocus>0)--libraryFocus;
                if((pressed&PS5_PAD_BUTTON_RIGHT)&&libraryFocus+1<cv.count)++libraryFocus;
                if((pressed&PS5_PAD_BUTTON_UP)&&libraryFocus>=columns)libraryFocus-=columns;
                if((pressed&PS5_PAD_BUTTON_DOWN)&&libraryFocus/columns+1<(cv.count+columns-1)/columns)
                    libraryFocus=std::min(libraryFocus+columns,cv.count-1);
            }
            if(pressed&PS5_PAD_BUTTON_CROSS) {
                if(cv.state==CloudState::failed)command.store(6);
                else if(streamFailed)launch(-1);
                else if(cv.count){detailOpen=true;detailProfile=profile;}
            }
            if(pressed&PS5_PAD_BUTTON_SQUARE)command.store(6);
            if(pressed&PS5_PAD_BUTTON_R1)command.store(7);
        }
    }
#ifndef OPENNOW_HOST_PREVIEW
    if(streaming&&media.frames.load())return media.draw(c);
#endif
    static opennow::CloudView previousCloud;
    static opennow::View previous;
    static auto previousProfile=opennow::StreamProfile::quality;
    static unsigned previousMask=0;
    static bool first=true;
    if (!first && !input && std::memcmp(&previous,&v,sizeof(v))==0&&std::memcmp(&previousCloud,&cv,sizeof(cv))==0&&previousProfile==profile&&previousMask==profileMask) return false;
    first=false; previous=v;previousCloud=cv;previousProfile=profile;previousMask=profileMask;
    const Screen screen=opennow::ui::screenFor({v,cv,streaming,sessionOwned,streamFailed,searchInput.open,detailOpen});
#ifndef OPENNOW_HOST_PREVIEW
    const char* output=opennow::gpu::outputLabel();
#else
    const char* output="Host preview \xC2\xB7 no video output";
#endif
    opennow::ui::render(c,{screen,v,cv,screen==Screen::detail?detailProfile:profile,profileMask,libraryFocus,searchInput,output,streaming});
    return true;
}
}
int main() {
#ifdef OPENNOW_HOST_PREVIEW
    const char* scene=std::getenv("OPENNOW_PREVIEW_SCENE");
    const std::string_view name(scene?scene:"signin-code");
    const auto game=[](unsigned i,const char* title,const char* store){
        std::snprintf(publishedCloud.games[i].id,sizeof(publishedCloud.games[i].id),"%u",100+i);
        std::snprintf(publishedCloud.games[i].title,sizeof(publishedCloud.games[i].title),"%s",title);
        std::snprintf(publishedCloud.games[i].store,sizeof(publishedCloud.games[i].store),"%s",store);
    };
    const char* titles[][2]={{"Baldur\xE2\x80\x99s Gate 3","STEAM"},{"Forza Horizon 5","XBOX"},{"Dungeons II","XBOX"},{"Fortnite","EPIC"},
        {"Assassin\xE2\x80\x99s Creed Shadows","UBISOFT"},{"Diablo IV","BATTLE.NET"},{"Cyberpunk 2077","STEAM"},{"Dungeons II","STEAM"},
        {"Hogwarts Legacy","EPIC"},{"Clair Obscur: Expedition 33","XBOX"},{"Hades II","STEAM"},{"The Witcher 3: Wild Hunt","GOG"},
        {"Satisfactory","STEAM"},{"No Man\xE2\x80\x99s Sky","STEAM"}};
    published.state=State::authenticated;published.sessionSaved=true;
    publishedProfileMask=0;
    for(unsigned i=0;i<static_cast<unsigned>(opennow::StreamProfile::count);++i)
        if(!opennow::settingsFor(static_cast<opennow::StreamProfile>(i)).hardware)publishedProfileMask|=1U<<i;
    publishedCloud.state=opennow::CloudState::catalog;publishedCloud.revision=1;focusRevision=1;
    for(unsigned i=0;i<14;++i)game(i,titles[i][0],titles[i][1]);
    publishedCloud.count=14;publishedCloud.hasNext=true;libraryFocus=2;
    std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Choose a game and store");
    if(name.rfind("signin",0)==0) {
        published=opennow::View{};
        if(name=="signin-code") {
            published.state=State::waiting;published.expiresIn=892;
            std::snprintf(published.code,sizeof(published.code),"K7QX-4MZP");
            std::snprintf(published.url,sizeof(published.url),"https://static-login.nvidia.com/service/gfn/pin");
            std::snprintf(published.qrUrl,sizeof(published.qrUrl),"https://static-login.nvidia.com/service/gfn/pin?code=FIXTURE");
        } else if(name=="signin-requesting") {
            published.state=State::requesting;std::snprintf(published.message,sizeof(published.message),"Checking saved NVIDIA login...");
        } else if(name=="signin-failed") {
            published.state=State::failed;std::snprintf(published.message,sizeof(published.message),"Unable to reach NVIDIA (preview fixture: curl error 6)");
        }
    } else if(name=="loading") {
        publishedCloud=opennow::CloudView{};publishedCloud.state=opennow::CloudState::loading;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Loading NVIDIA catalog...");
    } else if(name=="empty") {
        publishedCloud.count=0;publishedCloud.hasNext=false;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"No games found in this catalog page");
    } else if(name=="catalog-error") {
        publishedCloud.state=opennow::CloudState::failed;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Catalog request failed (HTTP 503, preview fixture)");
    } else if(name=="library-row2") {
        libraryFocus=8;
    } else if(name=="library-long") {
        game(12,"Ultra Long Title: The Definitive Remastered Collector\xE2\x80\x99s Edition With Every Expansion","STEAM");
        game(13,"Pok\xC3\xA9mon-free caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xFF bad UTF-8","EPIC GAMES STORE");
        libraryFocus=13;
    } else if(name=="search") {
        searchInput.open=true;std::snprintf(searchInput.text,sizeof(searchInput.text),"DUNGEONS");searchInput.selected=18;
    } else if(name=="detail"||name=="detail-hardware") {
        detailOpen=true;detailProfile=opennow::StreamProfile::quality;
        if(name=="detail-hardware") {
            publishedProfileMask=~0U>>(32-static_cast<unsigned>(opennow::StreamProfile::count));
            detailProfile=opennow::StreamProfile::native_hdr120;
        }
    } else if(name=="starting") {
        publishedCloud.state=opennow::CloudState::starting;publishedCloud.selected=2;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Starting cloud session...");
    } else if(name=="queued") {
        publishedCloud.state=opennow::CloudState::queued;publishedSession=true;publishedCloud.selected=2;publishedCloud.queuePosition=3;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Waiting for server allocation - queue position 3");
    } else if(name=="preparing") {
        publishedCloud.state=opennow::CloudState::queued;publishedSession=true;publishedCloud.selected=2;publishedCloud.setupStep=2;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Server preparing game - setup step 2");
    } else if(name=="connecting") {
        publishedCloud.state=opennow::CloudState::ready;publishedSession=true;publishedCloud.selected=2;publishedStream=true;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Negotiating secure media connection");
    } else if(name=="cleanup-failed") {
        publishedCloud.state=opennow::CloudState::failed;publishedSession=true;publishedCloud.selected=2;publishedStreamFailure=true;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"WebRTC: failed | Cleanup: Unable to stop cloud session");
    } else if(name=="stream-ended") {
        publishedCloud.selected=2;publishedStreamFailure=true;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Signaling disconnected; cloud session stopped");
    }
    ps5::demo::run(draw,"Host preview - fixture data");
    return 0;
#else
    storageError=opennow::appStorage::initialize();
    opennow::gpu::initialize();
    sceNetInit(); sceUserServiceInitialize(nullptr); scePadInit();
    activeHttp=new opennow::Http;
    void* thread=nullptr;
    void* attributes=nullptr;
    int threadResult=scePthreadAttrInit(&attributes);
    if(threadResult==0) {
        // TLS and session negotiation have nested bounded buffers.
        threadResult=scePthreadAttrSetstacksize(&attributes,2*1024*1024);
        if(threadResult==0) threadResult=scePthreadCreate(&thread,&attributes,worker,nullptr,"opennow-auth");
        scePthreadAttrDestroy(&attributes);
    }
    if (threadResult!=0) {
        published.state=State::failed;
        std::snprintf(published.message,sizeof(published.message),"Could not start networking worker");
    }
    ps5::demo::run(draw,"OpenNOW PS5 prototype");
    if(threadResult==0)(void)scePthreadJoin(thread,nullptr);
    opennow::gpu::shutdown();
    if(pad>=0)(void)scePadClose(pad);
    delete activeHttp;activeHttp=nullptr;
    (void)sceSystemServiceLoadExec("exit",nullptr);
    // Do not return through the native C runtime if the system rejects exit.
    for(;;)sceKernelUsleep(1000000);
#endif
}
