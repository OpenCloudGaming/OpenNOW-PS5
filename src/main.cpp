// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_renderer.hpp"
#include "gfn.hpp"
#include "app_storage.h"
#include "settings_file.hpp"
#include "ui/stream_store.hpp"
#include "http.hpp"
#include "random.hpp"
#include "cloud.hpp"
#include "catalog_search.hpp"
#include "input/input_queue.hpp"
#include "input/stream_keyboard.hpp"
#include "input/touch_mouse.hpp"
#include "input/launcher_mouse.hpp"
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
opennow::StreamSettings publishedDefaults{};
opennow::StreamSettings publishedLaunch{};
bool artworkWanted=true;
opennow::ui::SettingsInfo publishedSettings;
opennow::ui::AudioInfo publishedAudio;
unsigned audioCapacity=2;
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
opennow::LauncherMouse launcher;
unsigned sentMouse=0;
unsigned suppressed=0;
bool backHeld=false;
bool padConnected=false;
char pendingSearch[128]{};
char shownSearch[128]{};
struct Request { int index=-1; int source=-1; int direction=1; bool hasSettings=false; opennow::StreamSettings settings{}; };
Request pendingRequest;
struct Browser { unsigned focus=0; unsigned revision=~0U; int target=0; int pending=-1; char id[96]{}; };
Browser browsers[2];
Section section=Section::library;
bool detailOpen=false;
Section detailSection=Section::library;
unsigned detailChoice=0;
SettingsPane settingsPane=SettingsPane::stream;
bool settingsContent=false;
unsigned settingsRow=0;
bool confirmSignOut=false;
opennow::StreamSettings draft{};
opennow::StreamSettings proposal{};
unsigned draftRevision=0;
unsigned adjusted=0;
opennow::ui::NumberEdit numberEdit;
bool confirmClear=false;
bool confirmFixed=false;
Request lastLaunch;
bool hasLastLaunch=false;
unsigned artFrame=0;

