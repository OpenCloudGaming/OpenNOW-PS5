// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol adapted from pinned OpenNOW-Switch gfn/catalog and cloud_session.
#include "cloud.hpp"
#include "vendor/cJSON.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
namespace opennow {
namespace {
const char* str(const cJSON* j,const char* k) {auto* v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:"";}
const cJSON* obj(const cJSON* j,const char* k) {return cJSON_GetObjectItemCaseSensitive(j,k);}
int num(const cJSON* j,const char* k,int fallback=0) {auto* v=obj(j,k);return cJSON_IsNumber(v)?v->valueint:fallback;}
bool put(char* out,std::size_t cap,const char* in) {if(!in||std::strlen(in)>=cap)return false;std::memcpy(out,in,std::strlen(in)+1);return true;}
template<std::size_t N> bool copy(char (&out)[N],const char* in){return put(out,N,in);}
void wipeJson(cJSON* v) {for(;v;v=v->next){if(v->valuestring)secureErase(v->valuestring,std::strlen(v->valuestring));wipeJson(v->child);}}
struct Json {cJSON* p;explicit Json(const Response& r):p(r.body?cJSON_ParseWithLengthOpts(r.body,r.length+1,nullptr,true):nullptr){}~Json(){wipeJson(p);cJSON_Delete(p);}};
#define OPENNOW_APP_FIELDS "pageInfo{hasNextPage endCursor}items{id title images{GAME_BOX_ART KEY_ART HERO_IMAGE TV_BANNER} variants{id appStore gfn{status library{status}}}}"
const char* searchQuery=R"(query GetSearchFilterResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$searchString:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"itemMetadata.relevance:DESC,sortName:ASC",first:$fetchCount,after:$cursor,searchQuery:$searchString,filters:$filters){)" OPENNOW_APP_FIELDS "}}";
const char* browseQuery=R"(query GetFilterBrowseResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"itemMetadata.relevance:DESC,sortName:ASC",first:$fetchCount,after:$cursor,filters:$filters){)" OPENNOW_APP_FIELDS "}}";
const char* libraryQuery=R"(query GetFilterBrowseResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"variants.gfn.library.lastPlayedDate:DESC,computedValues.libraryAddedDate:DESC,sortName:ASC",first:$fetchCount,after:$cursor,filters:$filters){)" OPENNOW_APP_FIELDS "}}";
#undef OPENNOW_APP_FIELDS
void pickArtwork(char* out,std::size_t capacity,const cJSON* images,std::initializer_list<const char*> keys) {
    out[0]=0;
    for(const char* key:keys)if(artworkUrl(str(images,key))&&put(out,capacity,str(images,key)))return;
}
bool safeId(const char* s) {if(!*s)return false;for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'))return false;return true;}
bool connectionAddress(const cJSON* connection,char* host,std::size_t capacity,int& port,const char* fallback="") {
    const auto* ip=obj(connection,"ip");if(cJSON_IsArray(ip))ip=cJSON_GetArrayItem(ip,0);
    if(!put(host,capacity,cJSON_IsString(ip)?ip->valuestring:fallback))return false;
    port=num(connection,"port");
    if(const char* scheme=std::strstr(str(connection,"resourcePath"),"://")) {
        const char* start=scheme+3;
        const char* end=start;
        if(*start=='['){end=std::strchr(start,']');if(!end)return false;++end;}
        else while(*end&&*end!=':'&&*end!='/')++end;
        if(end==start||end-start>253)return false;
        for(const char* p=start;p<end;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'||*p==':'||*p=='['||*p==']'))return false;
        if(!cJSON_IsString(ip)) {
            if(static_cast<std::size_t>(end-start)>=capacity)return false;
            std::memcpy(host,start,end-start);host[end-start]=0;
        }
        if(port<=0&&*end==':') {
            char* tail=nullptr;long parsed=std::strtol(end+1,&tail,10);
            if(tail==end+1||(*tail&&*tail!='/')||parsed<1||parsed>65535)return false;
            port=static_cast<int>(parsed);
        }
    }
    for(const char* p=host;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'||*p==':'||*p=='['||*p==']'))return false;
    return *host&&port>=0&&port<=65535;
}
bool signalingAddress(char* out,std::size_t capacity,const cJSON* connection,const char* fallback="") {
    const char* path=str(connection,"resourcePath");
    if(!std::strncmp(path,"wss://",6))return put(out,capacity,path);
    if(!std::strncmp(path,"https://",8)){return std::snprintf(out,capacity,"wss://%s",path+8)>0&&std::strlen(path)-2<capacity;}
    char host[256];int port=0;
    if(!connectionAddress(connection,host,sizeof(host),port,fallback))return false;
    if(!std::strncmp(path,"rtsps://",8)||!std::strncmp(path,"rtsp://",7)) {
        const int n=std::snprintf(out,capacity,"wss://%s/nvst/",host);
        return n>0&&static_cast<std::size_t>(n)<capacity;
    }
    if(*path&&(*path!='/'||path[1]=='/'))return false;
    int n=std::snprintf(out,capacity,"wss://%s:443%s",host,*path?path:"/nvst/");
    return n>0&&static_cast<std::size_t>(n)<capacity;
}

}
bool trustedCloudUrl(const char* url) noexcept {
    if(std::strncmp(url,"https://",8))return false;
    auto* start=url+8;auto* end=std::strchr(start,'/');if(!end)return false;
    for(auto* p=url;*p;++p)if(static_cast<unsigned char>(*p)<=32||*p=='\\'||*p=='@')return false;
    const auto n=static_cast<std::size_t>(end-start);
    for(auto* suffix:{".geforcenow.com",".nvidiagrid.net",".geforce.com"}) {
        auto len=std::strlen(suffix);if(n>len&&!std::strncmp(end-len,suffix,len))return true;
    }
    return false;
}
bool ownedLibraryStatus(const char* status) noexcept {
    return status&&(!std::strcmp(status,"MANUAL")||!std::strcmp(status,"PLATFORM_SYNC")||!std::strcmp(status,"IN_LIBRARY"));
}
bool artworkUrl(const char* url) noexcept {
    static constexpr char prefix[]="https://img.nvidiagrid.net/apps/";
    constexpr std::size_t prefixLength=sizeof(prefix)-1;
    if(!url||std::strncmp(url,prefix,prefixLength))return false;
    const std::size_t length=std::strlen(url);
    if(length<=prefixLength||length>=sizeof(Game::art)||std::strstr(url,".."))return false;
    for(const char* p=url+prefixLength;*p;++p)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='/'||*p=='_'||*p=='-'||*p=='.'))return false;
    return true;
}
bool parseCatalog(const Response& r,CloudView& out,char* cursor,std::size_t capacity,bool ownedOnly) noexcept {
    Json j(r);const auto* apps=obj(obj(j.p,"data"),"apps");const auto* items=obj(apps,"items");const auto* page=obj(apps,"pageInfo");
    if(r.status!=200||!cJSON_IsArray(items)||cJSON_GetArraySize(items)>60||!cJSON_IsBool(obj(page,"hasNextPage"))||cJSON_GetArraySize(obj(j.p,"errors")))return false;
    CloudView parsed;parsed.state=CloudState::catalog;parsed.hasNext=cJSON_IsTrue(obj(page,"hasNextPage"));
    if(parsed.hasNext&&(!*str(page,"endCursor")||!std::strcmp(cursor,str(page,"endCursor"))))return false;
    if(!put(cursor,capacity,str(page,"endCursor")))return false;
    const cJSON* app; cJSON_ArrayForEach(app,items) {
        char art[sizeof(Game::art)],hero[sizeof(Game::hero)];
        pickArtwork(art,sizeof(art),obj(app,"images"),{"GAME_BOX_ART","KEY_ART","HERO_IMAGE","TV_BANNER"});
        pickArtwork(hero,sizeof(hero),obj(app,"images"),{"HERO_IMAGE","TV_BANNER","KEY_ART"});
        const cJSON* variant;cJSON_ArrayForEach(variant,obj(app,"variants")) {
            if(parsed.count==60)break;
            const bool owned=ownedLibraryStatus(str(obj(obj(variant,"gfn"),"library"),"status"));
            if(ownedOnly&&!owned)continue;
            auto& game=parsed.games[parsed.count];
            if(!safeId(str(variant,"id"))||!copy(game.id,str(variant,"id"))||!copy(game.title,str(app,"title"))||!copy(game.store,str(variant,"appStore")))return false;
            if(!*game.title||!*game.store){game=Game{};continue;}
            copy(game.art,art);copy(game.hero,hero);game.owned=owned;
            ++parsed.count;
        }
    }
    copy(parsed.message,parsed.count?"Choose a game and store":ownedOnly?"No games in your library":"No games found in this catalog page");out=parsed;return true;
}
void Cloud::fail(const char* text) noexcept {view_.state=CloudState::failed;std::snprintf(view_.message,sizeof(view_.message),"%s",text);}
void Cloud::failLaunch(const char* text) noexcept {fail(text);view_.launchError=true;}
void Cloud::reset() noexcept {
    secureErase(&session_,sizeof(session_));
    session_={};
    const unsigned revision=view_.revision,libraryRevision=library_.revision;
    view_={};library_={};view_.revision=revision;library_.revision=libraryRevision;
    base_[0]=vpc_[0]=search_[0]=0;
    for(auto& cursor:browseCursors_)cursor[0]=0;
    for(auto& cursor:libraryCursors_)cursor[0]=0;
}
bool Cloud::connect(const char* jwt,const char* device,CloudView& target) noexcept {
    const auto failTarget=[&](const char* text){target.state=CloudState::failed;std::snprintf(target.message,sizeof(target.message),"%s",text);return false;};
    if(*base_)return true;
    auto r=request_(context_,"GET","https://pcs.geforcenow.com/v1/serviceUrls",nullptr,nullptr,nullptr);
    if(r.error)return failTarget(r.error);
    {
        Json j(r);
        const cJSON* entry;cJSON_ArrayForEach(entry,obj(obj(j.p,"gfnServiceInfo"),"gfnServiceEndpoints")) {
            if(!std::strcmp(str(entry,"idpId"),"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg")) {
                if(trustedCloudUrl(str(entry,"streamingServiceUrl")))copy(base_,str(entry,"streamingServiceUrl"));
                break;
            }
        }
    }
    if(!*base_)return failTarget("NVIDIA streaming endpoint unavailable");
    auto n=std::strlen(base_);if(base_[n-1]!='/'&&n+1<sizeof(base_)){base_[n]='/';base_[n+1]=0;}
    char url[768];std::snprintf(url,sizeof(url),"%sv2/serverInfo",base_);
    r=request_(context_,"GET",url,nullptr,jwt,device);
    if(r.error){base_[0]=0;return failTarget(r.error);}
    Json info(r);
    if(r.status!=200){base_[0]=0;return failTarget("Unable to read NVIDIA server information");}
    if(!copy(vpc_,str(obj(info.p,"requestStatus"),"serverId"))||!*vpc_)copy(vpc_,"GFN-PC");
    return true;
}
void Cloud::load(const char* jwt,const char* device,const char* search,bool next) noexcept {
    if(*session_.id){fail("Stop the active session before browsing");return;}
    if(next){if(view_.hasNext)loadPage(CatalogSource::browse,view_.page+1,jwt,device);return;}
    if(!copy(search_,search)){fail("Search text too long");return;}
    for(unsigned i=1;i<catalogMaxPages;++i)browseCursors_[i][0]=0;
    const Game current=view_.current;
    const unsigned revision=view_.revision;
    view_={};view_.current=current;view_.revision=revision;
    loadPage(CatalogSource::browse,0,jwt,device);
}
void Cloud::loadPage(CatalogSource source,unsigned page,const char* jwt,const char* device,int direction) noexcept {
    const bool library=source==CatalogSource::library;
    CloudView& target=library?library_:view_;
    for(unsigned step=0;fetchPage(source,page,jwt,device)&&library&&!target.count&&step<emptyPageSkips;++step) {
        if(direction<0&&page==0)direction=1;
        if(direction>0&&!target.hasNext)return;
        page=direction>0?page+1:page-1;
    }
}
bool Cloud::fetchPage(CatalogSource source,unsigned page,const char* jwt,const char* device) noexcept {
    const bool library=source==CatalogSource::library;
    CloudView& target=library?library_:view_;
    auto& cursors=library?libraryCursors_:browseCursors_;
    const auto failTarget=[&](const char* text){target.state=CloudState::failed;std::snprintf(target.message,sizeof(target.message),"%s",text);return false;};
    if(*session_.id){if(!library)fail("Stop the active session before browsing");return false;}
    if(page>=catalogMaxPages||(page>0&&!*cursors[page]))return failTarget("That catalog page is no longer available");
    target.state=CloudState::loading;
    if(!connect(jwt,device,target))return false;
    auto* root=cJSON_CreateObject();auto* vars=cJSON_AddObjectToObject(root,"variables");
    // NVIDIA rejects searchQuery when it is empty; browsing uses its own operation.
    cJSON_AddStringToObject(root,"query",library?libraryQuery:*search_?searchQuery:browseQuery);
    cJSON_AddStringToObject(vars,"vpcId",vpc_);cJSON_AddStringToObject(vars,"locale","en_US");
    cJSON_AddNumberToObject(vars,"fetchCount",catalogPageSize);cJSON_AddStringToObject(vars,"cursor",cursors[page]);
    if(!library&&*search_)cJSON_AddStringToObject(vars,"searchString",search_);
    auto* filters=cJSON_AddObjectToObject(vars,"filters");
    if(library)cJSON_AddStringToObject(cJSON_AddObjectToObject(cJSON_AddObjectToObject(cJSON_AddObjectToObject(cJSON_AddObjectToObject(filters,"variants"),"gfn"),"library"),"status"),"notEquals","NOT_OWNED");
    char* body=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!body)return failTarget("Out of memory");
    auto r=request_(context_,"POST","https://games.geforce.com/graphql",body,jwt,device);cJSON_free(body);
    if(r.error)return failTarget(r.error);
    char next[sizeof(cursors[0])];std::memcpy(next,cursors[page],sizeof(next));
    const unsigned revision=target.revision;
    const Game current=target.current;
    const int queuePosition=target.queuePosition,setupStep=target.setupStep;
    if(!parseCatalog(r,target,next,sizeof(next),library)) {
        char msg[128];std::snprintf(msg,sizeof(msg),"%s request failed (HTTP %ld)",library?"Library":"Catalog",r.status);
        return failTarget(msg);
    }
    target.revision=revision+1;target.page=page;target.current=current;
    target.queuePosition=queuePosition;target.setupStep=setupStep;
    for(unsigned i=page+1;i<catalogMaxPages;++i)cursors[i][0]=0;
    if(target.hasNext&&page+1<catalogMaxPages)std::memcpy(cursors[page+1],next,sizeof(next));
    else target.hasNext=false;
    return true;
}
void Cloud::select(int delta) noexcept {if(view_.state!=CloudState::catalog||!view_.count)return;view_.selected=(view_.selected+view_.count+delta)%view_.count;}
void Cloud::focus(unsigned index) noexcept {if(view_.state==CloudState::catalog&&index<view_.count)view_.selected=index;}
void Cloud::launch(const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings) noexcept {
    if(view_.state!=CloudState::catalog||view_.selected>=view_.count||*session_.id)return;
    start(view_.games[view_.selected],jwt,device,now,settings);
}
void Cloud::launchEntry(CatalogSource source,unsigned index,const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings) noexcept {
    const CloudView& entries=source==CatalogSource::library?library_:view_;
    if(entries.state!=CloudState::catalog||index>=entries.count)return;
    if(source==CatalogSource::browse)view_.selected=index;
    const Game game=entries.games[index];
    start(game,jwt,device,now,settings);
}
void Cloud::dismissLaunchError() noexcept {
    if(*session_.id||!view_.launchError)return;
    view_.launchError=false;view_.state=view_.count?CloudState::catalog:CloudState::idle;copy(view_.message,"Choose a game and store");
}
void Cloud::start(const Game& game,const char* jwt,const char* device,std::uint64_t now,const StreamSettings& requested) noexcept {
    if(*session_.id||view_.state==CloudState::starting||view_.state==CloudState::queued||view_.state==CloudState::ready)return;
    view_.current=game;view_.launchError=false;
    for(const char* digit=game.id;*digit;++digit)if(*digit<'0'||*digit>'9'){failLaunch("Catalog variant has no numeric launch ID");return;}
    const StreamSettings settings=requested;
    if(validateSettings(settings)!=SettingsError::none){failLaunch("Invalid stream settings");return;}
    session_.settings=settings;
    char netId[128]{},netUrl[768],netBody[256];
    std::snprintf(netUrl,sizeof(netUrl),"%sv2/nettestsession",base_);
    std::snprintf(netBody,sizeof(netBody),
        R"({"netTestRequestData":{"clientPlatformName":"windows","netTestProfile":{"widthInPixels":%d,"heightInPixels":%d,"framesPerSecond":%d}}})",
        settings.width,settings.height,settings.fps);
    const auto net=request_(context_,"POST",netUrl,
        netBody,jwt,device);
    // Upstream permits an unavailable network test; never reuse its response buffer.
    {Json result(net);if(!net.error&&net.status>=200&&net.status<300&&num(obj(result.p,"requestStatus"),"statusCode")==1){
        const char* id=str(obj(result.p,"netTestSession"),"sessionId");if(safeId(id))copy(netId,id);
    }}
    auto* root=cJSON_CreateObject();auto* req=cJSON_AddObjectToObject(root,"sessionRequestData");
    cJSON_AddNumberToObject(req,"userAge",25);
    cJSON_AddNumberToObject(req,"appId",std::strtod(game.id,nullptr));cJSON_AddStringToObject(req,"cmsId",game.id);
    for(auto* key:{"internalTitle","parentSessionId","clientDisplayHdrCapabilities"})cJSON_AddNullToObject(req,key);
    if(*netId)cJSON_AddStringToObject(req,"networkTestSessionId",netId);else cJSON_AddNullToObject(req,"networkTestSessionId");
    cJSON_AddStringToObject(req,"clientIdentification","GFN-PC");cJSON_AddStringToObject(req,"deviceHashId",device);cJSON_AddStringToObject(req,"clientVersion","30.0");cJSON_AddStringToObject(req,"clientPlatformName","windows");cJSON_AddStringToObject(req,"sdkVersion","1.0");cJSON_AddStringToObject(req,"partnerCustomData","");
    cJSON_AddItemToArray(cJSON_AddArrayToObject(req,"availableSupportedControllers"),cJSON_CreateNumber(2));
    for(auto* key:{"useOps","accountLinked"})cJSON_AddBoolToObject(req,key,true);
    for(auto* key:{"secureRTSPSupported","enablePersistingInGameSettings"})cJSON_AddBoolToObject(req,key,false);
    cJSON_AddNumberToObject(req,"streamerVersion",1);cJSON_AddNumberToObject(req,"audioMode",2);cJSON_AddNumberToObject(req,"sdrHdrMode",settings.hdr()?1:0);cJSON_AddNumberToObject(req,"surroundAudioInfo",0);cJSON_AddNumberToObject(req,"remoteControllersBitmap",1);cJSON_AddNumberToObject(req,"enhancedStreamMode",1);cJSON_AddNumberToObject(req,"appLaunchMode",2);cJSON_AddNumberToObject(req,"clientTimezoneOffset",0);
    auto* features=cJSON_AddObjectToObject(req,"requestedStreamingFeatures");
    for(auto* key:{"reflex","cloudGsync","enabledL4S","trueHdr","fallbackToLogicalResolution","vsync"})cJSON_AddBoolToObject(features,key,false);
    for(auto* key:{"mouseMovementFlags","supportedHidDevices","profile","chromaFormat","prefilterMode","prefilterSharpness","prefilterNoiseReduction","hudStreamingMode","hdrColorSpace"})cJSON_AddNumberToObject(features,key,0);
    cJSON_AddNumberToObject(features,"bitDepth",settings.tenBit()?1:0);cJSON_AddNullToObject(features,"hidDevices");cJSON_AddNumberToObject(features,"sdrColorSpace",2);cJSON_AddNumberToObject(features,"maxBitrateKbps",settings.bitrate_kbps);cJSON_AddNumberToObject(features,"codec",settings.codec()==VideoCodec::hevc?2:1);if(settings.network==NetworkPolicy::adaptive)cJSON_AddNumberToObject(features,"dynamicStreamingMode",3);cJSON_AddNumberToObject(features,"audioChannelCount",2);
    auto* meta=cJSON_AddArrayToObject(req,"metaData");
    const char* keys[]={"SubSessionId","wssignaling","GSStreamerType"};const char* values[]={device,"1","WebRTC"};
    for(int i=0;i<3;++i){auto* m=cJSON_CreateObject();cJSON_AddStringToObject(m,"key",keys[i]);cJSON_AddStringToObject(m,"value",values[i]);cJSON_AddItemToArray(meta,m);}
    auto* monitors=cJSON_AddArrayToObject(req,"clientRequestMonitorSettings");auto* monitor=cJSON_CreateObject();cJSON_AddItemToArray(monitors,monitor);
    for(auto* key:{"monitorId","positionX","positionY"})cJSON_AddNumberToObject(monitor,key,0);
    cJSON_AddNumberToObject(monitor,"widthInPixels",settings.width);cJSON_AddNumberToObject(monitor,"heightInPixels",settings.height);cJSON_AddNumberToObject(monitor,"framesPerSecond",settings.fps);cJSON_AddNumberToObject(monitor,"dpi",100);cJSON_AddNumberToObject(monitor,"sdrHdrMode",settings.hdr()?1:0);if(settings.hdr()){auto* display=cJSON_AddObjectToObject(monitor,"displayData");
     // Preferred content luminance defaults used by the native desktop client;
     // these are negotiation hints, not measured LG panel capabilities.
     cJSON_AddNumberToObject(display,"desiredContentMaxLuminance",1000);cJSON_AddNumberToObject(display,"desiredContentMinLuminance",0);cJSON_AddNumberToObject(display,"desiredContentMaxFrameAverageLuminance",400);
    }else cJSON_AddNullToObject(monitor,"displayData");cJSON_AddNullToObject(monitor,"hdr10PlusGamingData");
    char* body=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!body){failLaunch("Out of memory");return;}
    char url[768];std::snprintf(url,sizeof(url),"%sv2/session?keyboardLayout=en-US_qwerty&languageCode=en_US",base_);
    view_.state=CloudState::starting;
    auto r=request_(context_,"POST",url,body,jwt,device);cJSON_free(body);parseSession(r);nextPoll_=now+3;
}
bool Cloud::parseSession(const Response& r) noexcept {
    view_.queuePosition=view_.setupStep=-1;
    if(r.error){failLaunch(r.error);return false;}Json root(r);auto* sess=obj(root.p,"session");
    const auto* requestStatus=obj(root.p,"requestStatus");
    const bool patching=num(requestStatus,"statusCode")==41&&std::strstr(str(requestStatus,"statusDescription"),"APP_PATCHING_STATUS");
    if(!patching&&(r.status<200||r.status>=300||num(requestStatus,"statusCode",-1)!=1)){char text[192];const auto* status=requestStatus;
        // Only bounded error fields reach the UI. Never log the response/token/session ID.
        std::snprintf(text,sizeof(text),"HTTP %ld code %d / %.64s / unified %.24s / session %d",
            r.status,num(status,"statusCode",-1),str(status,"statusDescription"),
            str(status,"unifiedErrorCode"),num(sess,"errorCode",-1));
        if(cJSON_IsNumber(obj(status,"unifiedErrorCode")))std::snprintf(text,sizeof(text),"HTTP %ld code %d / %.64s / unified %d / session %d",r.status,num(status,"statusCode",-1),str(status,"statusDescription"),num(status,"unifiedErrorCode"),num(sess,"errorCode",-1));
        failLaunch(text);return false;}
    if(*str(sess,"sessionId")&&safeId(str(sess,"sessionId")))copy(session_.id,str(sess,"sessionId"));
    if(!*session_.id){failLaunch("Cloud session response missing ID");return false;}
    const auto* status=obj(sess,"status");int state=cJSON_IsNumber(status)?status->valueint:-1;
    if(cJSON_IsString(status)){auto* s=status->valuestring;if(!std::strcmp(s,"queued"))state=0;else if(!std::strcmp(s,"ready")||!std::strcmp(s,"active"))state=2;else if(!std::strcmp(s,"streaming")||!std::strcmp(s,"playing"))state=3;else if(!std::strcmp(s,"provisioning")||!std::strcmp(s,"initializing")||!std::strcmp(s,"setup")||!std::strcmp(s,"launching"))state=1;else if(!std::strcmp(s,"resuming"))state=6;else if(!std::strcmp(s,"finished"))state=7;}
    if(state==4||state==5){failLaunch("Cloud session paused. Stop it before launching again");return false;}
    if(state==7){failLaunch("Cloud session ended");return false;}
    session_.signaling[0]=session_.mediaIp[0]=0;session_.mediaPort=0;session_.signalingSource=SignalingSource::none;
    if(*str(sess,"signalingUrl")&&copy(session_.signaling,str(sess,"signalingUrl")))session_.signalingSource=SignalingSource::explicitUrl;
    if(*str(sess,"serverIp"))copy(session_.mediaIp,str(sess,"serverIp"));
    const auto* control=obj(sess,"sessionControlInfo");
    if(!*session_.mediaIp) {char ip[sizeof(session_.mediaIp)];int port=0;if(connectionAddress(control,ip,sizeof(ip),port))copy(session_.mediaIp,ip);}
    const cJSON* conn;
    for(int usage:{14,16}) {
        cJSON_ArrayForEach(conn,obj(sess,"connectionInfo")) {
            if(num(conn,"usage")==usage&&!*session_.signaling&&signalingAddress(session_.signaling,sizeof(session_.signaling),conn,session_.mediaIp))
                session_.signalingSource=usage==14?SignalingSource::streamConnection:SignalingSource::alternateConnection;
        }
    }
    if(!*session_.signaling&&control&&signalingAddress(session_.signaling,sizeof(session_.signaling),control,session_.mediaIp))session_.signalingSource=SignalingSource::sessionControl;
    for(int usage:{2,17,14}) {
        char bestIp[sizeof(session_.mediaIp)]{};int bestPort=0;
        cJSON_ArrayForEach(conn,obj(sess,"connectionInfo")) {
            if(num(conn,"usage")!=usage)continue;
            char ip[sizeof(session_.mediaIp)];int port=0;
            if(connectionAddress(conn,ip,sizeof(ip),port,usage==14?session_.mediaIp:"")&&port>0&&(usage!=14||port>bestPort)) {
                copy(bestIp,ip);bestPort=port;
            }
        }
        if(bestPort){copy(session_.mediaIp,bestIp);session_.mediaPort=bestPort;break;}
    }
    if(patching||state==6){view_.state=CloudState::queued;copy(view_.message,patching?"Server patching game. Waiting for launch...":"Cloud session resuming. Waiting for server...");}
    else if((state==2||state==3)&&*session_.signaling){view_.state=CloudState::ready;copy(view_.message,"Cloud session ready. Connecting stream...");}
    else if(state<0||state>3){failLaunch("Unknown cloud session state");return false;}
    else {
        view_.state=CloudState::queued;
        if(state==2||state==3)copy(view_.message,"Server ready, but no supported streaming address received");
        else if(state==1){view_.setupStep=num(obj(sess,"seatSetupInfo"),"seatSetupStep",-1);std::snprintf(view_.message,sizeof(view_.message),"Server preparing game - setup step %d",view_.setupStep);}
        else {int queue=num(obj(sess,"seatSetupInfo"),"queuePosition",num(sess,"queuePosition",-1));
            view_.queuePosition=queue<0?-1:queue;
            if(queue<0)copy(view_.message,"Waiting for server allocation (queue position unavailable)");
            else std::snprintf(view_.message,sizeof(view_.message),"Waiting for server allocation - queue position %d",queue);
        }
    }
    return true;
}
void Cloud::tick(const char* jwt,const char* device,std::uint64_t now) noexcept {
    if(view_.state!=CloudState::queued||now<nextPoll_)return;
    nextPoll_=now+3;char url[768];std::snprintf(url,sizeof(url),"%sv2/session/%s",base_,session_.id);
    parseSession(request_(context_,"GET",url,nullptr,jwt,device));
}
bool Cloud::stop(const char* jwt,const char* device) noexcept {
    if(*session_.id){char url[768];std::snprintf(url,sizeof(url),"%sv2/session/%s",base_,session_.id);auto r=request_(context_,"DELETE",url,nullptr,jwt,device);if(r.error||((r.status!=404&&r.status!=410)&&(r.status<200||r.status>=300))){fail("Unable to stop cloud session");return false;}}
    secureErase(&session_,sizeof(session_));session_={};view_.queuePosition=view_.setupStep=-1;view_.launchError=false;view_.state=view_.count?CloudState::catalog:CloudState::idle;copy(view_.message,"Choose a game and store");return true;
}
}
