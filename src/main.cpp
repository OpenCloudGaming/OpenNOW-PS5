// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_renderer.hpp"
#include "gfn.hpp"
#include "app_storage.h"
#include "settings_file.hpp"
#include "http.hpp"
#include "random.hpp"
#include "cloud.hpp"
#include "catalog_search.hpp"
#include "input/input_queue.hpp"
#include "input/stream_keyboard.hpp"
#include "input/touch_mouse.hpp"
#include "ui/tv_ui.hpp"
#include "ui/artwork.hpp"
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
using opennow::CloudState;
using opennow::CatalogSource;
using opennow::StreamProfile;
using opennow::ui::Section;
using opennow::ui::Screen;
using opennow::ui::SettingsPane;
using ps5::demo::Canvas;
opennow::View published;
opennow::CloudView publishedCloud;
opennow::CloudView publishedLibrary;
PS5_PadData publishedPad{};
#ifndef OPENNOW_HOST_PREVIEW
opennow::Media media;
#endif
bool publishedStream=false;
bool publishedInputReady=false;
bool publishedSession=false;
bool publishedStreamFailure=false;
StreamProfile publishedProfile=StreamProfile::quality;
StreamProfile publishedLaunchProfile=StreamProfile::quality;
opennow::ui::SettingsInfo publishedSettings;
pthread_mutex_t viewMutex=PTHREAD_MUTEX_INITIALIZER;
std::atomic_int command{0};
opennow::Http* activeHttp=nullptr; // Set before UI loop; lifetime is the process.
opennow::art::Cache* artCache=nullptr;
int storageError=0;
int pad=-1;
unsigned lastButtons=0;
opennow::CatalogSearch searchInput;
opennow::InputQueue inputQueue;
opennow::StreamKeyboard keyboard;
opennow::TouchMouse touchMouse;
std::uint8_t sentMouse=0;
unsigned suppressed=0;
bool backHeld=false;
bool padConnected=false;
char pendingSearch[128]{};
char shownSearch[128]{};
unsigned publishedProfileMask=0;
struct Request { int index=-1; int profile=-1; int source=-1; int direction=1; };
Request pendingRequest;
struct Browser { unsigned focus=0; unsigned revision=~0U; int target=0; int pending=-1; char id[96]{}; };
Browser browsers[2];
Section section=Section::library;
bool detailOpen=false;
Section detailSection=Section::library;
StreamProfile detailProfile=StreamProfile::quality;
SettingsPane settingsPane=SettingsPane::stream;
bool settingsContent=false;
unsigned settingsRow=0;
bool confirmSignOut=false;
int pendingDefault=-1;
Request lastLaunch;
bool hasLastLaunch=false;
unsigned artFrame=0;

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
bool acceptProfile(int requested,unsigned mask,StreamProfile& profile) noexcept {
    if(requested<0||requested>=static_cast<int>(StreamProfile::count)||!(mask&(1U<<requested)))return false;
    profile=static_cast<StreamProfile>(requested);
    return true;
}
unsigned qualifiedMask() noexcept {
    unsigned mask=0;
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i) {
        const auto candidate=static_cast<StreamProfile>(i);
        if(!opennow::settingsFor(candidate).hardware||opennow::gpu::profileAvailable(candidate))mask|=1U<<i;
    }
    return mask;
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
    const auto best=opennow::gpu::bestProfile();
#else
    const auto best=StreamProfile::quality;
#endif
    const unsigned profileMask=qualifiedMask();
    auto profile=best;
    auto launchProfile=best;
    opennow::ui::SettingsInfo settings;
    {
        opennow::settingsFile::Saved saved;
        const auto status=opennow::settingsFile::load(OPENNOW_SETTINGS_PATH,saved);
        settings.loadCorrupt=status==opennow::settingsFile::Status::corrupt;
        settings.loadUnreadable=status==opennow::settingsFile::Status::unreadable;
        if(status==opennow::settingsFile::Status::loaded) {
            settings.saved=true;
            if(profileMask&(1U<<static_cast<unsigned>(saved.profile)))profile=saved.profile;
            else settings.savedUnavailable=true;
        }
    }
    pthread_mutex_lock(&viewMutex);publishedProfileMask=profileMask;publishedProfile=profile;publishedSettings=settings;pthread_mutex_unlock(&viewMutex);
    unsigned publishTick=0;
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
        if(action==16&&acceptProfile(request.profile,profileMask,profile)) {
            settings.saveError=opennow::settingsFile::save(OPENNOW_SETTINGS_PATH,{true,profile});
            settings.saved=settings.saveError==0;
            settings.savedUnavailable=settings.loadCorrupt=settings.loadUnreadable=false;
        }
        if(action==17) {
            settings.saveError=opennow::settingsFile::clear(OPENNOW_SETTINGS_PATH);
            profile=best;
            settings.saved=settings.savedUnavailable=settings.loadCorrupt=settings.loadUnreadable=false;
        }
        const auto now=sceKernelGetProcessTime();
        // Renew between games: blocking login HTTPS must not stall media processing.
