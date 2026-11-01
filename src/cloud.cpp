// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol adapted from pinned OpenNOW-Switch gfn/catalog and cloud_session.
#include "cloud.hpp"
#include "cloud_endpoint.hpp"
#include "random.hpp"
#include "vendor/cJSON.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <climits>
#include <cmath>
#include <ctime>
#include <string>
#include <utility>
namespace opennow {
namespace {
const char* str(const cJSON* j,const char* k) {auto* v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:"";}
const cJSON* obj(const cJSON* j,const char* k) {return cJSON_GetObjectItemCaseSensitive(j,k);}
int num(const cJSON* j,const char* k,int fallback=0) {auto* v=obj(j,k);return cJSON_IsNumber(v)?v->valueint:fallback;}
bool integer(const cJSON* v,int& out) {
    if(cJSON_IsNumber(v)) {
        if(!std::isfinite(v->valuedouble)||v->valuedouble<INT_MIN||v->valuedouble>INT_MAX)return false;
        const int value=static_cast<int>(v->valuedouble);if(v->valuedouble!=value)return false;
        out=value;return true;
    }
    if(!cJSON_IsString(v)||!*v->valuestring)return false;
    const char* p=v->valuestring;const bool negative=*p=='-';if(*p=='-'||*p=='+')++p;
    if(!*p)return false;
    const unsigned limit=static_cast<unsigned>(INT_MAX)+(negative?1u:0u);unsigned value=0;
    for(;*p;++p){if(*p<'0'||*p>'9'||value>(limit-static_cast<unsigned>(*p-'0'))/10)return false;value=value*10+static_cast<unsigned>(*p-'0');}
    out=negative?(value==static_cast<unsigned>(INT_MAX)+1?INT_MIN:-static_cast<int>(value)):static_cast<int>(value);return true;
}
int number(const cJSON* j,const char* key,int fallback=-1) {int value;return integer(obj(j,key),value)?value:fallback;}
bool appId(const char* text,int& out) {
    if(!text||!*text)return false;
    int value=0;
    for(const char* p=text;*p;++p){if(*p<'0'||*p>'9'||value>(INT_MAX-(*p-'0'))/10)return false;value=value*10+(*p-'0');}
    out=value;return value>0;
}
int sessionStatus(const cJSON* session) {
    const auto* status=obj(session,"status");int value;
    if(integer(status,value))return value;
    const char* text=str(session,"status");
    if(!std::strcmp(text,"queued"))return 0;
    if(!std::strcmp(text,"ready")||!std::strcmp(text,"active"))return 2;
    if(!std::strcmp(text,"streaming")||!std::strcmp(text,"playing"))return 3;
    if(!std::strcmp(text,"provisioning")||!std::strcmp(text,"initializing")||!std::strcmp(text,"setup")||!std::strcmp(text,"launching"))return 1;
    if(!std::strcmp(text,"resuming"))return 6;
    if(!std::strcmp(text,"finished"))return 7;
    return -1;
}
bool featureEnabled(const cJSON* feature) {
    if(std::strcmp(str(feature,"key"),"IN_GAME_SETTINGS_PERSISTENCE_ENABLED"))return false;
    if(cJSON_IsTrue(obj(feature,"value")))return true;
    const char* value=str(feature,"value");return (value[0]=='t'||value[0]=='T')&&(value[1]=='r'||value[1]=='R')&&(value[2]=='u'||value[2]=='U')&&(value[3]=='e'||value[3]=='E')&&!value[4];
}
bool hasPersistence(const cJSON* features) {
    if(cJSON_IsObject(features))return featureEnabled(features);
    if(!cJSON_IsArray(features))return false;
    const cJSON* feature;cJSON_ArrayForEach(feature,features)if(featureEnabled(feature))return true;
    return false;
}
bool newSubSession(char (&out)[37]) {
    unsigned char bytes[16];if(!randomBytes(bytes,sizeof(bytes)))return false;
    bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
    std::snprintf(out,sizeof(out),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
    secureErase(bytes,sizeof(bytes));return true;
}
std::uint64_t monotonicMs() {timespec time{};if(clock_gettime(CLOCK_MONOTONIC,&time))return 0;return static_cast<std::uint64_t>(time.tv_sec)*1000+time.tv_nsec/1000000;}
bool containsInsensitive(const char* text,const char* needle) {
    for(;*text;++text){const char* a=text;const char* b=needle;while(*a&&*b&&((*a>='a'&&*a<='z'?*a-32:*a)==*b)){++a;++b;}if(!*b)return true;}
    return false;
}
bool sessionConflict(const cJSON* root) {
    const auto* status=obj(root,"requestStatus");
    if(number(status,"statusCode")==11||containsInsensitive(str(status,"statusDescription"),"SESSION_LIMIT"))return true;
    for(const auto* code:{obj(status,"unifiedErrorCode"),obj(obj(root,"session"),"errorCode")}) {
        int value;if(integer(code,value)&&value==0x4af1201e)return true;
        if(cJSON_IsString(code)){const char* text=code->valuestring;if(!std::strncmp(text,"0x",2))text+=2;if(std::strlen(text)==8&&containsInsensitive(text,"4AF1201E"))return true;}
    }
    return false;
}
bool controlOrigin(const cJSON* session,char* out,std::size_t capacity) {
    const auto* control=obj(session,"sessionControlInfo");
    if(obj(control,"port")&&number(control,"port")!=443)return false;
    const auto* ip=obj(control,"ip");if(cJSON_IsArray(ip))ip=cJSON_GetArrayItem(ip,0);
    if(!cJSON_IsString(ip)||!*ip->valuestring)return false;
    const char* host=ip->valuestring;char raw[768];
    const int length=std::snprintf(raw,sizeof(raw),std::strstr(host,"://")?"%s":std::strchr(host,':')&&*host!='['?"https://[%s]/":"https://%s/",host);
    if(length<0||static_cast<std::size_t>(length)>=sizeof(raw)||!publicCloudControlUrl(raw)||!cloudControlOrigin(raw,out,capacity))return false;
    const auto size=std::strlen(out);if(!size)return false;
    if(out[size-1]!='/'){if(size+1>=capacity)return false;out[size]='/';out[size+1]=0;}
    return true;
}
const char* acceptedProfile(const cJSON* session,const Session& baseline,bool allowMissing,unsigned capacity,const StreamSettings* envelope,Session& parsed) {
    const auto* request=obj(session,"sessionRequestData");
    const auto* monitor=cJSON_GetArrayItem(obj(request,"clientRequestMonitorSettings"),0);
    const auto* accepted=cJSON_GetArrayItem(obj(session,"monitorSettings"),0);if(!accepted)accepted=monitor;
    const auto* requested=obj(request,"requestedStreamingFeatures");
    const auto* finalized=obj(session,"finalizedStreamingFeatures");
    const auto* normalized=obj(session,"negotiatedStreamProfile");
    for(const auto* value:{finalized,normalized})if(value&&!cJSON_IsNull(value)&&!cJSON_IsObject(value))return "Cloud service returned an invalid accepted profile";
    const auto feature=[&](const char* key){const auto* value=obj(finalized,key);return value?value:obj(requested,key);};
    StreamSettings profile=baseline.settings;
    for(const auto& field:{std::pair{"widthInPixels",&profile.width},std::pair{"heightInPixels",&profile.height},std::pair{"framesPerSecond",&profile.fps}})
        if(obj(accepted,field.first)||!allowMissing)*field.second=number(accepted,field.first);
    int resolutionWidth=-1,resolutionHeight=-1;
    if(const auto* resolution=obj(normalized,"resolution")) {
        if(!cJSON_IsString(resolution))return "Cloud service returned an invalid accepted resolution";
        const char* separator=std::strchr(resolution->valuestring,'x');char width[16];
        if(!separator||separator==resolution->valuestring||static_cast<std::size_t>(separator-resolution->valuestring)>=sizeof(width))return "Cloud service returned an invalid accepted resolution";
        const auto size=static_cast<std::size_t>(separator-resolution->valuestring);std::memcpy(width,resolution->valuestring,size);width[size]=0;
        if(!appId(width,resolutionWidth)||!appId(separator+1,resolutionHeight))return "Cloud service returned an invalid accepted resolution";
        profile.width=resolutionWidth;profile.height=resolutionHeight;
    }
    for(const auto& field:{std::pair{"width",&profile.width},std::pair{"height",&profile.height},std::pair{"fps",&profile.fps}})
        if(const auto* value=obj(normalized,field.first)){if(!integer(value,*field.second))return "Cloud service returned invalid accepted dimensions or frame rate";}
    if(resolutionWidth>0&&(profile.width!=resolutionWidth||profile.height!=resolutionHeight))return "Cloud service returned conflicting accepted dimensions";
    int codec=allowMissing?(baseline.settings.codec()==VideoCodec::hevc?2:1):-1;
    if(const auto* value=feature("codec")){if(!integer(value,codec)||(codec!=1&&codec!=2))return "Cloud service accepted an unsupported codec";}
    if(const auto* value=obj(normalized,"codec")) {
        if(!cJSON_IsString(value))return "Cloud service returned an invalid accepted codec";
        const char* name=value->valuestring;const auto equal=[&](const char* expected){return std::strlen(name)==std::strlen(expected)&&containsInsensitive(name,expected);};
        if(equal("H264")||equal("AVC"))codec=1;else if(equal("H265")||equal("HEVC"))codec=2;else return "Cloud service accepted an unsupported codec";
    }
    int depth=allowMissing?(baseline.settings.tenBit()?10:8):-1,chroma=allowMissing?0:-1;
    if(const auto* value=feature("bitDepth")){int wire;if(!integer(value,wire)||(wire!=0&&wire!=1&&wire!=8&&wire!=10))return "Cloud service returned an invalid accepted bit depth";depth=wire==0?8:wire==1?10:wire;}
    if(const auto* value=feature("chromaFormat")){if(!integer(value,chroma)||chroma!=0)return "Cloud service accepted an unsupported chroma format";}
    if(const auto* value=obj(normalized,"bitDepth")){if(!integer(value,depth)||(depth!=8&&depth!=10))return "Cloud service returned an invalid accepted bit depth";}
    if(const auto* value=obj(normalized,"chromaFormat")){if(!integer(value,chroma)||chroma!=0)return "Cloud service accepted an unsupported chroma format";}
    if(const auto* value=obj(normalized,"colorQuality")) {
        if(!cJSON_IsString(value))return "Cloud service returned an invalid accepted color profile";
        const int color=!std::strcmp(value->valuestring,"8bit_420")?8:!std::strcmp(value->valuestring,"10bit_420")?10:-1;
        if(color<0||(obj(normalized,"bitDepth")&&depth!=color)||(obj(normalized,"chromaFormat")&&chroma!=0))return "Cloud service accepted an unsupported or conflicting color profile";
        depth=color;chroma=0;
    }
    int hdr=allowMissing&&baseline.settings.hdr()?1:0;bool hdrReported=false;
    for(const auto* source:{request,monitor,finalized,session})if(const auto* value=obj(source,"sdrHdrMode")){if(!integer(value,hdr)||(hdr!=0&&hdr!=1))return "Cloud service returned an invalid accepted HDR mode";hdrReported=true;}
    for(const auto* source:{finalized,normalized})if(const auto* value=obj(source,"enableHdr")){if(!cJSON_IsBool(value))return "Cloud service returned an invalid accepted HDR mode";hdr=cJSON_IsTrue(value)?1:0;hdrReported=true;}
    if(!allowMissing&&!hdrReported)return "Existing session is missing its accepted HDR mode";
    if((codec!=1&&codec!=2)||chroma!=0||(codec==1&&(depth!=8||hdr))||(codec==2&&depth!=10))return "Cloud service accepted an unsupported video profile";
    profile.mode=codec==2?(hdr?VideoMode::hevcMain10HdrHardware:VideoMode::hevcMain10SdrHardware):(baseline.settings.hardware()?VideoMode::h264Hardware:VideoMode::h264Software);
    int value;
    if(feature("maxBitrateKbps")){if(!integer(feature("maxBitrateKbps"),value))return "Cloud service returned an invalid accepted bitrate";profile.bitrate_kbps=value;}
    const auto* policy=obj(normalized,"dynamicStreamingMode");if(!policy)policy=feature("dynamicStreamingMode");
    if(policy){if(!integer(policy,value)||value<0||value>3)return "Cloud service returned an unsupported quality policy";profile.network=value==3?NetworkPolicy::adaptive:NetworkPolicy::fixed;}
    for(const auto* l4s:{feature("enabledL4S"),obj(normalized,"enableL4S")})if(l4s&&(!cJSON_IsBool(l4s)||cJSON_IsTrue(l4s)))return "Existing session requires unsupported L4S transport";
    unsigned channels=allowMissing?baseline.audioChannels:2;
    if(feature("audioChannelCount")){if(!integer(feature("audioChannelCount"),value)||(value!=2&&value!=6&&value!=8))return "Cloud service returned an unsupported audio profile";channels=static_cast<unsigned>(value);}
    else {const cJSON* entry;cJSON_ArrayForEach(entry,obj(request,"metaData"))if(!std::strcmp(str(entry,"key"),"surroundAudioInfo")){if(!integer(obj(entry,"value"),value)||(value!=2&&value!=6&&value!=8))return "Cloud service returned an unsupported audio profile";channels=static_cast<unsigned>(value);}}
    if(channels>capacity)return "Existing session audio exceeds the current output. The session is retained";
    if(channels!=baseline.audioChannels)profile.audio_mode=channels==8?audio::Mode::surround71:channels==6?audio::Mode::surround51:audio::Mode::stereo;
    if(validateSettings(profile)!=SettingsError::none)return "Cloud service accepted unsupported stream settings. The session is retained";
    if(envelope&&(profile.width>envelope->width||profile.height>envelope->height))return "Accepted stream dimensions exceed the allocated envelope";
    parsed.settings=profile;parsed.audioChannels=channels;return nullptr;
}
bool put(char* out,std::size_t cap,const char* in) {if(!in||std::strlen(in)>=cap)return false;std::memcpy(out,in,std::strlen(in)+1);return true;}
template<std::size_t N> bool copy(char (&out)[N],const char* in){return put(out,N,in);}
void wipeJson(cJSON* v) {for(;v;v=v->next){if(v->valuestring)secureErase(v->valuestring,std::strlen(v->valuestring));wipeJson(v->child);}}
struct Json {cJSON* p;explicit Json(const Response& r):p(r.body?cJSON_ParseWithLengthOpts(r.body,r.length+1,nullptr,true):nullptr){}~Json(){wipeJson(p);cJSON_Delete(p);}};
#define OPENNOW_APP_FIELDS "pageInfo{hasNextPage endCursor}items{id title images{GAME_BOX_ART KEY_ART HERO_IMAGE TV_BANNER} variants{id appStore gfn{status features{__typename ... on GfnSubscriptionFeatureValue{key value} ... on GfnSubscriptionFeatureValueList{key values}} library{status}}}}"
const char* searchQuery=R"(query GetSearchFilterResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$searchString:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"itemMetadata.relevance:DESC,sortName:ASC",first:$fetchCount,after:$cursor,searchQuery:$searchString,filters:$filters){)" OPENNOW_APP_FIELDS "}}";
const char* browseQuery=R"(query GetFilterBrowseResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"itemMetadata.relevance:DESC,sortName:ASC",first:$fetchCount,after:$cursor,filters:$filters){)" OPENNOW_APP_FIELDS "}}";
const char* libraryQuery=R"(query GetFilterBrowseResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"variants.gfn.library.lastPlayedDate:DESC,computedValues.libraryAddedDate:DESC,sortName:ASC",first:$fetchCount,after:$cursor,filters:$filters){)" OPENNOW_APP_FIELDS "}}";
const char* launchQuery=R"(query OpenNowGame($vpcId:String!,$locale:String!,$ids:[Int]!){apps(vpcId:$vpcId,language:$locale,variantIds:$ids){items{id title images{GAME_BOX_ART KEY_ART HERO_IMAGE TV_BANNER} gfn{playabilityState} variants{id appStore gfn{status library{status playStatus}}}}}})";
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
            copy(game.art,art);copy(game.hero,hero);game.owned=owned;game.supportsPersistence=hasPersistence(obj(obj(variant,"gfn"),"features"));
            ++parsed.count;
        }
    }
    copy(parsed.message,parsed.count?"Choose a game and store":ownedOnly?"No games in your library":"No games found in this catalog page");out=parsed;return true;
}
void Cloud::fail(const char* text) noexcept {view_.state=CloudState::failed;std::snprintf(view_.message,sizeof(view_.message),"%s",text);}
void Cloud::failLaunch(const char* text) noexcept {fail(text);view_.launchError=true;}
void Cloud::clearSession() noexcept {
    secureErase(&session_,sizeof(session_));session_={};allocationSettings_={};controlBase_[0]=0;
    nextPoll_=resumeDeadline_=0;pollFailures_=resumePolls_=0;audioCapacity_=2;resumePending_=false;
    view_.queuePosition=view_.setupStep=-1;
}
void Cloud::reset() noexcept {
    clearSession();remote_={};remoteCount_=0;conflictUntil_=currentNow_=0;recoveryBlocked_=false;
    secureErase(remoteIds_,sizeof(remoteIds_));
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
void Cloud::launch(const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings,unsigned audioChannels) noexcept {
    if(view_.state!=CloudState::catalog||view_.selected>=view_.count||*session_.id)return;
    start(view_.games[view_.selected],jwt,device,now,settings,audioChannels);
}
void Cloud::launchEntry(CatalogSource source,unsigned index,const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings,unsigned audioChannels) noexcept {
    const CloudView& entries=source==CatalogSource::library?library_:view_;
    if(entries.state!=CloudState::catalog||index>=entries.count)return;
    if(source==CatalogSource::browse)view_.selected=index;
    const Game game=entries.games[index];
    start(game,jwt,device,now,settings,audioChannels);
}
void Cloud::launchId(const char* appId,const char* store,const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings,unsigned audioChannels) noexcept {
    if(*session_.id||view_.state==CloudState::starting||view_.state==CloudState::queued||view_.state==CloudState::ready)return;
    view_.current={};view_.launchError=false;
    unsigned numericId=0;
    if(!appId||!*appId||std::strlen(appId)>=sizeof(Game::id)||!store||std::strlen(store)>=sizeof(Game::store)){failLaunch("Invalid forwarded game identity");return;}
    for(const char* p=appId;*p;++p) {
        if(*p<'0'||*p>'9'||numericId>(2147483647U-static_cast<unsigned>(*p-'0'))/10){failLaunch("Forwarded game ID is not a positive GraphQL Int");return;}
        numericId=numericId*10+static_cast<unsigned>(*p-'0');
    }
    if(!numericId){failLaunch("Forwarded game ID is not a positive GraphQL Int");return;}
    if(!connect(jwt,device,view_)){view_.launchError=true;return;}
    auto* root=cJSON_CreateObject();auto* vars=cJSON_AddObjectToObject(root,"variables");
    cJSON_AddStringToObject(root,"query",launchQuery);
    cJSON_AddStringToObject(vars,"vpcId",vpc_);cJSON_AddStringToObject(vars,"locale","en_US");
    cJSON_AddItemToArray(cJSON_AddArrayToObject(vars,"ids"),cJSON_CreateNumber(numericId));
    char* body=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!body){failLaunch("Out of memory");return;}
    const auto r=request_(context_,"POST","https://games.geforce.com/graphql",body,jwt,device);cJSON_free(body);
    if(r.error){failLaunch(r.error);return;}
    Game game;
    {
        Json result(r);const auto* items=obj(obj(obj(result.p,"data"),"apps"),"items");
        if(r.status!=200||cJSON_GetArraySize(obj(result.p,"errors"))||!cJSON_IsArray(items)||cJSON_GetArraySize(items)>60){failLaunch("Unable to look up forwarded game");return;}
        const cJSON* app;cJSON_ArrayForEach(app,items) {
            const auto* variants=obj(app,"variants");
            if(!cJSON_IsArray(variants)||cJSON_GetArraySize(variants)>128){failLaunch("Invalid forwarded game catalog response");return;}
            const cJSON* variant;cJSON_ArrayForEach(variant,variants) {
                if(std::strcmp(str(variant,"id"),appId))continue;
                if(*game.id){failLaunch("Forwarded game identity is ambiguous");return;}
                if(!copy(game.id,str(variant,"id"))||!copy(game.title,str(app,"title"))||!*game.title||
                   !copy(game.store,str(variant,"appStore"))||!*game.store){failLaunch("Invalid forwarded game catalog response");return;}
                view_.current=game;
                if(*store&&std::strcmp(game.store,store)){failLaunch("Forwarded game store no longer matches the catalog");return;}
                const auto* gfn=obj(variant,"gfn");const auto* library=obj(gfn,"library");
                game.owned=ownedLibraryStatus(str(library,"status"));
                if(!game.owned){failLaunch("This game is not in your NVIDIA library for this store");return;}
                if(std::strcmp(str(obj(app,"gfn"),"playabilityState"),"PLAYABLE")||std::strcmp(str(gfn,"status"),"AVAILABLE")||!std::strcmp(str(library,"playStatus"),"NOT_PLAYABLE")){
                    failLaunch("This store version is not currently playable on GeForce NOW");return;
                }
                pickArtwork(game.art,sizeof(game.art),obj(app,"images"),{"GAME_BOX_ART","KEY_ART","HERO_IMAGE","TV_BANNER"});
                pickArtwork(game.hero,sizeof(game.hero),obj(app,"images"),{"HERO_IMAGE","TV_BANNER","KEY_ART"});
            }
        }
    }
    if(!*game.id){failLaunch("Forwarded game was not found in the NVIDIA catalog");return;}
    start(game,jwt,device,now,settings,audioChannels);
}
void Cloud::dismissLaunchError() noexcept {
    if(*session_.id||!view_.launchError)return;
    view_.launchError=false;view_.state=view_.count?CloudState::catalog:CloudState::idle;copy(view_.message,"Choose a game and store");
}
void Cloud::start(const Game& game,const char* jwt,const char* device,std::uint64_t now,const StreamSettings& requested,unsigned audioChannels) noexcept {
    if(*session_.id||view_.state==CloudState::starting||view_.state==CloudState::queued||view_.state==CloudState::ready)return;
    if(recoveryBlocked_){failLaunch("Check the existing cloud session before launching again");return;}
    view_.current=game;view_.launchError=false;
    int launchId;if(!appId(game.id,launchId)){failLaunch("Catalog variant has no positive bounded launch ID");return;}
    const StreamSettings settings=requested;
    if(validateSettings(settings)!=SettingsError::none){failLaunch("Invalid stream settings");return;}
    if((audioChannels!=2&&audioChannels!=6&&audioChannels!=8)||audio::requestedChannels(settings.audio_mode,audioChannels)!=audioChannels){failLaunch("Invalid audio channel request");return;}
    session_.settings=settings;
    allocationSettings_=settings;
    session_.audioChannels=audioChannels;
    audioCapacity_=audioChannels;
    char subSession[37];if(!newSubSession(subSession)){failLaunch("Could not create cloud session identity");return;}
    currentNow_=now;view_.sessionConflict=false;remote_={};remoteCount_=0;conflictUntil_=0;
    copy(controlBase_,base_);
    char netId[128]{},netUrl[768],netBody[256];
    std::snprintf(netUrl,sizeof(netUrl),"%sv2/nettestsession",base_);
    std::snprintf(netBody,sizeof(netBody),
        R"({"netTestRequestData":{"clientPlatformName":"GFN-PC","netTestProfile":{"widthInPixels":%d,"heightInPixels":%d,"framesPerSecond":%d}}})",
        settings.width,settings.height,settings.fps);
    const auto net=request_(context_,"POST",netUrl,
        netBody,jwt,device);
    // Upstream permits an unavailable network test; never reuse its response buffer.
    {Json result(net);if(!net.error&&net.status>=200&&net.status<300&&num(obj(result.p,"requestStatus"),"statusCode")==1){
        const char* id=str(obj(result.p,"netTestSession"),"sessionId");if(safeId(id))copy(netId,id);
    }}
    auto* root=cJSON_CreateObject();auto* req=cJSON_AddObjectToObject(root,"sessionRequestData");
    cJSON_AddNumberToObject(req,"userAge",25);
    cJSON_AddNumberToObject(req,"appId",launchId);cJSON_AddStringToObject(req,"internalTitle",game.title);
    for(auto* key:{"parentSessionId","clientDisplayHdrCapabilities","partnerCustomData"})cJSON_AddNullToObject(req,key);
    if(*netId)cJSON_AddStringToObject(req,"networkTestSessionId",netId);else cJSON_AddNullToObject(req,"networkTestSessionId");
    cJSON_AddStringToObject(req,"clientIdentification","GFN-PC");cJSON_AddStringToObject(req,"deviceHashId",device);cJSON_AddStringToObject(req,"clientVersion","30.0");cJSON_AddStringToObject(req,"clientPlatformName","Windows");cJSON_AddStringToObject(req,"sdkVersion","1.0");
    cJSON_AddItemToArray(cJSON_AddArrayToObject(req,"availableSupportedControllers"),cJSON_CreateNumber(2));
    cJSON_AddNumberToObject(req,"preferredController",2);cJSON_AddBoolToObject(req,"useOps",true);
    cJSON_AddBoolToObject(req,"accountLinked",game.owned);cJSON_AddBoolToObject(req,"secureRTSPSupported",false);
    cJSON_AddBoolToObject(req,"enablePersistingInGameSettings",settings.persist_game_settings&&game.supportsPersistence);
    cJSON_AddNumberToObject(req,"streamerVersion",1);cJSON_AddNumberToObject(req,"audioMode",2);cJSON_AddNumberToObject(req,"sdrHdrMode",settings.hdr()?1:0);cJSON_AddNumberToObject(req,"surroundAudioInfo",0);cJSON_AddNumberToObject(req,"remoteControllersBitmap",1);cJSON_AddNumberToObject(req,"enhancedStreamMode",1);cJSON_AddNumberToObject(req,"appLaunchMode",2);cJSON_AddNumberToObject(req,"clientTimezoneOffset",0);
    auto* features=cJSON_AddObjectToObject(req,"requestedStreamingFeatures");
    for(auto* key:{"reflex","cloudGsync","enabledL4S","trueHdr","fallbackToLogicalResolution","vsync"})cJSON_AddBoolToObject(features,key,false);
    for(auto* key:{"mouseMovementFlags","supportedHidDevices","profile","chromaFormat","prefilterMode","prefilterSharpness","prefilterNoiseReduction","hudStreamingMode","hdrColorSpace"})cJSON_AddNumberToObject(features,key,0);
    cJSON_AddNumberToObject(features,"bitDepth",settings.tenBit()?1:0);cJSON_AddNullToObject(features,"hidDevices");cJSON_AddNumberToObject(features,"sdrColorSpace",2);cJSON_AddNumberToObject(features,"maxBitrateKbps",settings.bitrate_kbps);cJSON_AddNumberToObject(features,"codec",settings.codec()==VideoCodec::hevc?2:1);if(settings.network==NetworkPolicy::adaptive)cJSON_AddNumberToObject(features,"dynamicStreamingMode",3);cJSON_AddNumberToObject(features,"audioChannelCount",audioChannels);
    auto* dlss=cJSON_AddObjectToObject(features,"dlssOverrides");for(const char* key:{"srMode","srModel","rrModel","fgMode","fgModel"})cJSON_AddNumberToObject(dlss,key,0);cJSON_AddNumberToObject(dlss,"srScale",100);
    const bool surround=audioChannels>2;
    if(surround)cJSON_AddNumberToObject(req,"requestedAudioFormat",audioChannels==8?3:2);
    auto* meta=cJSON_AddArrayToObject(req,"metaData");
    const auto audioCount=std::to_string(audioChannels);
    const char* keys[]={"SubSessionId","wssignaling","GSStreamerType","surroundAudioInfo"};const char* values[]={subSession,"1","WebRTC",audioCount.c_str()};
    for(int i=0;i<4;++i){auto* m=cJSON_CreateObject();cJSON_AddStringToObject(m,"key",keys[i]);cJSON_AddStringToObject(m,"value",values[i]);cJSON_AddItemToArray(meta,m);}
    auto* monitors=cJSON_AddArrayToObject(req,"clientRequestMonitorSettings");auto* monitor=cJSON_CreateObject();cJSON_AddItemToArray(monitors,monitor);
    for(auto* key:{"monitorId","positionX","positionY"})cJSON_AddNumberToObject(monitor,key,0);
    cJSON_AddNumberToObject(monitor,"widthInPixels",settings.width);cJSON_AddNumberToObject(monitor,"heightInPixels",settings.height);cJSON_AddNumberToObject(monitor,"framesPerSecond",settings.fps);cJSON_AddNumberToObject(monitor,"dpi",96);cJSON_AddNumberToObject(monitor,"sdrHdrMode",settings.hdr()?1:0);if(settings.hdr()){auto* display=cJSON_AddObjectToObject(monitor,"displayData");
     // Preferred content luminance defaults used by the native desktop client;
     // these are negotiation hints, not measured LG panel capabilities.
     cJSON_AddNumberToObject(display,"desiredContentMaxLuminance",1000);cJSON_AddNumberToObject(display,"desiredContentMinLuminance",0);cJSON_AddNumberToObject(display,"desiredContentMaxFrameAverageLuminance",400);
    }else cJSON_AddNullToObject(monitor,"displayData");cJSON_AddNullToObject(monitor,"hdr10PlusGamingData");
    char* body=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!body){failLaunch("Out of memory");return;}
    char url[768];std::snprintf(url,sizeof(url),"%sv2/session?keyboardLayout=en-US_qwerty&languageCode=en_US",base_);
    view_.state=CloudState::starting;
    auto r=request_(context_,"POST",url,body,jwt,device);cJSON_free(body);parseSession(r);nextPoll_=now+3;
    if(!*session_.id&&(r.error||(r.status>=200&&r.status<300)||r.status==408||r.status==425||r.status==429||r.status>=500))recoveryBlocked_=true;
}
bool Cloud::controlEndpoint(const cJSON* session,char* out,std::size_t capacity) noexcept {
    char learned[512];if(!controlOrigin(session,learned,sizeof(learned)))return true;
    if(trust_&&!trust_(context_,learned)){failLaunch("Could not trust the cloud session control endpoint");return false;}
    return put(out,capacity,learned);
}
void Cloud::remoteSeat(const cJSON* session,const char* base) noexcept {
    const int status=sessionStatus(session);const char* id=str(session,"sessionId");
    if(status<1||status>6)return;
    if(!safeId(id)||std::strlen(id)>=sizeof(remote_.id)){remoteCount_=33;return;}
    if(*session_.id&&std::strcmp(id,session_.id))return;
    for(unsigned i=0;i<remoteCount_&&i<32;++i)if(!std::strcmp(id,remoteIds_[i]))return;
    if(remoteCount_>=32){remoteCount_=33;return;}
    copy(remoteIds_[remoteCount_++],id);
    if(remoteCount_!=1)return;
    remote_={};copy(remote_.id,id);copy(remote_.base,base);copy(remote_.control,base);
    int value;if(integer(obj(obj(session,"sessionRequestData"),"appId"),value)&&value>0)std::snprintf(remote_.appId,sizeof(remote_.appId),"%d",value);
    char control[sizeof(remote_.control)];if(controlOrigin(session,control,sizeof(control)))copy(remote_.control,control);
}
RecoveryResult Cloud::recover(const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings,unsigned audioChannels,const Session* retained,bool allowRetainedResume) noexcept {
    currentNow_=now;recoveryBlocked_=true;
    const auto failed=[&](const char* message){failLaunch(message);return RecoveryResult::failed;};
    if(validateSettings(settings)!=SettingsError::none||(audioChannels!=2&&audioChannels!=6&&audioChannels!=8))return failed("Invalid recovery stream settings");
    const std::uint64_t began=monotonicMs();
    if(!began)return failed("Could not start the bounded session recovery check");
    if(!connect(jwt,device,view_))return RecoveryResult::failed;
    const bool conflict=view_.sessionConflict;
    if(!conflict||now>=conflictUntil_||!remoteCount_) {
        remote_={};remoteCount_=0;secureErase(remoteIds_,sizeof(remoteIds_));
        char regions[32][512]{};unsigned count=1;copy(regions[0],base_);bool incomplete=false;
        char url[768];std::snprintf(url,sizeof(url),"%sv2/serverInfo",base_);
        auto info=request_(context_,"GET",url,nullptr,jwt,device);
        if(info.status==401||info.status==403)return failed("Authentication required to check existing sessions");
        {
            Json root(info);const auto* metadata=obj(root.p,"metaData");
            if(info.error||info.status<200||info.status>=300||!cJSON_IsArray(metadata)||(obj(root.p,"requestStatus")&&number(obj(root.p,"requestStatus"),"statusCode")!=1))incomplete=true;
            else {
                const auto metadataValue=[&](const char* key){const cJSON* entry;cJSON_ArrayForEach(entry,metadata)if(!std::strcmp(str(entry,"key"),key))return str(entry,"value");return "";};
                const auto add=[&](const char* name){
                    if(!*name)return;
                    char origin[512];if(!cloudControlOrigin(metadataValue(name),origin,sizeof(origin)))return;
                    for(unsigned i=0;i<count;++i)if(!std::strcmp(regions[i],origin))return;
                    if(count==32){incomplete=true;return;}
                    if(trust_&&!trust_(context_,origin)){incomplete=true;return;}
                    copy(regions[count++],origin);
                };
                add(metadataValue("local-region"));
                const char* names=metadataValue("gfn-regions");
                while(*names){const char* end=std::strchr(names,',');if(!end)end=names+std::strlen(names);while(names<end&&*names==' ')++names;const char* last=end;while(last>names&&last[-1]==' ')--last;char name[128];const auto length=static_cast<std::size_t>(last-names);if(length&&length<sizeof(name)){std::memcpy(name,names,length);name[length]=0;add(name);}else if(length)incomplete=true;names=*end?end+1:end;}
            }
        }
        for(unsigned i=0;i<count;++i) {
            const auto elapsed=monotonicMs()-began;if(elapsed>9000){incomplete=true;break;}
            std::snprintf(url,sizeof(url),"%sv2/session",regions[i]);auto response=request_(context_,"GET",url,nullptr,jwt,device);
            if(response.status==401||response.status==403)return failed("Authentication required to check existing sessions");
            Json root(response);const auto* sessions=obj(root.p,"sessions");
            if(response.error||response.status<200||response.status>=300||number(obj(root.p,"requestStatus"),"statusCode")!=1||!cJSON_IsArray(sessions)){incomplete=true;continue;}
            const cJSON* candidate;cJSON_ArrayForEach(candidate,sessions)remoteSeat(candidate,regions[i]);
        }
        if(!remoteCount_&&incomplete)return failed("Could not check every cloud region. Retry session recovery before launching");
    }
    conflictUntil_=0;
    if(!remoteCount_) {
        clearSession();recoveryBlocked_=false;view_.launchError=view_.sessionConflict=false;
        view_.state=view_.count?CloudState::catalog:CloudState::idle;copy(view_.message,"Choose a game and store");return RecoveryResult::none;
    }
    if(remoteCount_!=1||(conflict&&*view_.current.id&&std::strcmp(view_.current.id,remote_.appId))) {
        view_.sessionConflict=true;failLaunch("Another cloud game is active. No session was changed");return RecoveryResult::conflict;
    }
    if(trust_&&!trust_(context_,remote_.control))return failed("Could not trust the existing session endpoint");
    char lookupBase[512];copy(lookupBase,remote_.control);
    char url[768];std::snprintf(url,sizeof(url),"%sv2/session/%s",remote_.control,remote_.id);
    auto response=request_(context_,"GET",url,nullptr,jwt,device);
    bool lookupFailed=response.error||response.status<200||response.status>=300;
    if(!lookupFailed){Json payload(response);lookupFailed=number(obj(payload.p,"requestStatus"),"statusCode")!=1;}
    if(lookupFailed&&response.status!=401&&response.status!=403&&std::strcmp(remote_.control,remote_.base)) {
        copy(lookupBase,remote_.base);
        std::snprintf(url,sizeof(url),"%sv2/session/%s",remote_.base,remote_.id);response=request_(context_,"GET",url,nullptr,jwt,device);
    }
    if(response.error||response.status<200||response.status>=300)return failed("Unable to refresh the existing cloud session. No new game was launched");
    Json root(response);const auto* session=obj(root.p,"session");
    if(number(obj(root.p,"requestStatus"),"statusCode")!=1||std::strcmp(str(session,"sessionId"),remote_.id))return failed("Existing session lookup returned invalid data or a different ID");
    const int status=sessionStatus(session);
    if(status==7){clearSession();recoveryBlocked_=false;view_.launchError=view_.sessionConflict=false;view_.state=view_.count?CloudState::catalog:CloudState::idle;copy(view_.message,"Cloud session ended");return RecoveryResult::none;}
    if(status<1||status>6)return failed("The existing session is no longer resumable");
    const auto* request=obj(session,"sessionRequestData");
    const int existingApp=number(request,"appId",0);
    if(conflict&&*view_.current.id){int requestedApp;if(!appId(view_.current.id,requestedApp)||existingApp!=requestedApp){view_.sessionConflict=true;failLaunch("Another cloud game is active. No session was changed");return RecoveryResult::conflict;}}
    const bool retainedMatches=retained&&!std::strcmp(retained->id,remote_.id);
    const bool hasRetained=retainedMatches&&validateSettings(retained->settings)==SettingsError::none;
    const Session saved=hasRetained?*retained:Session{};
    copy(session_.id,remote_.id);session_.recovered=true;copy(controlBase_,lookupBase);
    audioCapacity_=audioChannels;
    if(!controlEndpoint(session,controlBase_,sizeof(controlBase_)))return RecoveryResult::failed;
    if(retainedMatches&&!allowRetainedResume)return failed("This session cannot be resumed. Stop it explicitly to launch again");
    if(existingApp<=0)return failed("Existing session is missing its game identity. The session is retained");
    view_.current={};if(existingApp>0)std::snprintf(view_.current.id,sizeof(view_.current.id),"%d",existingApp);
    copy(view_.current.title,*str(request,"internalTitle")?str(request,"internalTitle"):"Existing cloud game");
    for(const auto* catalog:{&view_,&library_})for(unsigned i=0;i<catalog->count;++i)if(!std::strcmp(catalog->games[i].id,view_.current.id))view_.current=catalog->games[i];
    bool webrtc=false;const cJSON* metadata;cJSON_ArrayForEach(metadata,obj(request,"metaData"))if(!std::strcmp(str(metadata,"key"),"GSStreamerType")&&!std::strcmp(str(metadata,"value"),"WebRTC"))webrtc=true;
    if((!webrtc&&!hasRetained)||cJSON_IsTrue(obj(request,"secureRTSPSupported"))||number(request,"streamerVersion")==14)return failed("Existing session uses an unsupported transport. Stop it explicitly to launch again");
    Session baseline=hasRetained?saved:Session{};if(!hasRetained){baseline.settings=settings;baseline.audioChannels=audioChannels;}
    if(const char* error=acceptedProfile(session,baseline,hasRetained,audioChannels,nullptr,session_))return failed(error);
    allocationSettings_=session_.settings;const unsigned channels=session_.audioChannels;const int hdr=session_.settings.hdr()?1:0;
    if(status>=2&&status<=5) {
        char subSession[37];if(!newSubSession(subSession))return failed("Could not create resume identity. The session is retained");
        auto* body=cJSON_CreateObject();cJSON_AddNumberToObject(body,"action",2);cJSON_AddStringToObject(body,"data","RESUME");cJSON_AddNullToObject(body,"metaData");cJSON_AddNullToObject(body,"adUpdates");
        auto* data=cJSON_AddObjectToObject(body,"sessionRequestData");cJSON_AddNumberToObject(data,"appId",existingApp);cJSON_AddNumberToObject(data,"audioMode",2);cJSON_AddNumberToObject(data,"remoteControllersBitmap",1);cJSON_AddNumberToObject(data,"sdrHdrMode",hdr);
        for(const char* key:{"networkTestSessionId","internalTitle","parentSessionId"})cJSON_AddNullToObject(data,key);
        cJSON_AddItemToArray(cJSON_AddArrayToObject(data,"availableSupportedControllers"),cJSON_CreateNumber(2));cJSON_AddNumberToObject(data,"preferredController",2);
        cJSON_AddStringToObject(data,"clientVersion","30.0");cJSON_AddStringToObject(data,"deviceHashId",device);cJSON_AddStringToObject(data,"clientPlatformName",*str(request,"clientPlatformName")?str(request,"clientPlatformName"):"Windows");cJSON_AddNumberToObject(data,"surroundAudioInfo",0);cJSON_AddNumberToObject(data,"clientTimezoneOffset",0);cJSON_AddStringToObject(data,"clientIdentification","GFN-PC");cJSON_AddNumberToObject(data,"streamerVersion",1);cJSON_AddBoolToObject(data,"secureRTSPSupported",false);cJSON_AddStringToObject(data,"sdkVersion","1.0");cJSON_AddNumberToObject(data,"enhancedStreamMode",1);
        for(const char* key:{"appLaunchMode","enablePersistingInGameSettings"})if(const auto* original=obj(request,key))cJSON_AddItemToObject(data,key,cJSON_Duplicate(original,true));
        auto* entries=cJSON_AddArrayToObject(data,"metaData");const auto count=std::to_string(channels);
        const char* keys[]={"wssignaling","SubSessionId","surroundAudioInfo","GSStreamerType"};const char* values[]={"1",subSession,count.c_str(),"WebRTC"};
        for(unsigned i=0;i<4;++i){auto* entry=cJSON_CreateObject();cJSON_AddStringToObject(entry,"key",keys[i]);cJSON_AddStringToObject(entry,"value",values[i]);cJSON_AddItemToArray(entries,entry);}
        char* encoded=cJSON_PrintUnformatted(body);cJSON_Delete(body);if(!encoded)return failed("Out of memory resuming the existing session");
        std::snprintf(url,sizeof(url),"%sv2/session/%s?keyboardLayout=en-US_qwerty&languageCode=en_US",controlBase_,session_.id);
        auto resumed=request_(context_,"PUT",url,encoded,jwt,device);cJSON_free(encoded);Json result(resumed);const auto* statusData=obj(result.p,"requestStatus");
        const bool notPaused=number(statusData,"statusCode")==34||containsInsensitive(str(statusData,"statusDescription"),"SESSION_NOT_PAUSED");
        if(resumed.error||resumed.status==401||resumed.status==403||(!notPaused&&(resumed.status<200||resumed.status>=300||number(statusData,"statusCode")!=1)))return failed("Cloud session resume failed. The existing session is retained");
    }
    session_.signaling[0]=session_.mediaIp[0]=0;session_.mediaPort=0;session_.signalingSource=SignalingSource::none;
    resumePending_=true;resumeDeadline_=now+90;resumePolls_=pollFailures_=0;nextPoll_=now+1;
    recoveryBlocked_=false;view_.launchError=view_.sessionConflict=false;view_.queuePosition=view_.setupStep=-1;
    view_.state=CloudState::queued;copy(view_.message,"Cloud session resuming. Waiting for fresh server status...");return RecoveryResult::pending;
}
bool Cloud::parseSession(const Response& r) noexcept {
    view_.queuePosition=view_.setupStep=-1;
    if(!r.error&&r.status==404&&*session_.id){clearSession();recoveryBlocked_=false;failLaunch("Cloud session ended");return false;}
    if(r.error){failLaunch(r.error);return false;}Json root(r);auto* sess=obj(root.p,"session");
    if(!*session_.id&&r.status!=401&&sessionConflict(root.p)) {
        remote_={};remoteCount_=0;
        const cJSON* candidate;cJSON_ArrayForEach(candidate,obj(root.p,"otherUserSessions"))remoteSeat(candidate,base_);
        remoteSeat(sess,base_);conflictUntil_=currentNow_+30;
        recoveryBlocked_=true;view_.sessionConflict=true;
        failLaunch("A cloud session is already active. Resume or stop it before launching another game");return false;
    }
    const auto* requestStatus=obj(root.p,"requestStatus");
    const bool patching=r.status>=200&&r.status<300&&number(requestStatus,"statusCode")==41&&std::strstr(str(requestStatus,"statusDescription"),"APP_PATCHING_STATUS");
    if(!patching&&(r.status<200||r.status>=300||number(requestStatus,"statusCode")!=1)){char text[192];const auto* status=requestStatus;
        // Only bounded error fields reach the UI. Never log the response/token/session ID.
        std::snprintf(text,sizeof(text),"HTTP %ld code %d / %.64s / unified %.24s / session %d",
            r.status,num(status,"statusCode",-1),str(status,"statusDescription"),
            str(status,"unifiedErrorCode"),num(sess,"errorCode",-1));
        if(cJSON_IsNumber(obj(status,"unifiedErrorCode")))std::snprintf(text,sizeof(text),"HTTP %ld code %d / %.64s / unified %d / session %d",r.status,num(status,"statusCode",-1),str(status,"statusDescription"),num(status,"unifiedErrorCode"),num(sess,"errorCode",-1));
        failLaunch(text);return false;}
    if(!cJSON_IsObject(sess)){failLaunch("Cloud session response missing session");return false;}
    const char* returnedId=str(sess,"sessionId");
    if(obj(sess,"sessionId")&&(!safeId(returnedId)||std::strlen(returnedId)>=sizeof(session_.id)||(*session_.id&&std::strcmp(session_.id,returnedId)))){failLaunch("Cloud session response has a different or invalid ID");return false;}
    if(*returnedId)copy(session_.id,returnedId);
    if(!*session_.id){failLaunch("Cloud session response missing ID");return false;}
    const int state=sessionStatus(sess);
    if(state==7){clearSession();recoveryBlocked_=false;failLaunch("Cloud session ended");return false;}
    if((state==4||state==5)&&!resumePending_){failLaunch("Cloud session paused. Resume or stop it before launching again");return false;}
    if(!controlEndpoint(sess,controlBase_,sizeof(controlBase_)))return false;
    if(const char* error=acceptedProfile(sess,session_,true,audioCapacity_,&allocationSettings_,session_)){failLaunch(error);return false;}
    const int step=number(obj(sess,"seatSetupInfo"),"seatSetupStep");view_.setupStep=step>=0&&step<=6?step:-1;
    for(const auto* queue:{obj(sess,"queuePosition"),obj(obj(sess,"seatSetupInfo"),"queuePosition"),obj(obj(sess,"sessionProgress"),"queuePosition"),obj(obj(sess,"progressInfo"),"queuePosition")}){int position;if(integer(queue,position)){view_.queuePosition=position>0?position:-1;break;}}
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
    if(patching||state==6||(resumePending_&&(state==4||state==5))){view_.state=CloudState::queued;copy(view_.message,patching?"Server patching game. Waiting for launch...":"Cloud session resuming. Waiting for server...");}
    else if((state==2||state==3)&&*session_.signaling){resumePending_=false;view_.state=CloudState::ready;copy(view_.message,"Cloud session ready. Connecting stream...");}
    else if(state<0||state>3){failLaunch("Unknown cloud session state");return false;}
    else {
        view_.state=CloudState::queued;
        if(state==2||state==3)copy(view_.message,"Server ready, but no supported streaming address received");
        else if(view_.setupStep==5)copy(view_.message,"Cleaning up your previous session");
        else if(view_.setupStep==6)copy(view_.message,"Waiting for cloud storage");
        else if(view_.queuePosition>0)std::snprintf(view_.message,sizeof(view_.message),"Waiting for an available rig - queue position %d",view_.queuePosition);
        else if(view_.setupStep==1)copy(view_.message,"Waiting for an available rig");
        else if(view_.setupStep==0)copy(view_.message,"Connecting to GeForce NOW");
        else if(view_.setupStep>=2&&view_.setupStep<=4)copy(view_.message,"Configuring your cloud gaming rig");
        else copy(view_.message,"Preparing your game");
    }
    return true;
}
void Cloud::tick(const char* jwt,const char* device,std::uint64_t now) noexcept {
    if(view_.state!=CloudState::queued||now<nextPoll_)return;
    currentNow_=now;
    if(resumePending_&&(now>=resumeDeadline_||resumePolls_++>=60)){failLaunch("Cloud session resume timed out. The session is retained");return;}
    nextPoll_=now+3;char url[768];std::snprintf(url,sizeof(url),"%sv2/session/%s",*controlBase_?controlBase_:base_,session_.id);
    auto r=request_(context_,"GET",url,nullptr,jwt,device);
    const bool cancelled=r.error&&(std::strstr(r.error,"cancel")||std::strstr(r.error,"Cancel"));
    const bool transient=(r.error&&!cancelled)||r.status==408||r.status==425||r.status==429||r.status==500||r.status==502||r.status==503||r.status==504;
    if(transient&&++pollFailures_<3){nextPoll_=now+1;return;}
    pollFailures_=0;parseSession(r);
}
bool Cloud::stop(const char* jwt,const char* device) noexcept {
    if(!*session_.id&&recoveryBlocked_){failLaunch("Check the existing cloud session before stopping it");return false;}
    if(*session_.id){
        char url[768];std::snprintf(url,sizeof(url),"%sv2/session/%s",*controlBase_?controlBase_:base_,session_.id);
        auto r=request_(context_,"DELETE",url,nullptr,jwt,device);
        if(r.error||((r.status!=404&&r.status!=410)&&(r.status<200||r.status>=300))){fail("Unable to stop cloud session");return false;}
        if(r.status!=204&&r.status!=404&&r.status!=410&&r.length){Json root(r);if(!root.p||(obj(root.p,"requestStatus")&&number(obj(root.p,"requestStatus"),"statusCode")!=1)){fail("Cloud service rejected session cleanup. The session is retained");return false;}}
    }
    clearSession();recoveryBlocked_=false;remote_={};remoteCount_=0;conflictUntil_=0;
    view_.launchError=view_.sessionConflict=false;view_.state=view_.count?CloudState::catalog:CloudState::idle;copy(view_.message,"Choose a game and store");return true;
}
}
