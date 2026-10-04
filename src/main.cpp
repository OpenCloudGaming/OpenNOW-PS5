// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_renderer.hpp"
#include "gfn.hpp"
#include "account_file.hpp"
#include "http.hpp"
#include "random.hpp"
#include "cloud.hpp"
#include "stream/native/gpu_presenter.hpp"
#ifndef OPENNOW_HOST_PREVIEW
#include "stream/stream.hpp"
#endif
#include "vendor/qrcodegen.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
extern "C" {
#include "platform/ps5-pad.h"
int sceUserServiceGetInitialUser(int*);
int sceNetInit();
int sceKernelUsleep(unsigned);
unsigned long long sceKernelGetProcessTime();
int scePthreadCreate(void**,const void*,void* (*)(void*),void*,const char*);
int scePthreadAttrInit(void**);
int scePthreadAttrSetstacksize(void**,std::size_t);
int scePthreadAttrDestroy(void**);
}
namespace {
using opennow::State;
opennow::View published;
opennow::CloudView publishedCloud;
PS5_PadData publishedPad{};
#ifndef OPENNOW_HOST_PREVIEW
opennow::Media media;
#endif
bool publishedStream=false;
opennow::StreamProfile publishedProfile=opennow::StreamProfile::quality;
pthread_mutex_t viewMutex=PTHREAD_MUTEX_INITIALIZER;
std::atomic_int command{0};
opennow::Http* activeHttp=nullptr; // Set before UI loop; lifetime is the process.
int pad=-1;
unsigned lastButtons=0;
void publish(const opennow::View& v) {
    pthread_mutex_lock(&viewMutex); published=v; pthread_mutex_unlock(&viewMutex);
}
void* worker(void*) {
    auto& http=*activeHttp;
    // One process-lifetime worker owns these large bounded objects.
    static opennow::Login login(opennow::Http::request,&http,"/data/opennow/account.bin");
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
    if (opennow::accountFile::makeDirectory("/data/opennow",0700)==0)
        opennow::accountFile::syncParent("/data/opennow");
    opennow::View restoring; restoring.state=State::requesting;
    std::snprintf(restoring.message,sizeof(restoring.message),"Checking saved NVIDIA login...");
    publish(restoring);
    login.restore(id,sceKernelGetProcessTime()/1000000);
    bool catalogLoaded=false,streamAttempted=false;
#ifndef OPENNOW_HOST_PREVIEW
    auto profile=opennow::gpu::bestProfile();
#else
    auto profile=opennow::StreamProfile::quality;
#endif
    for (;;) {
        const int action=command.exchange(0);
        if (action==2) {
            http.cancelled.store(false);
#ifndef OPENNOW_HOST_PREVIEW
            stream.stop();
#endif
            if(*cloud.session().id) { cloud.stop(login.cloudToken(),id);streamAttempted=false; }
            else {login.cancel();cloud.reset();catalogLoaded=false;}
        }
        if (action==1) {
            http.cancelled.store(false);
            cloud.reset();catalogLoaded=false;streamAttempted=false;
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
            if(!catalogLoaded){publish(login.view());pthread_mutex_lock(&viewMutex);publishedCloud.state=opennow::CloudState::loading;std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Loading NVIDIA catalog...");pthread_mutex_unlock(&viewMutex);cloud.load(login.cloudToken(),id,"Minecraft");catalogLoaded=true;}
            if(action==9&&cloud.view().state==opennow::CloudState::catalog) {
                do {profile=opennow::nextProfile(profile);} while(opennow::settingsFor(profile).hardware&&!opennow::gpu::profileAvailable(profile));
            }
            if(action==3)cloud.select(-1);
            if(action==4)cloud.select(1);
            if(action==5){
                // Personal launch configuration is separate from the distributable title.
                int age=-1;char trailing=0;
                if(FILE* config=std::fopen("/data/opennow/launch-age.txt","r")){
                    if(std::fscanf(config,"%d %c",&age,&trailing)!=1)age=-1;
                    std::fclose(config);
                }
                cloud.launch(login.cloudToken(),id,now/1000000,age,profile);streamAttempted=false;
            }
            if(action==6)cloud.load(login.cloudToken(),id,"",false);
            if(action==7&&cloud.view().hasNext)cloud.load(login.cloudToken(),id,"",true);
            if(action==8)cloud.load(login.cloudToken(),id,"Minecraft",false);
            cloud.tick(login.cloudToken(),id,now/1000000);
#ifndef OPENNOW_HOST_PREVIEW
            if(cloud.view().state==opennow::CloudState::ready&&!streamAttempted){streamAttempted=true;stream.start(cloud.session(),id);}
            if(stream.active()){
                stream.tick(now);PS5_PadData padState{};pthread_mutex_lock(&viewMutex);padState=publishedPad;pthread_mutex_unlock(&viewMutex);stream.input(padState,now);
            }
#endif
        }
        pthread_mutex_lock(&viewMutex);publishedCloud=cloud.view();publishedProfile=profile;
#ifndef OPENNOW_HOST_PREVIEW
        publishedStream=stream.active();
        if(streamAttempted)std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"%s",stream.status());
#endif
        pthread_mutex_unlock(&viewMutex);
        // A cancellation during a blocking request must win over its response.
        if (command.load()==2) continue;
        publish(login.view());
        sceKernelUsleep(publishedStream?2000:100000);
    }
}
bool draw(ps5::demo::Canvas& c) noexcept {
    using ps5::demo::Color;
    if (pad<0) {
        int user=-1;
        if (sceUserServiceGetInitialUser(&user)==0) pad=scePadOpen(user,0,0,nullptr);
    }
    PS5_PadData data{};
    unsigned pressed=0;
    if (pad>=0 && scePadReadState(pad,&data)==0 && data.connected) {
        pressed=data.buttons & ~lastButtons; lastButtons=data.buttons;
    } else lastButtons=0;
    opennow::View v;opennow::CloudView cv;bool streaming=false;opennow::StreamProfile profile;
    pthread_mutex_lock(&viewMutex);v=published;cv=publishedCloud;publishedPad=data;streaming=publishedStream;profile=publishedProfile;pthread_mutex_unlock(&viewMutex);
    if (((!streaming&&(pressed&PS5_PAD_BUTTON_CIRCLE))||(streaming&&(data.buttons&PS5_PAD_BUTTON_OPTIONS)&&(pressed&PS5_PAD_BUTTON_TOUCH_PAD))) && activeHttp) {
        activeHttp->cancelled.store(true); command.store(2);
    } else if (!streaming && (pressed&PS5_PAD_BUTTON_CROSS) && v.state!=State::waiting && v.state!=State::requesting && v.state!=State::authenticated) command.store(1);
    if(v.state==State::authenticated&&!streaming) {
        if(cv.state==opennow::CloudState::catalog&&(pressed&PS5_PAD_BUTTON_L1))command.store(9);
        if(pressed&PS5_PAD_BUTTON_UP)command.store(3);
        if(pressed&PS5_PAD_BUTTON_DOWN)command.store(4);
        if(pressed&PS5_PAD_BUTTON_CROSS)command.store(5);
        if(pressed&PS5_PAD_BUTTON_SQUARE)command.store(6);
        if(pressed&PS5_PAD_BUTTON_R1)command.store(7);
        if(pressed&PS5_PAD_BUTTON_TRIANGLE)command.store(8);
    }
#ifndef OPENNOW_HOST_PREVIEW
    if(streaming&&media.frames.load())return media.draw(c);
#endif
    static opennow::CloudView previousCloud;
    static opennow::View previous;
    static auto previousProfile=opennow::StreamProfile::quality;
    static bool first=true;
    if (!first && std::memcmp(&previous,&v,sizeof(v))==0&&std::memcmp(&previousCloud,&cv,sizeof(cv))==0&&previousProfile==profile) return false;
    first=false; previous=v;previousCloud=cv;previousProfile=profile;
    const auto bg=static_cast<Color>(0xff1c1610), green=static_cast<Color>(0xff9ee656);
    c.clear(bg);
    c.text(100,80,"OPENNOW",12,Color::white);
    c.text(105,205,"PS5 CLOUD GAMING - DEVELOPMENT",4,green);
    c.rectangle(100,280,1720,4,green);
    // Wrap bounded status text without truncating the useful network error.
    std::string_view message(v.state==State::authenticated?cv.message:v.message);
    unsigned row=0;
    while (!message.empty()) {
        auto length=message.size()>65 ? 65 : message.size();
        c.text(100,340+row*48,message.substr(0,length),4,Color::white);
        message.remove_prefix(length); ++row;
    }
    if (v.state==State::waiting) {
        c.text(100,490,"ENTER THIS CODE",4,green);
        c.text(100,555,v.code,8,Color::white);
        c.text(100,675,v.url,3,Color::white);
        char remaining[80]; std::snprintf(remaining,sizeof(remaining),"EXPIRES IN %u SECONDS",v.expiresIn);
        c.text(100,755,remaining,3,green);
        static unsigned char temp[qrcodegen_BUFFER_LEN_MAX],qr[qrcodegen_BUFFER_LEN_MAX];
        if (qrcodegen_encodeText(v.qrUrl,temp,qr,qrcodegen_Ecc_MEDIUM,1,20,qrcodegen_Mask_AUTO,true)) {
            const int size=qrcodegen_getSize(qr); const unsigned scale=420U/static_cast<unsigned>(size+8);
            c.rectangle(1310,450,static_cast<unsigned>(size+8)*scale,static_cast<unsigned>(size+8)*scale,Color::white);
            for (int y=0;y<size;++y) for (int x=0;x<size;++x)
                if (qrcodegen_getModule(qr,x,y)) c.rectangle(1310+(x+4)*scale,450+(y+4)*scale,scale,scale,bg);
        }
    } else if (v.state==State::authenticated) {
        if(cv.state==opennow::CloudState::catalog){
            unsigned page=cv.selected/7*7;
            for(unsigned i=page;i<cv.count&&i<page+7;++i){char line[256];std::snprintf(line,sizeof(line),"%s %.52s / %s",i==cv.selected?">":" ",cv.games[i].title,cv.games[i].store);c.text(100,490+(i-page)*52,line,3,i==cv.selected?green:Color::white);}
        } else c.text(100,570,"CIRCLE  STOP SESSION / BACK",4,green);

    }
    if(v.state==State::authenticated){
        char quality[128];std::snprintf(quality,sizeof(quality),"L1 QUALITY: %s",opennow::profileLabel(profile));
        c.text(100,855,quality,3,green);
    }
    c.text(100,905,v.state==State::authenticated?"CROSS PLAY  SQUARE CATALOG  TRIANGLE MINECRAFT  R1 NEXT  CIRCLE SIGN OUT":"CROSS SIGN IN  CIRCLE CANCEL",3,Color::white);
#ifndef OPENNOW_HOST_PREVIEW
    c.text(100,955,opennow::gpu::outputLabel(),3,green);
#else
    c.text(100,955,"SDR / STEREO",3,green);
#endif
    c.text(100,995,v.state==State::authenticated ?
        (v.sessionSaved ? "ACCOUNT SAVED - CLOSE WITH THE PS MENU" :
            "ACCOUNT NOT SAVED - CLOSE WITH THE PS MENU") :
        "UNOFFICIAL CLIENT - CLOSE WITH THE PS MENU",3,green);
    return true;
}
}
int main() {
#ifdef OPENNOW_HOST_PREVIEW
    published.state=State::waiting;
    std::snprintf(published.code,sizeof(published.code),"DEMO-CODE");
    std::snprintf(published.url,sizeof(published.url),"https://static-login.nvidia.com/service/gfn/pin");
    std::snprintf(published.qrUrl,sizeof(published.qrUrl),"https://static-login.nvidia.com/service/gfn/pin");
    std::snprintf(published.message,sizeof(published.message),"Scan the QR code with your phone and sign in");
    published.expiresIn=900;
    if(std::getenv("OPENNOW_PREVIEW_CATALOG")){
        published.state=State::authenticated;published.sessionSaved=true;publishedCloud.state=opennow::CloudState::catalog;publishedCloud.count=2;
        std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Choose a game and store (preview fixtures)");
        std::snprintf(publishedCloud.games[0].title,sizeof(publishedCloud.games[0].title),"Example Game");std::snprintf(publishedCloud.games[0].store,sizeof(publishedCloud.games[0].store),"XBOX");
        std::snprintf(publishedCloud.games[1].title,sizeof(publishedCloud.games[1].title),"Example Game");std::snprintf(publishedCloud.games[1].store,sizeof(publishedCloud.games[1].store),"STEAM");
    }
    ps5::demo::run(draw,"Host preview - fixture data");
#else
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
#endif
}