#ifndef OPENNOW_HOST_PREVIEW
        if (!stream.active()) login.tick(now/1000000);
#else
        login.tick(now/1000000);
#endif
        if(login.view().state==State::authenticated) {
            if(!catalogLoaded) {
                publish(login.view());
                pthread_mutex_lock(&viewMutex);publishedLibrary.state=CloudState::loading;publishedCloud.state=CloudState::loading;pthread_mutex_unlock(&viewMutex);
                cloud.loadPage(CatalogSource::library,0,login.cloudToken(),id);
                pthread_mutex_lock(&viewMutex);publishedLibrary=cloud.library();pthread_mutex_unlock(&viewMutex);
                cloud.load(login.cloudToken(),id,"");
                catalogLoaded=true;
            }
            const bool idle=!*cloud.session().id;
            if(action==5&&idle&&request.index>=0) {
                launchProfile=profile;
                acceptProfile(request.profile,profileMask,launchProfile);
                streamFailure[0]=0;
                pthread_mutex_lock(&viewMutex);
                publishedCloud.state=CloudState::starting;publishedLaunchProfile=launchProfile;
                std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Starting cloud session...");
                pthread_mutex_unlock(&viewMutex);
                cloud.launchEntry(request.source==1?CatalogSource::library:CatalogSource::browse,static_cast<unsigned>(request.index),
                                  login.cloudToken(),id,now/1000000,launchProfile);
                streamAttempted=false;
            }
            if((action==6||action==7)&&idle){streamFailure[0]=0;cloud.loadPage(CatalogSource::browse,request.index>0?static_cast<unsigned>(request.index):0,login.cloudToken(),id,request.direction);}
            if(action==12&&idle)cloud.loadPage(CatalogSource::library,request.index>0?static_cast<unsigned>(request.index):0,login.cloudToken(),id,request.direction);
            if(action==15&&idle){streamFailure[0]=0;cloud.dismissLaunchError();}
            if(action==8&&idle){
                streamFailure[0]=0;
                char search[128];
                pthread_mutex_lock(&viewMutex);std::memcpy(search,pendingSearch,sizeof(search));pthread_mutex_unlock(&viewMutex);
                cloud.load(login.cloudToken(),id,search,false);
            }
            if(const int pending=command.load();pending==2||pending==10||pending==11)continue;
            cloud.tick(login.cloudToken(),id,now/1000000);
            if(const int pending=command.load();pending==2||pending==10||pending==11)continue;
#ifndef OPENNOW_HOST_PREVIEW
            if(cloud.view().state==CloudState::ready&&!streamAttempted){streamAttempted=true;inputQueue.clear();stream.start(cloud.session(),id);}
            if(stream.active()){
                stream.tick(now);PS5_PadData padState{};pthread_mutex_lock(&viewMutex);padState=publishedPad;pthread_mutex_unlock(&viewMutex);stream.input(padState,now);stream.events(inputQueue,now);
            }
            if(stream.failed()) {
                std::snprintf(streamFailure,sizeof(streamFailure),"%s",stream.status());
                stream.stop();streamAttempted=false;
                cloud.stop(login.cloudToken(),id);
            }
#endif
        }
        pthread_mutex_lock(&viewMutex);
        if(!publishedStream||++publishTick%50==0){publishedCloud=cloud.view();publishedLibrary=cloud.library();}
        else {
            publishedCloud.state=cloud.view().state;publishedCloud.queuePosition=cloud.view().queuePosition;
            publishedCloud.setupStep=cloud.view().setupStep;std::memcpy(publishedCloud.message,cloud.view().message,sizeof(publishedCloud.message));
        }
        publishedProfile=profile;publishedSettings=settings;
        publishedSession=*cloud.session().id!=0;
        publishedStreamFailure=*streamFailure!=0;
        if(*streamFailure) {
            if(publishedSession)std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"%.75s | Cleanup: %.100s",streamFailure,cloud.view().message);
            else std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"%s",streamFailure);
        }
#ifndef OPENNOW_HOST_PREVIEW
        publishedStream=stream.active();
        publishedInputReady=stream.inputReady();
        if(streamAttempted)std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"%s",stream.status());
#endif
        pthread_mutex_unlock(&viewMutex);
        // A cancellation during a blocking request must win over its response.
        if (command.load()==2||command.load()==10||command.load()==11) continue;
        publish(login.view());
        sceKernelUsleep(publishedStream?2000:100000);
    }
}