#ifdef OPENNOW_HOST_PREVIEW
bool previewVideo=false;
void previewVideoFrame(Canvas& c) noexcept {
    for(unsigned y=0;y<1080;y+=4) {
        const unsigned t=y*255/1080;
        const auto color=static_cast<ps5::demo::Color>(0xff000000U|((0x3A+t/3)<<16)|((0x4A+t/5)<<8)|(0x55+t/6));
        c.rectangle(0,y,1920,4,color);
    }
}
#endif
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
bool acceptSettings(const opennow::StreamSettings& settings) noexcept {
    return opennow::validateSettings(settings)==opennow::SettingsError::none&&opennow::gpu::settingsAvailable(settings);
}
opennow::ui::StreamCaps streamCaps() noexcept {
    using opennow::VideoMode;
    const auto mode=[](VideoMode m){return opennow::gpu::settingsAvailable(opennow::ui::modeProbe(m));};
    return {mode(VideoMode::h264Hardware),mode(VideoMode::hevcMain10SdrHardware),mode(VideoMode::hevcMain10HdrHardware)};
}
unsigned launchChoices(const opennow::StreamSettings& defaults,opennow::StreamSettings* out,unsigned capacity) noexcept {
    StreamProfile presets[static_cast<unsigned>(StreamProfile::count)];
    const unsigned count=opennow::ui::qualifiedPresets(presets,static_cast<unsigned>(StreamProfile::count),opennow::gpu::settingsAvailable);
    unsigned n=0;
    out[n++]=defaults;
    for(unsigned i=0;i<count&&n<capacity;++i)out[n++]=opennow::ui::presetFor(presets[i],defaults);
    return n;
}
void rejectLaunch(opennow::CloudView& view,const char* reason,const opennow::Game& game) noexcept {
    view.launchError=true;view.current=game;
    std::snprintf(view.message,sizeof(view.message),"Not started: %s",reason);
}
void setArtwork(bool enabled) noexcept {
    pthread_mutex_lock(&viewMutex);const bool changed=artworkWanted!=enabled;artworkWanted=enabled;pthread_mutex_unlock(&viewMutex);
    if(changed&&artCache)artCache->setDiskEnabled(enabled);
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
    opennow::ui::StreamStore store;
    pthread_mutex_lock(&viewMutex);const bool artworkStart=artworkWanted;pthread_mutex_unlock(&viewMutex);
    store.load(OPENNOW_SETTINGS_PATH,opennow::gpu::bestSettings(),artworkStart,opennow::gpu::settingsAvailable);
    auto launchSettings=store.defaults;
    char rejected[128]{};
    opennow::Game rejectedGame{};
    pthread_mutex_lock(&viewMutex);publishedDefaults=store.defaults;publishedSettings=store.info;pthread_mutex_unlock(&viewMutex);
    unsigned publishTick=0;
    for (;;) {
        Request request;
        const int action=takeCommand(request);
        if(action==10||action==11) {
            http.cancelled.store(false);
#ifndef OPENNOW_HOST_PREVIEW
            stream.stop();
#endif
            streamAttempted=false;rejected[0]=0;
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
        if(action==16&&request.hasSettings)store.save(OPENNOW_SETTINGS_PATH,request.settings,opennow::gpu::settingsAvailable);
        if(action==17)store.reset(OPENNOW_SETTINGS_PATH);
        {
            pthread_mutex_lock(&viewMutex);const bool wanted=artworkWanted;pthread_mutex_unlock(&viewMutex);
            store.artwork(OPENNOW_SETTINGS_PATH,wanted);
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
                const auto chosen=store.launch(request.hasSettings,request.settings,opennow::gpu::settingsAvailable);
                streamFailure[0]=0;rejected[0]=0;
                if(!chosen) {
                    const auto& list=request.source==1?cloud.library():cloud.view();
                    rejectedGame=static_cast<unsigned>(request.index)<list.count?list.games[request.index]:opennow::Game{};
                    std::snprintf(rejected,sizeof(rejected),"%s",opennow::ui::settingsProblem(request.settings,opennow::gpu::settingsAvailable(request.settings)));
                    pthread_mutex_lock(&viewMutex);publishedLaunch=request.settings;pthread_mutex_unlock(&viewMutex);
                } else {
                    launchSettings=*chosen;
                    pthread_mutex_lock(&viewMutex);
                    publishedCloud.state=CloudState::starting;publishedLaunch=launchSettings;
                    std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Starting cloud session...");
                    pthread_mutex_unlock(&viewMutex);
                    const unsigned audioChannels=opennow::audio::requestedChannels(launchSettings.audio_mode,audioCapacity);
                    pthread_mutex_lock(&viewMutex);publishedAudio={audioCapacity,audioChannels,0,false};pthread_mutex_unlock(&viewMutex);
                    cloud.launchEntry(request.source==1?CatalogSource::library:CatalogSource::browse,static_cast<unsigned>(request.index),
                                      login.cloudToken(),id,now/1000000,launchSettings,audioChannels);
                    streamAttempted=false;
                }
            }
            if((action==6||action==7)&&idle){streamFailure[0]=0;cloud.loadPage(CatalogSource::browse,request.index>0?static_cast<unsigned>(request.index):0,login.cloudToken(),id,request.direction);}
            if(action==12&&idle)cloud.loadPage(CatalogSource::library,request.index>0?static_cast<unsigned>(request.index):0,login.cloudToken(),id,request.direction);
            if(action==15&&idle){streamFailure[0]=0;rejected[0]=0;cloud.dismissLaunchError();}
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
        publishedDefaults=store.defaults;publishedSettings=store.info;
#ifndef OPENNOW_HOST_PREVIEW
        if(stream.active()){publishedAudio.negotiated=media.audioChannels.load();publishedAudio.fallback=media.audioStereoFallbacks.load()>0;}
#endif
        if(*rejected)rejectLaunch(publishedCloud,rejected,rejectedGame);
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
    submit(s==Section::library?12:7,{static_cast<int>(page),-1,direction});
}
void launchFocused(Section s,const opennow::StreamSettings& settings) noexcept {
    lastLaunch={static_cast<int>(browserFor(s).focus),s==Section::library?1:0,1,true,settings};
    hasLastLaunch=true;
    submit(5,lastLaunch);
    detailOpen=false;
}
void proposeDraft(const opennow::StreamSettings& next) noexcept {
    if(opennow::ui::needsFixedNetwork(next)&&!opennow::ui::needsFixedNetwork(draft)) {
        proposal=next;proposal.network=opennow::NetworkPolicy::fixed;confirmFixed=true;
        return;
    }
    draft=next;
}
void draftCommands(unsigned pressed) noexcept {
    if(pressed&PS5_PAD_BUTTON_SQUARE){draft=publishedDefaults;adjusted=0;}
    if((pressed&PS5_PAD_BUTTON_OPTIONS)&&opennow::ui::changedRows(draft,publishedDefaults)&&!opennow::ui::settingsProblem(draft,acceptSettings(draft)))
        submit(16,{-1,-1,1,true,draft});
}
void editStream(unsigned pressed,const opennow::ui::StreamCaps& caps) noexcept {
    using opennow::ui::StreamRow;
    const auto row=static_cast<StreamRow>(settingsRow);
    const int direction=(pressed&PS5_PAD_BUTTON_RIGHT)?1:(pressed&PS5_PAD_BUTTON_LEFT)?-1:0;
    const int common=(pressed&PS5_PAD_BUTTON_R1)?1:(pressed&PS5_PAD_BUTTON_L1)?-1:0;
    const auto before=draft;
    if(direction&&row==StreamRow::preset) {
        StreamProfile presets[static_cast<unsigned>(StreamProfile::count)];
        const unsigned count=opennow::ui::qualifiedPresets(presets,static_cast<unsigned>(StreamProfile::count),opennow::gpu::settingsAvailable);
        int at=-1;
        for(unsigned i=0;i<count;++i)if(opennow::ui::presetFor(presets[i],draft)==draft)at=static_cast<int>(i);
        const int next=at<0?(direction>0?0:static_cast<int>(count)-1):std::clamp(at+direction,0,static_cast<int>(count)-1);
        if(count)proposeDraft(opennow::ui::presetFor(presets[next],draft));
    }
    if(direction&&row==StreamRow::decoding)proposeDraft(opennow::ui::withHardware(draft,direction<0,caps));
    if(direction&&row==StreamRow::codec)proposeDraft(opennow::ui::withHevc(draft,direction>0,caps));
    if(direction&&row==StreamRow::hdr)draft=opennow::ui::withHdr(draft,direction>0,caps);
    if(direction&&(row==StreamRow::fps||row==StreamRow::bitrate))draft=opennow::ui::stepValue(draft,row,direction);
    if((direction&&row==StreamRow::resolution)||(common&&(row==StreamRow::resolution||row==StreamRow::fps||row==StreamRow::bitrate)))
        draft=opennow::ui::stepCommon(draft,row,direction?direction:common);
    if(pressed&PS5_PAD_BUTTON_CROSS) {
        if(row==StreamRow::reset)submit(17,{});
        else numberEdit=opennow::ui::beginEdit(row,draft);
    }
    draftCommands(pressed);
    if(draft==before)return;
    adjusted=row==StreamRow::decoding&&!draft.hardware()?opennow::ui::rowCount(opennow::ui::changedRows(before,draft)&~opennow::ui::rowBit(StreamRow::decoding)):0;
}
void editField(unsigned pressed) noexcept {
    auto& e=numberEdit;
    if((pressed&PS5_PAD_BUTTON_LEFT)&&e.cursor>0)--e.cursor;
    if((pressed&PS5_PAD_BUTTON_RIGHT)&&e.cursor+1<e.length)++e.cursor;
    if(pressed&PS5_PAD_BUTTON_UP)opennow::ui::changeDigit(e,1);
    if(pressed&PS5_PAD_BUTTON_DOWN)opennow::ui::changeDigit(e,-1);
    if(pressed&(PS5_PAD_BUTTON_L1|PS5_PAD_BUTTON_R1))
        opennow::ui::setEdit(e,opennow::ui::stepCommon(opennow::ui::applyEdit(e,draft),e.row,(pressed&PS5_PAD_BUTTON_R1)?1:-1));
    if(pressed&PS5_PAD_BUTTON_CROSS) {
        const auto candidate=opennow::ui::applyEdit(e,draft);
        if(opennow::validateSettings(candidate)==opennow::SettingsError::none){if(!(candidate==draft))adjusted=0;draft=candidate;e={};}
    }
    if(pressed&PS5_PAD_BUTTON_CIRCLE)e={};
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
    opennow::StreamSettings defaults,launch;opennow::ui::SettingsInfo settings;opennow::ui::AudioInfo audio;
    pthread_mutex_lock(&viewMutex);
    v=published;cv=publishedCloud;lv=publishedLibrary;streaming=publishedStream;inputReady=publishedInputReady;sessionOwned=publishedSession;
    streamFailed=publishedStreamFailure;defaults=publishedDefaults;launch=publishedLaunch;settings=publishedSettings;audio=publishedAudio;
    pthread_mutex_unlock(&viewMutex);
    if(settings.revision!=draftRevision){draft=defaults;draftRevision=settings.revision;adjusted=0;numberEdit={};confirmFixed=false;}
    const auto caps=streamCaps();
    opennow::StreamSettings choices[1+static_cast<unsigned>(StreamProfile::count)];
    const unsigned choiceCount=launchChoices(defaults,choices,1+static_cast<unsigned>(StreamProfile::count));
    if(detailChoice>=choiceCount)detailChoice=0;
    const bool input=pressed!=0;
    if(artCache)artCache->setPaused(streaming||sessionOwned||cv.state==CloudState::starting||cv.state==CloudState::queued||cv.state==CloudState::ready);
    syncBrowser(Section::library,lv);
    syncBrowser(Section::browse,cv);
    if(v.state!=State::authenticated||streaming||sessionOwned){searchInput.open=false;detailOpen=false;confirmSignOut=false;}
    const bool typing=keyboard.open, pointing=launcher.active;
    const bool padLost=padConnected&&!data.connected;
    padConnected=data.connected;
    if(padLost||!streaming){keyboard.hide();launcher.active=false;}
    suppressed&=data.buttons;
    const bool options=data.buttons&PS5_PAD_BUTTON_OPTIONS;
    if(streaming&&options&&(pressed&PS5_PAD_BUTTON_TRIANGLE)) {
        if(keyboard.open)keyboard.hide();
        else keyboard.show();
        pressed&=~PS5_PAD_BUTTON_TRIANGLE;
    } else if(streaming&&options&&(pressed&PS5_PAD_BUTTON_R3)) {
        launcher.active=!launcher.active;keyboard.hide();
        suppressed|=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_R3;pressed&=~PS5_PAD_BUTTON_R3;
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
    } else if(streaming&&options&&(pressed&PS5_PAD_BUTTON_SQUARE)) {
        backHeld=true;suppressed|=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_SQUARE;pressed&=~PS5_PAD_BUTTON_SQUARE;
    } else if(launcher.active) {
        if(pressed&PS5_PAD_BUTTON_TRIANGLE)keyboard.show();
        if(pressed&PS5_PAD_BUTTON_SQUARE)launcher.cycleSpeed();
        pressed&=PS5_PAD_BUTTON_TOUCH_PAD;
    }
    if((typing&&!keyboard.open)||(pointing&&!launcher.active))suppressed|=data.buttons;
    if(padLost||typing!=keyboard.open||pointing!=launcher.active){inputQueue.cancel();sentMouse=0;launcher.reset();}
    backHeld=backHeld&&streaming&&!keyboard.open&&(data.buttons&PS5_PAD_BUTTON_SQUARE);
    PS5_PadData gamePad=data;
    gamePad.buttons=(data.buttons&~(PS5_PAD_BUTTON_TOUCH_PAD|suppressed))|(backHeld?PS5_PAD_BUTTON_TOUCH_PAD:0);
    if(keyboard.open)gamePad=PS5_PadData{};
    else if(launcher.active) {
        gamePad=PS5_PadData{};gamePad.connected=data.connected;
        gamePad.leftStick={128,128};gamePad.rightStick={128,128};
        gamePad.buttons=backHeld?PS5_PAD_BUTTON_TOUCH_PAD:0;
    }
    const auto now=sceKernelGetProcessTime();
    const bool pointerInput=streaming&&inputReady&&!keyboard.open;
    const auto motion=touchMouse.update(data,pointerInput);
    const auto pointer=launcher.update(data,now,pointerInput);
    inputQueue.move(motion.dx+pointer.dx,motion.dy+pointer.dy);
    if((pointer.wheelX||pointer.wheelY)&&inputQueue.wheel(pointer.wheelX,pointer.wheelY))launcher.wheelSent(now);
    const unsigned wanted=((touchMouse.held==opennow::TouchMouse::left||pointer.left)?1U<<1:0U)|
                          ((touchMouse.held==opennow::TouchMouse::right||pointer.right)?1U<<3:0U);
    for(const std::uint8_t b:{std::uint8_t(1),std::uint8_t(3)}) {
        const unsigned bit=1U<<b;
        if(((wanted^sentMouse)&bit)&&inputQueue.button(b,(wanted&bit)!=0))sentMouse^=bit;
    }
    pthread_mutex_lock(&viewMutex);publishedPad=gamePad;pthread_mutex_unlock(&viewMutex);
    const auto screenNow=[&]{return opennow::ui::screenFor({v,cv,lv,section,streaming,sessionOwned,streamFailed,searchInput.open,detailOpen});};
    Screen screen=screenNow();
    if(screen!=Screen::detail)detailOpen=false;
    if(screen!=Screen::settings){confirmSignOut=confirmClear=confirmFixed=false;numberEdit={};}
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
    } else if(numberEdit.open()) {
        editField(pressed);pressed=0;
    } else if(confirmFixed) {
        if(pressed&PS5_PAD_BUTTON_CROSS){draft=proposal;adjusted=0;confirmFixed=false;}
        else if(pressed&PS5_PAD_BUTTON_CIRCLE)confirmFixed=false;
        pressed=0;
    } else if(confirmClear) {
        if(pressed&PS5_PAD_BUTTON_CROSS){if(artCache)artCache->requestDiskClear();confirmClear=false;}
        else if(pressed&PS5_PAD_BUTTON_CIRCLE)confirmClear=false;
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
            if((pressed&PS5_PAD_BUTTON_UP)&&detailChoice>0)--detailChoice;
            if((pressed&PS5_PAD_BUTTON_DOWN)&&detailChoice+1<choiceCount)++detailChoice;
            if(pressed&PS5_PAD_BUTTON_L1)detailChoice=(detailChoice+1)%choiceCount;
            unsigned siblings[16];
            const unsigned siblingCount=opennow::ui::storeSiblings(view,b.focus,siblings,16);
            for(unsigned i=0;i<siblingCount;++i)if(siblings[i]==b.focus) {
                if((pressed&PS5_PAD_BUTTON_LEFT)&&i>0)b.focus=siblings[i-1];
                else if((pressed&PS5_PAD_BUTTON_RIGHT)&&i+1<siblingCount)b.focus=siblings[i+1];
                break;
            }
            if(pressed&PS5_PAD_BUTTON_CROSS)launchFocused(detailSection,choices[detailChoice]);
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
                    if(settingsPane==SettingsPane::stream)editStream(pressed,caps);
                    else if(settingsPane==SettingsPane::display) {
                        const int direction=(pressed&PS5_PAD_BUTTON_RIGHT)?1:(pressed&PS5_PAD_BUTTON_LEFT)?-1:0;
                        if(direction)draft.audio_mode=static_cast<opennow::audio::Mode>(std::clamp(static_cast<int>(draft.audio_mode)+direction,0,static_cast<int>(opennow::audio::Mode::surround71)));
                        draftCommands(pressed);
                    }
                    else if(settingsPane==SettingsPane::cache) {
                        if(settingsRow==0&&(pressed&(PS5_PAD_BUTTON_LEFT|PS5_PAD_BUTTON_RIGHT)))setArtwork(pressed&PS5_PAD_BUTTON_RIGHT);
                        if(settingsRow==0&&(pressed&PS5_PAD_BUTTON_CROSS))setArtwork(!artworkWanted);
                        const auto disk=artCache?artCache->diskStats():opennow::art::DiskStats{};
                        if(settingsRow==1&&(pressed&PS5_PAD_BUTTON_CROSS)&&artCache&&!disk.busy&&(!disk.available||disk.count))confirmClear=true;
                        if((pressed&PS5_PAD_BUTTON_LEFT)&&settingsRow==1)settingsContent=false;
                    } else {
                        if(pressed&PS5_PAD_BUTTON_LEFT)settingsContent=false;
                        if((pressed&PS5_PAD_BUTTON_CROSS)&&settingsPane==SettingsPane::account&&settingsRow==0)confirmSignOut=true;
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
                    if(pressed&PS5_PAD_BUTTON_CROSS){detailOpen=true;detailSection=section;detailChoice=0;}
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
    const bool overlay=streaming&&(keyboard.open||launcher.active);
    static bool previousOverlay=false,previousOverlayReady=false,previousStreaming=false;
    const bool overlayChanged=overlay&&(input||!previousOverlay||previousOverlayReady!=inputReady);
    [[maybe_unused]] const bool overlayClosed=previousOverlay&&!overlay;
    previousOverlay=overlay;previousOverlayReady=inputReady;
    const opennow::ui::Overlay overlayModel{keyboard,launcher.active,launcher.speed,inputReady};
#ifndef OPENNOW_HOST_PREVIEW
    if(streaming!=previousStreaming){opennow::gpu::invalidateVideo();previousStreaming=streaming;}
    if(streaming&&media.frames.load()) {
        const bool hardware=media.hardwareVideo();
        if(hardware)opennow::gpu::setOverlay(overlay,overlayChanged);
        const bool pending=hardware&&opennow::gpu::overlayPending();
        if(!media.draw(c,overlayChanged||overlayClosed||pending)&&!pending)return false;
        if(overlay&&(!hardware||overlayChanged)) {
            if(hardware)c.clear(static_cast<ps5::demo::Color>(0));
            opennow::ui::renderOverlay(c,overlayModel);
        }
        return true;
    }
#else
    previousStreaming=streaming;
    if(streaming&&previewVideo) {
        previewVideoFrame(c);
        if(overlay)opennow::ui::renderOverlay(c,overlayModel);
        return true;
    }
#endif
    static opennow::CloudView previousCloud,previousLibrary;
    static opennow::View previous;
    static opennow::StreamSettings previousDefaults,previousLaunch;
    static unsigned previousArt=~0U;
    static opennow::ui::SettingsInfo previousSettings;
    static opennow::ui::AudioInfo previousAudio;
    static opennow::art::DiskStats previousDisk;
    static bool first=true,previousReady=false,previousKeyboard=false;
    const unsigned artGeneration=artCache?artCache->generation():0;
    const auto disk=artCache?artCache->diskStats():opennow::art::DiskStats{};
    const bool diskShown=screen==Screen::settings&&settingsPane==SettingsPane::cache;
    const bool diskSame=!diskShown||(disk.enabled==previousDisk.enabled&&disk.available==previousDisk.available&&disk.busy==previousDisk.busy&&
                                     disk.bytes==previousDisk.bytes&&disk.count==previousDisk.count&&disk.error==previousDisk.error);
    if(!first&&!input&&artGeneration==previousArt&&std::memcmp(&previous,&v,sizeof(v))==0&&
       std::memcmp(&previousCloud,&cv,sizeof(cv))==0&&std::memcmp(&previousLibrary,&lv,sizeof(lv))==0&&
       previousDefaults==defaults&&previousLaunch==launch&&std::memcmp(&previousSettings,&settings,sizeof(settings))==0&&previousAudio==audio&&
       previousReady==inputReady&&previousKeyboard==keyboard.open&&diskSame)
        return false;
    first=false;previous=v;previousCloud=cv;previousLibrary=lv;previousDefaults=defaults;previousLaunch=launch;previousDisk=disk;
    previousSettings=settings;previousAudio=audio;previousArt=artGeneration;previousReady=inputReady;previousKeyboard=keyboard.open;
#ifndef OPENNOW_HOST_PREVIEW
    const char* output=opennow::gpu::outputLabel();
#else
    const char* output="Host preview \xC2\xB7 no video output";
#endif
    const Section shownSection=screen==Screen::detail?detailSection:section;
    opennow::ui::render(c,{screen,shownSection,v,cv,lv,defaults,launch,choices,choiceCount,detailChoice,
        browserFor(shownSection).focus,searchInput,shownSearch,output,streaming,settingsPane,settingsContent,settingsRow,confirmSignOut,
        settings,artCache,++artFrame,keyboard,inputReady,draft,caps,acceptSettings(draft),adjusted,numberEdit,
        numberEdit.open()&&acceptSettings(opennow::ui::applyEdit(numberEdit,draft)),disk,artworkWanted,confirmClear,proposal,confirmFixed,audio});
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
    publishedDefaults=publishedLaunch=opennow::gpu::bestSettings();
    publishedSettings.revision=draftRevision=1;
    draft=publishedDefaults;
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
        detailOpen=true;detailSection=Section::library;detailChoice=name=="detail-hardware"?2:0;
    } else if(name.rfind("settings",0)==0) {
        section=Section::settings;
        publishedSettings.saved=true;
        if(name=="settings-stream"){settingsContent=true;settingsRow=0;}
        if(name.rfind("settings-display",0)==0){settingsPane=SettingsPane::display;publishedAudio={8,0,0,false};}
        if(name=="settings-display-audio"){settingsContent=true;draft.audio_mode=opennow::audio::Mode::surround51;publishedAudio={8,8,2,true};}
        if(name=="settings-display-negotiated")publishedAudio={8,8,2,false};
        if(name=="settings-account"||name=="settings-signout"){settingsPane=SettingsPane::account;settingsContent=true;}
        if(name=="settings-signout")confirmSignOut=true;
        if(name=="settings-about")settingsPane=SettingsPane::about;
        if(name=="settings-save-error"){settingsContent=true;publishedSettings.saved=false;publishedSettings.saveError=28;}
        using opennow::ui::StreamRow;
        const auto custom=opennow::StreamSettings{2560,1440,90,75000,opennow::VideoMode::hevcMain10HdrHardware,opennow::QualityMode::original,opennow::NetworkPolicy::fixed};
        if(name.rfind("settings-stream-",0)==0){settingsContent=true;draft=custom;}
        if(name=="settings-stream-custom")settingsRow=static_cast<unsigned>(StreamRow::decoding);
        if(name=="settings-stream-software"){const auto before=draft;draft=opennow::ui::withHardware(draft,false,streamCaps());adjusted=opennow::ui::rowCount(opennow::ui::changedRows(before,draft)&~opennow::ui::rowBit(StreamRow::decoding));settingsRow=static_cast<unsigned>(StreamRow::decoding);}
        if(name=="settings-stream-editor"){settingsRow=static_cast<unsigned>(StreamRow::resolution);numberEdit=opennow::ui::beginEdit(StreamRow::resolution,draft);std::memcpy(numberEdit.digits+4,"1081",4);}
        if(name=="settings-stream-unqualified"){draft=opennow::settingsFor(StreamProfile::native_hdr120);settingsRow=static_cast<unsigned>(StreamRow::fps);}
        if(name=="settings-stream-fixed"){draft=opennow::settingsFor(StreamProfile::experimental);proposal=opennow::ui::withHardware(draft,true,streamCaps());proposal.network=opennow::NetworkPolicy::fixed;confirmFixed=true;settingsRow=static_cast<unsigned>(StreamRow::decoding);}
        if(name.rfind("settings-cache",0)==0){settingsPane=SettingsPane::cache;settingsContent=true;settingsRow=1;}
        if(name=="settings-cache-clear")confirmClear=true;
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
    } else if(name.rfind("overlay-",0)==0) {
        publishedCloud.state=CloudState::ready;publishedSession=true;publishedStream=true;publishedInputReady=name!="overlay-waiting";previewVideo=true;
        launcher.active=name!="overlay-keyboard-only";
        if(name=="overlay-keyboard"||name=="overlay-keyboard-only"||name=="overlay-waiting"){keyboard.show();keyboard.selected=33;keyboard.shift=name=="overlay-keyboard";}
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
    static opennow::art::Cache cache(previewFetch,nullptr,"build/preview-cache");
    artCache=&cache;
    if(name=="settings-cache-off"){artworkWanted=false;cache.setDiskEnabled(false);}
    const auto& primed=section==Section::browse?publishedCloud:publishedLibrary;
    for(unsigned i=0;i<primed.count;++i) {
        cache.want(primed.games[i].art,opennow::art::Kind::tile,1);
        cache.want(primed.games[i].hero,opennow::art::Kind::hero,1);
    }
    if(name!="library-art-loading")while(cache.step()){}
    if(name.rfind("settings-cache",0)==0) {
        for(unsigned i=0;i<4;++i){cache.want(covers[i][2],opennow::art::Kind::tile,2);cache.want(covers[i][3],opennow::art::Kind::hero,2);}
        while(cache.step()){}
    }
    ps5::demo::run(draw,"Host preview - fixture data");
    return 0;
#else
    storageError=opennow::appStorage::initialize();
    opennow::gpu::initialize();
    audioCapacity=opennow::Media::availableAudioChannels();
    publishedAudio.capacity=audioCapacity;
    sceNetInit(); sceUserServiceInitialize(nullptr); scePadInit();
    activeHttp=new opennow::Http;
    static opennow::art::Cache cache(opennow::art::httpsFetch,nullptr,storageError?nullptr:OPENNOW_ARTWORK_CACHE_PATH);
    {
        opennow::settingsFile::Saved saved;
        if(opennow::settingsFile::load(OPENNOW_SETTINGS_PATH,saved)==opennow::settingsFile::Status::loaded)artworkWanted=saved.artworkCache;
        cache.setDiskEnabled(artworkWanted);
    }
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