Browser& browserFor(Section s) noexcept {return browsers[s==Section::library?0:1];}
void syncBrowser(Section s,const opennow::CloudView& view) noexcept {
    Browser& b=browserFor(s);
    if(view.revision!=b.revision) {
        b.revision=view.revision;
        unsigned next=0;
        if(b.target==2&&view.count)next=view.count-1;
        else if(b.target==0&&*b.id)
            for(unsigned i=0;i<view.count;++i)if(!std::strcmp(view.games[i].id,b.id)){next=i;break;}
        b.focus=next;b.target=0;b.pending=-1;
        if(detailOpen&&detailSection==s)detailOpen=false;
    }
    if(view.count&&b.focus>=view.count)b.focus=view.count-1;
    if(view.count)std::snprintf(b.id,sizeof(b.id),"%s",view.games[b.focus].id);
}
void requestPage(Section s,unsigned page,int target,int direction=1) noexcept {
    Browser& b=browserFor(s);
    b.target=target;b.pending=static_cast<int>(page);
    submit(s==Section::library?12:7,{static_cast<int>(page),-1,-1,direction});
}
void launchFocused(Section s,int profile) noexcept {
    lastLaunch={static_cast<int>(browserFor(s).focus),profile,s==Section::library?1:0};
    hasLastLaunch=true;
    submit(5,lastLaunch);
    detailOpen=false;
}
StreamProfile shiftProfile(unsigned mask,StreamProfile current,int delta,bool wrap) noexcept {
    StreamProfile options[static_cast<unsigned>(StreamProfile::count)];
    const unsigned count=opennow::ui::availableProfiles(mask,options,static_cast<unsigned>(StreamProfile::count));
    if(!count)return current;
    unsigned at=0;
    for(unsigned i=0;i<count;++i)if(options[i]==current)at=i;
    if(delta<0)at=at>0?at-1:(wrap?count-1:0);
    else if(delta>0)at=at+1<count?at+1:(wrap?0:count-1);
    return options[at];
}

bool draw(ps5::demo::Canvas& c) noexcept {
    if (pad<0) {
        int user=-1;
        if (sceUserServiceGetInitialUser(&user)==0) pad=scePadOpen(user,0,0,nullptr);
    }
    PS5_PadData data{};
    unsigned pressed=0;
    if (pad>=0 && scePadReadState(pad,&data)==0 && data.connected) {
        pressed=data.buttons & ~lastButtons; lastButtons=data.buttons;
    } else {lastButtons=0;data=PS5_PadData{};}
    static opennow::View v;
    static opennow::CloudView cv,lv;
    bool streaming=false,inputReady=false,sessionOwned=false,streamFailed=false;
    StreamProfile profile,launchProfile;unsigned profileMask=0;opennow::ui::SettingsInfo settings;
    pthread_mutex_lock(&viewMutex);
    v=published;cv=publishedCloud;lv=publishedLibrary;streaming=publishedStream;inputReady=publishedInputReady;sessionOwned=publishedSession;
    streamFailed=publishedStreamFailure;profile=publishedProfile;launchProfile=publishedLaunchProfile;profileMask=publishedProfileMask;settings=publishedSettings;
    pthread_mutex_unlock(&viewMutex);
    if(pendingDefault>=0&&static_cast<int>(profile)==pendingDefault)pendingDefault=-1;
    const StreamProfile defaultProfile=pendingDefault>=0?static_cast<StreamProfile>(pendingDefault):profile;
    const bool input=pressed!=0;
    if(artCache)artCache->setPaused(streaming||sessionOwned||cv.state==CloudState::starting||cv.state==CloudState::queued||cv.state==CloudState::ready);
    syncBrowser(Section::library,lv);
    syncBrowser(Section::browse,cv);
    if(v.state!=State::authenticated||streaming||sessionOwned){searchInput.open=false;detailOpen=false;confirmSignOut=false;}
    const bool typing=keyboard.open;
    const bool padLost=padConnected&&!data.connected;
    padConnected=data.connected;
    if(padLost||!streaming)keyboard.hide();
    suppressed&=data.buttons;
    if(streaming&&(data.buttons&PS5_PAD_BUTTON_OPTIONS)&&(pressed&PS5_PAD_BUTTON_TRIANGLE)) {
        if(keyboard.open)keyboard.hide();
        else keyboard.show();
        pressed&=~PS5_PAD_BUTTON_TRIANGLE;
    } else if(keyboard.open) {
        if(pressed&PS5_PAD_BUTTON_LEFT)keyboard.move(-1,0);
        if(pressed&PS5_PAD_BUTTON_RIGHT)keyboard.move(1,0);
        if(pressed&PS5_PAD_BUTTON_UP)keyboard.move(0,-1);
        if(pressed&PS5_PAD_BUTTON_DOWN)keyboard.move(0,1);
        if(inputReady&&(pressed&PS5_PAD_BUTTON_CROSS))keyboard.press(keyboard.selected,inputQueue);
        if(inputReady&&(pressed&PS5_PAD_BUTTON_SQUARE))keyboard.press(opennow::StreamKeyboard::backspaceKey,inputQueue);
        if(inputReady&&(pressed&PS5_PAD_BUTTON_TRIANGLE))keyboard.press(opennow::StreamKeyboard::spaceKey,inputQueue);
        if(pressed&PS5_PAD_BUTTON_L1)keyboard.shift=!keyboard.shift;
        if(pressed&PS5_PAD_BUTTON_CIRCLE)keyboard.hide();
        pressed&=PS5_PAD_BUTTON_TOUCH_PAD;
    } else if(streaming&&(data.buttons&PS5_PAD_BUTTON_OPTIONS)&&(pressed&PS5_PAD_BUTTON_SQUARE)) {
        backHeld=true;suppressed|=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_SQUARE;pressed&=~PS5_PAD_BUTTON_SQUARE;
    }
    if(typing&&!keyboard.open)suppressed|=data.buttons;
    if(padLost||typing!=keyboard.open){inputQueue.cancel();sentMouse=0;}
    backHeld=backHeld&&streaming&&!keyboard.open&&(data.buttons&PS5_PAD_BUTTON_SQUARE);
    PS5_PadData gamePad=data;
    gamePad.buttons=(data.buttons&~(PS5_PAD_BUTTON_TOUCH_PAD|suppressed))|(backHeld?PS5_PAD_BUTTON_TOUCH_PAD:0);
    if(keyboard.open)gamePad=PS5_PadData{};
    const auto motion=touchMouse.update(data,streaming&&inputReady&&!keyboard.open);
    inputQueue.move(motion.dx,motion.dy);
    if(sentMouse&&sentMouse!=touchMouse.held&&inputQueue.button(sentMouse,false))sentMouse=0;
    if(!sentMouse&&touchMouse.held&&inputQueue.button(touchMouse.held,true))sentMouse=touchMouse.held;
    pthread_mutex_lock(&viewMutex);publishedPad=gamePad;pthread_mutex_unlock(&viewMutex);
    const auto screenNow=[&]{return opennow::ui::screenFor({v,cv,lv,section,streaming,sessionOwned,streamFailed,searchInput.open,detailOpen});};
    Screen screen=screenNow();
    if(screen!=Screen::detail)detailOpen=false;
    if(screen!=Screen::settings)confirmSignOut=false;
    const bool topLevel=screen==Screen::grid||screen==Screen::catalogEmpty||screen==Screen::catalogError||
                        screen==Screen::catalogLoading||screen==Screen::settings;
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
            std::memcpy(shownSearch,searchInput.text,sizeof(shownSearch));
            browserFor(Section::browse).target=1;
            command.store(8);searchInput.open=false;section=Section::browse;
        }
        pressed=0;
    } else if(confirmSignOut) {
        if(pressed&PS5_PAD_BUTTON_CROSS){confirmSignOut=false;settingsContent=false;if(activeHttp)activeHttp->cancelled.store(true);command.store(11);}
        else if(pressed&PS5_PAD_BUTTON_CIRCLE)confirmSignOut=false;
        pressed=0;
    } else if(detailOpen&&(pressed&PS5_PAD_BUTTON_CIRCLE)) {
        detailOpen=false;pressed&=~PS5_PAD_BUTTON_CIRCLE;
    } else if(screen==Screen::settings&&settingsContent&&(pressed&PS5_PAD_BUTTON_CIRCLE)) {
        settingsContent=false;pressed&=~PS5_PAD_BUTTON_CIRCLE;
    } else if(screen==Screen::launchFailed&&(pressed&PS5_PAD_BUTTON_CIRCLE)) {
        submit(15,{});pressed&=~PS5_PAD_BUTTON_CIRCLE;
    } else if(topLevel&&screen!=Screen::settings&&(pressed&PS5_PAD_BUTTON_TRIANGLE)) {
        searchInput.open=true;section=Section::browse;pressed=0;
    }
    if(!streaming&&(pressed&PS5_PAD_BUTTON_CIRCLE)&&activeHttp) {
        activeHttp->cancelled.store(true);command.store(10);pressed=0;
    } else if ((streaming||sessionOwned||cv.state==CloudState::starting||cv.state==CloudState::queued)&&
        (data.buttons&PS5_PAD_BUTTON_OPTIONS)&&(pressed&PS5_PAD_BUTTON_TOUCH_PAD) && activeHttp) {
        activeHttp->cancelled.store(true); command.store(2);
    } else if (!streaming && (pressed&PS5_PAD_BUTTON_CROSS) && v.state!=State::waiting && v.state!=State::requesting && v.state!=State::authenticated) command.store(1);
    if(v.state==State::authenticated&&!streaming&&!sessionOwned&&pressed) {
        if(screen==Screen::streamEnded) {
            if((pressed&PS5_PAD_BUTTON_CROSS)&&hasLastLaunch)submit(5,lastLaunch);
            else if(pressed&PS5_PAD_BUTTON_SQUARE)submit(15,{});
        } else if(screen==Screen::launchFailed) {
            if((pressed&PS5_PAD_BUTTON_CROSS)&&hasLastLaunch)submit(5,lastLaunch);
        } else if(screen==Screen::detail) {
            const auto& view=opennow::ui::catalogFor(detailSection,cv,lv);
            Browser& b=browserFor(detailSection);
            if(pressed&PS5_PAD_BUTTON_UP)detailProfile=shiftProfile(profileMask,detailProfile,-1,false);
            if(pressed&PS5_PAD_BUTTON_DOWN)detailProfile=shiftProfile(profileMask,detailProfile,1,false);
            if(pressed&PS5_PAD_BUTTON_L1)detailProfile=shiftProfile(profileMask,detailProfile,1,true);
            unsigned siblings[16];
            const unsigned siblingCount=opennow::ui::storeSiblings(view,b.focus,siblings,16);
            for(unsigned i=0;i<siblingCount;++i)if(siblings[i]==b.focus) {
                if((pressed&PS5_PAD_BUTTON_LEFT)&&i>0)b.focus=siblings[i-1];
                else if((pressed&PS5_PAD_BUTTON_RIGHT)&&i+1<siblingCount)b.focus=siblings[i+1];
                break;
            }
            if(pressed&PS5_PAD_BUTTON_CROSS)launchFocused(detailSection,static_cast<int>(detailProfile));
        } else if(topLevel) {
            if((pressed&(PS5_PAD_BUTTON_L1|PS5_PAD_BUTTON_R1))&&!settingsContent) {
                const int next=static_cast<int>(section)+((pressed&PS5_PAD_BUTTON_R1)?1:-1);
                section=static_cast<Section>(std::clamp(next,0,2));
                if(section!=Section::settings) {
                    const auto& view=opennow::ui::catalogFor(section,cv,lv);
                    if(view.state==CloudState::idle&&!view.count)requestPage(section,0,1);
                }
            } else if(screen==Screen::settings) {
                const unsigned rows=opennow::ui::settingsRows(settingsPane);
                if(!settingsContent) {
                    const int pane=static_cast<int>(settingsPane);
                    if(pressed&PS5_PAD_BUTTON_UP)settingsPane=static_cast<SettingsPane>(std::max(pane-1,0));
                    if(pressed&PS5_PAD_BUTTON_DOWN)settingsPane=static_cast<SettingsPane>(std::min(pane+1,static_cast<int>(opennow::ui::settingsPanes)-1));
                    if((pressed&(PS5_PAD_BUTTON_RIGHT|PS5_PAD_BUTTON_CROSS))&&rows){settingsContent=true;settingsRow=0;}
                } else {
                    if((pressed&PS5_PAD_BUTTON_UP)&&settingsRow>0)--settingsRow;
                    if((pressed&PS5_PAD_BUTTON_DOWN)&&settingsRow+1<rows)++settingsRow;
                    const bool valueRow=settingsPane==SettingsPane::stream&&settingsRow==0;
                    if(valueRow&&(pressed&(PS5_PAD_BUTTON_LEFT|PS5_PAD_BUTTON_RIGHT))) {
                        const auto next=shiftProfile(profileMask,defaultProfile,(pressed&PS5_PAD_BUTTON_RIGHT)?1:-1,false);
                        if(next!=defaultProfile){pendingDefault=static_cast<int>(next);submit(16,{-1,pendingDefault,-1});}
                    } else if(pressed&PS5_PAD_BUTTON_LEFT)settingsContent=false;
                    if(pressed&PS5_PAD_BUTTON_CROSS) {
                        if(settingsPane==SettingsPane::stream&&settingsRow==1){pendingDefault=-1;submit(17,{});}
                        if(settingsPane==SettingsPane::account&&settingsRow==0)confirmSignOut=true;
                    }
                }
            } else {
                const auto& view=opennow::ui::catalogFor(section,cv,lv);
                Browser& b=browserFor(section);
                const unsigned columns=opennow::ui::gridColumns;
                if(view.state==CloudState::catalog&&view.count) {
                    const unsigned rows=(view.count+columns-1)/columns;
                    if(pressed&PS5_PAD_BUTTON_LEFT){if(b.focus>0)--b.focus;else if(view.page>0)requestPage(section,view.page-1,2,-1);}
                    if(pressed&PS5_PAD_BUTTON_RIGHT){if(b.focus+1<view.count)++b.focus;else if(view.hasNext)requestPage(section,view.page+1,1);}
                    if(pressed&PS5_PAD_BUTTON_UP){if(b.focus>=columns)b.focus-=columns;else if(view.page>0)requestPage(section,view.page-1,2,-1);}
                    if(pressed&PS5_PAD_BUTTON_DOWN){
                        if(b.focus/columns+1<rows)b.focus=std::min(b.focus+columns,view.count-1);
                        else if(view.hasNext)requestPage(section,view.page+1,1);
                    }
                    if(pressed&PS5_PAD_BUTTON_CROSS){detailOpen=true;detailSection=section;detailProfile=defaultProfile;}
                } else if(pressed&PS5_PAD_BUTTON_CROSS) {
                    if(view.state==CloudState::failed)requestPage(section,b.pending>=0?static_cast<unsigned>(b.pending):view.page,view.count?1:0);
                    else if(screen==Screen::catalogEmpty&&view.hasNext)requestPage(section,view.page+1,1);
                    else if(screen==Screen::catalogEmpty&&section==Section::library) {
                        section=Section::browse;
                        if(cv.state==CloudState::idle&&!cv.count)requestPage(Section::browse,0,1);
                    }
                }
                if((pressed&PS5_PAD_BUTTON_SQUARE)&&view.state!=CloudState::loading)requestPage(section,view.page,0);
            }
        }
    }
    screen=keyboard.open?Screen::keyboard:screenNow();
    if(confirmSignOut&&screen!=Screen::settings)confirmSignOut=false;
#ifndef OPENNOW_HOST_PREVIEW
    if(streaming&&media.frames.load()&&!keyboard.open)return media.draw(c);
#endif
    static opennow::CloudView previousCloud,previousLibrary;
    static opennow::View previous;
    static StreamProfile previousProfile=StreamProfile::quality;
    static unsigned previousMask=0,previousArt=~0U;
    static opennow::ui::SettingsInfo previousSettings;
    static bool first=true,previousReady=false,previousKeyboard=false;
    const unsigned artGeneration=artCache?artCache->generation():0;
    if(!first&&!input&&artGeneration==previousArt&&std::memcmp(&previous,&v,sizeof(v))==0&&
       std::memcmp(&previousCloud,&cv,sizeof(cv))==0&&std::memcmp(&previousLibrary,&lv,sizeof(lv))==0&&
       previousProfile==defaultProfile&&previousMask==profileMask&&std::memcmp(&previousSettings,&settings,sizeof(settings))==0&&
       previousReady==inputReady&&previousKeyboard==keyboard.open)
        return false;
    first=false;previous=v;previousCloud=cv;previousLibrary=lv;previousProfile=defaultProfile;previousMask=profileMask;
    previousSettings=settings;previousArt=artGeneration;previousReady=inputReady;previousKeyboard=keyboard.open;
#ifndef OPENNOW_HOST_PREVIEW
    const char* output=opennow::gpu::outputLabel();
#else
    const char* output="Host preview \xC2\xB7 no video output";
#endif
    const Section shownSection=screen==Screen::detail?detailSection:section;
    opennow::ui::render(c,{screen,shownSection,v,cv,lv,screen==Screen::detail?detailProfile:defaultProfile,launchProfile,profileMask,
        browserFor(shownSection).focus,searchInput,shownSearch,output,streaming,settingsPane,settingsContent,settingsRow,confirmSignOut,
        settings,artCache,++artFrame,keyboard,inputReady});
    return true;
}
#ifdef OPENNOW_HOST_PREVIEW
bool previewFetch(void*,const char* url,unsigned char* buffer,std::size_t capacity,std::size_t& size,const std::atomic_bool&) noexcept {
    size=0;
    const char* apps=std::strstr(url,"/apps/");
    if(!apps)return false;
    char name[256];
    const int prefix=std::snprintf(name,sizeof(name),"build/preview-art/");
    std::snprintf(name+prefix,sizeof(name)-prefix,"%s",apps+6);
    for(char* p=name+prefix;*p;++p)if(*p=='/'||*p==';'||*p=='=')*p='_';
    FILE* file=std::fopen(name,"rb");
    if(!file)return false;
    size=std::fread(buffer,1,capacity,file);
    const bool ok=size>0&&size<capacity;
    std::fclose(file);
    return ok;
}
#endif
}
int main() {
#ifdef OPENNOW_HOST_PREVIEW
    const char* scene=std::getenv("OPENNOW_PREVIEW_SCENE");
    const std::string_view name(scene?scene:"library");
    const auto game=[](opennow::CloudView& view,unsigned i,const char* title,const char* store,const char* art,const char* hero,bool owned){
        auto& g=view.games[i];
        std::snprintf(g.id,sizeof(g.id),"%u",100+i);
        std::snprintf(g.title,sizeof(g.title),"%s",title);
        std::snprintf(g.store,sizeof(g.store),"%s",store);
        std::snprintf(g.art,sizeof(g.art),"%s",art);
        std::snprintf(g.hero,sizeof(g.hero),"%s",hero);
        g.owned=owned;
    };
    const char* covers[][4]={
        {"Fallout 3 Game of the Year Edition","EPIC","https://img.nvidiagrid.net/apps/101611411/ZZ/GAME_BOX_ART_01_1f4d5087-b764-4dc8-9b23-5a3e42d8fd4e.jpg","https://img.nvidiagrid.net/apps/101611411/ZZ/HERO_IMAGE_01_7b84d3a8-1e36-4f45-9f82-11de22a0105f.jpg"},
        {"Lords of the Fallen","EPIC","https://img.nvidiagrid.net/apps/101238711/ZZ/GAME_BOX_ART_01_967e038e-78ce-45bf-9d89-adb8c7c7abb1.jpg","https://img.nvidiagrid.net/apps/101238711/ZZ/HERO_IMAGE_01_652bc3b4-bf4a-4531-bdc2-d95acee16d15.jpg"},
        {"MORDHAU","EPIC","https://img.nvidiagrid.net/apps/100886511/ZZ/GAME_BOX_ART_01_69d32e41-2ca2-4bde-bc17-dc819da911dd.jpg","https://img.nvidiagrid.net/apps/100886511/ZZ/HERO_IMAGE_01_505d79e5-ac1d-456a-8909-4d176179aa63.jpg"},
        {"Tower of Fantasy","STEAM","https://img.nvidiagrid.net/apps/102328711/ZZ/GAME_BOX_ART_01_f28bd852-eb9a-4ea6-906d-285b080fb9d6.jpg","https://img.nvidiagrid.net/apps/102328711/ZZ/HERO_IMAGE_01_1d900eb6-5732-4132-96c8-b0cfa5c8fdb7.jpg"}};
    const char* extra[][2]={{"Dungeons II","XBOX"},{"Dungeons II","STEAM"},{"Cyberpunk 2077","STEAM"},{"Hades II","STEAM"},{"Satisfactory","STEAM"},
        {"No Man\xE2\x80\x99s Sky","STEAM"},{"Hogwarts Legacy","EPIC"},{"Clair Obscur: Expedition 33","XBOX"},{"The Witcher 3: Wild Hunt","GOG"},{"Diablo IV","BATTLE.NET"}};
    published.state=State::authenticated;published.sessionSaved=true;
    publishedProfileMask=0;
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i)
        if(!opennow::settingsFor(static_cast<StreamProfile>(i)).hardware)publishedProfileMask|=1U<<i;
    for(unsigned i=0;i<4;++i)game(publishedLibrary,i,covers[i][0],covers[i][1],covers[i][2],covers[i][3],true);
    game(publishedLibrary,4,"Dungeons II","XBOX","https://img.nvidiagrid.net/apps/100000011/ZZ/GAME_BOX_ART_01_preview-missing.jpg","",true);
    game(publishedLibrary,5,"Dungeons II","STEAM","","",true);
    publishedLibrary.state=CloudState::catalog;publishedLibrary.count=6;publishedLibrary.revision=1;
    for(unsigned i=0;i<4;++i)game(publishedCloud,i,covers[i][0],covers[i][1],covers[i][2],covers[i][3],i==0);
    for(unsigned i=0;i<10;++i)game(publishedCloud,4+i,extra[i][0],extra[i][1],"","",false);
    publishedCloud.state=CloudState::catalog;publishedCloud.count=14;publishedCloud.revision=1;publishedCloud.hasNext=true;
    publishedCloud.current=publishedLibrary.games[0];
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
    } else if(name=="library-loading") {
        publishedLibrary=opennow::CloudView{};publishedLibrary.state=CloudState::loading;
    } else if(name=="library-scanning") {
        publishedLibrary.count=0;publishedLibrary.hasNext=true;publishedLibrary.page=12;
    } else if(name=="library-empty") {
        publishedLibrary.count=0;std::snprintf(publishedLibrary.message,sizeof(publishedLibrary.message),"No games in your library");
    } else if(name=="library-error") {
        publishedLibrary=opennow::CloudView{};publishedLibrary.state=CloudState::failed;
        std::snprintf(publishedLibrary.message,sizeof(publishedLibrary.message),"Library request failed (HTTP 503, preview fixture)");
    } else if(name=="library-page-error") {
        publishedLibrary.state=CloudState::failed;publishedLibrary.hasNext=true;
        std::snprintf(publishedLibrary.message,sizeof(publishedLibrary.message),"Library request failed (HTTP 502, preview fixture)");
        browsers[0].focus=5;browsers[0].pending=1;
    } else if(name=="browse"||name=="browse-row2"||name=="browse-long") {
        section=Section::browse;
        browsers[1].focus=name=="browse"?1:8;
        if(name=="browse-long") {
            game(publishedCloud,12,"Ultra Long Title: The Definitive Remastered Collector\xE2\x80\x99s Edition With Every Expansion","STEAM","","",false);
            game(publishedCloud,13,"Pok\xC3\xA9mon-free caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xFF bad UTF-8","EPIC GAMES STORE","","",false);
            browsers[1].focus=13;
        }
    } else if(name=="browse-loading-more") {
        section=Section::browse;publishedCloud.state=CloudState::loading;browsers[1].focus=13;browsers[1].target=1;browsers[1].pending=1;
    } else if(name=="search") {
        section=Section::browse;searchInput.open=true;std::snprintf(searchInput.text,sizeof(searchInput.text),"DUNGEONS");searchInput.selected=18;
    } else if(name=="detail"||name=="detail-hardware") {
        detailOpen=true;detailSection=Section::library;detailProfile=StreamProfile::quality;
        if(name=="detail-hardware") {
            publishedProfileMask=~0U>>(32-static_cast<unsigned>(StreamProfile::count));
            detailProfile=StreamProfile::native_hdr120;
        }
    } else if(name.rfind("settings",0)==0) {
        section=Section::settings;
        publishedSettings.saved=true;
        if(name=="settings-stream"){settingsContent=true;settingsRow=0;}
        if(name=="settings-display")settingsPane=SettingsPane::display;
        if(name=="settings-account"||name=="settings-signout"){settingsPane=SettingsPane::account;settingsContent=true;}
        if(name=="settings-signout")confirmSignOut=true;
        if(name=="settings-about")settingsPane=SettingsPane::about;
        if(name=="settings-save-error"){settingsContent=true;publishedSettings.saved=false;publishedSettings.saveError=28;}
    } else if(name=="starting") {
        publishedCloud.state=CloudState::starting;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Starting cloud session...");
    } else if(name=="queued") {
        publishedCloud.state=CloudState::queued;publishedSession=true;publishedCloud.queuePosition=3;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Waiting for server allocation - queue position 3");
    } else if(name=="connecting") {
        publishedCloud.state=CloudState::ready;publishedSession=true;publishedStream=true;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Negotiating secure media connection");
    } else if(name.rfind("keyboard",0)==0) {
        publishedCloud.state=CloudState::ready;publishedSession=true;publishedStream=true;publishedInputReady=name!="keyboard-waiting";
        keyboard.show();
        keyboard.selected=33;
        if(name=="keyboard-modifiers"){keyboard.caps=true;keyboard.ctrl=true;keyboard.selected=30;}
    } else if(name=="cleanup-failed") {
        publishedCloud.state=CloudState::failed;publishedSession=true;publishedStreamFailure=true;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"WebRTC: failed | Cleanup: Unable to stop cloud session");
    } else if(name=="stream-ended") {
        publishedStreamFailure=true;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Signaling disconnected; cloud session stopped");
    } else if(name=="launch-failed") {
        publishedCloud.state=CloudState::failed;publishedCloud.launchError=true;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"INTERNAL_ERROR_STATUS (statusCode 4, preview fixture)");
    }
    browsers[0].revision=publishedLibrary.revision;browsers[1].revision=publishedCloud.revision;
    static opennow::art::Cache cache(previewFetch,nullptr);
    artCache=&cache;
    const auto& primed=section==Section::browse?publishedCloud:publishedLibrary;
    for(unsigned i=0;i<primed.count;++i) {
        cache.want(primed.games[i].art,opennow::art::Kind::tile,1);
        cache.want(primed.games[i].hero,opennow::art::Kind::hero,1);
    }
    if(name!="library-art-loading")while(cache.step()){}
    ps5::demo::run(draw,"Host preview - fixture data");
    return 0;
#else
    storageError=opennow::appStorage::initialize();
    opennow::gpu::initialize();
    sceNetInit(); sceUserServiceInitialize(nullptr); scePadInit();
    activeHttp=new opennow::Http;
    static opennow::art::Cache cache(opennow::art::httpsFetch,nullptr);
    void* thread=nullptr;
    void* artThread=nullptr;
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
    int artResult=-1;
    if(cache.ready()&&activeHttp->ready()&&scePthreadAttrInit(&attributes)==0) {
        artResult=scePthreadAttrSetstacksize(&attributes,1024*1024);
        if(artResult==0)artResult=scePthreadCreate(&artThread,&attributes,&opennow::art::Cache::run,&cache,"opennow-art");
        scePthreadAttrDestroy(&attributes);
    }
    if(artResult==0)artCache=&cache;
    ps5::demo::run(draw,"OpenNOW PS5 prototype");
    cache.requestStop();
    if(artResult==0)(void)scePthreadJoin(artThread,nullptr);
    if(threadResult==0)(void)scePthreadJoin(thread,nullptr);
    artCache=nullptr;
    opennow::gpu::shutdown();
    if(pad>=0)(void)scePadClose(pad);
    delete activeHttp;activeHttp=nullptr;
    (void)sceSystemServiceLoadExec("exit",nullptr);
    // Do not return through the native C runtime if the system rejects exit.
    for(;;)sceKernelUsleep(1000000);
#endif
}
