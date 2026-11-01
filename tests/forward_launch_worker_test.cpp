// SPDX-License-Identifier: GPL-3.0-or-later
#define main preview_entry
#include "../src/main.cpp"
#undef main
#include "vendor/cJSON.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
std::string testRoot;
unsigned scenario=0,lookups=0,catalogs=0,launches=0,networkTests=0,sessionLists=0,deletes=0;
std::atomic_uint refreshes{0};
std::uint64_t clockUs=1000000;
std::chrono::steady_clock::time_point deadline;
std::atomic_bool verified{false};
bool seed=true,startedSignIn=false,escaped=false,stopped=false;
const char* mapPath(const char* path,std::string& out) {
    if(!std::strcmp(path,OPENNOW_LEGACY_ACCOUNT_PATH)){out=testRoot+"/legacy-missing";return out.c_str();}
    if(!std::strcmp(path,"/download0")){out=testRoot;return out.c_str();}
    const std::size_t prefix=std::strlen(OPENNOW_STORAGE_ROOT);
    assert(!std::strncmp(path,OPENNOW_STORAGE_ROOT,prefix));out=testRoot+(path+prefix);return out.c_str();
}
opennow::Response reply(const char* body,long status=200) {return {status,const_cast<char*>(body),std::strlen(body),nullptr};}
const char* challenge=R"({"device_code":"fixture-device","user_code":"TEST","verification_uri":"https://login.nvidia.com/activate","interval":5})";
const char* serverInfo=R"({"requestStatus":{"statusCode":1,"serverId":"GFN-PC"},"metaData":[{"key":"local-region","value":"west"},{"key":"gfn-regions","value":"west,east"},{"key":"west","value":"https://west.geforcenow.com/"},{"key":"east","value":"https://east.geforcenow.com/"}]})";
const char* existingSeat=R"({"sessionId":"fixture-existing","status":2,"sessionRequestData":{"appId":43,"internalTitle":"Another game","streamerVersion":1,"secureRTSPSupported":false,"sdrHdrMode":0,"clientRequestMonitorSettings":[{"widthInPixels":1280,"heightInPixels":720,"framesPerSecond":30}],"requestedStreamingFeatures":{"codec":1,"bitDepth":0,"chromaFormat":0,"maxBitrateKbps":20000,"audioChannelCount":2},"metaData":[{"key":"GSStreamerType","value":"WebRTC"}]}})";
std::int64_t native(std::int64_t value) {return value<0?static_cast<std::int32_t>(0x80020000U|errno):value;}
}
extern "C" {
int sceKernelOpen(const char* path,int flags,unsigned mode) {std::string mapped;return static_cast<int>(native(::open(mapPath(path,mapped),flags,mode)));}
std::int64_t sceKernelRead(int fd,void* data,std::size_t size) {return native(::read(fd,data,size));}
std::int64_t sceKernelWrite(int fd,const void* data,std::size_t size) {return native(::write(fd,data,size));}
int sceKernelClose(int fd) {return static_cast<int>(native(::close(fd)));}
int sceKernelFsync(int fd) {return static_cast<int>(native(::fsync(fd)));}
int sceKernelFchmod(int fd,unsigned mode) {return static_cast<int>(native(::fchmod(fd,mode)));}
int sceKernelRename(const char* from,const char* to) {std::string a,b;return static_cast<int>(native(::rename(mapPath(from,a),mapPath(to,b))));}
int sceKernelUnlink(const char* path) {std::string mapped;return static_cast<int>(native(::unlink(mapPath(path,mapped))));}
int sceKernelMkdir(const char* path,unsigned mode) {std::string mapped;return static_cast<int>(native(::mkdir(mapPath(path,mapped),mode)));}
unsigned long long sceKernelGetProcessTime() {return clockUs;}
int sceKernelUsleep(unsigned) {
    if(clockUs<50000000)clockUs+=1000000;
    assert(std::chrono::steady_clock::now()<deadline);
    std::this_thread::yield();
    if(scenario==2&&!startedSignIn) {
        assert(!refreshes&&!lookups&&!catalogs&&!launches&&published.state==State::idle);
        startedSignIn=true;command.store(1);
    }
    if(scenario==3&&!escaped) {
        assert(published.state==State::requesting&&!lookups&&!catalogs&&!launches);
        escaped=true;submit(18,{});
    }
    if(scenario==4&&publishedCloud.launchError&&!escaped) {
        assert(!catalogs&&!launches);escaped=true;submit(18,{});
    }
    if(scenario==5&&publishedCloud.launchError&&!escaped) {
        assert(publishedCloud.sessionConflict&&!publishedSession&&!catalogs&&launches==1&&!deletes);
        assert(!std::strcmp(publishedCloud.message,"A cloud session is already active. Resume or stop it before launching another game"));
        escaped=true;submit(18,{});
    }
    if(scenario<=2&&launches) {
        opennow::sessionFile::Saved saved;
        assert(!publishedStreamFailure&&opennow::sessionFile::load(OPENNOW_SESSION_PATH,"fixture-user",saved)==opennow::sessionFile::Status::loaded);
        assert(saved.settings==publishedLaunch&&!std::strcmp(saved.sessionId,"fixture-new"));
        command.store(10);
    }
    if(scenario>=3&&catalogs==2)command.store(10);
    return 0;
}
}
namespace ps5::demo {void requestStop() noexcept {stopped=true;}}
namespace opennow::gpu {
bool settingsAvailable(const StreamSettings& settings) noexcept {return validateSettings(settings)==SettingsError::none&&!settings.hardware();}
StreamSettings bestSettings() noexcept {return settingsFor(StreamProfile::compatibility);}
}
namespace opennow {
Http::Http() noexcept {buffer_=reinterpret_cast<char*>(this);}
Http::~Http()=default;
bool Http::allowCloudEndpoint(void*,const char* url) noexcept {
    assert(!std::strcmp(url,"https://test.geforcenow.com/")||!std::strcmp(url,"https://west.geforcenow.com/")||!std::strcmp(url,"https://east.geforcenow.com/"));
    return true;
}
Response Http::request(void*,const char* method,const char* url,const char* body,const char* bearer,const char* device) noexcept {
    if(std::strstr(url,"/device/authorize"))return reply(challenge);
    if(std::strstr(url,"/token")) {
        if(!seed) {
            ++refreshes;
            if((scenario==1||scenario==3)&&refreshes<=2)return reply("{}",503);
        }
        return reply(R"({"access_token":"fixture-access","id_token":"fixture-cloud","refresh_token":"fixture-refresh","expires_in":3600})");
    }
    if(std::strstr(url,"/userinfo")){if(!seed)verified=true;return reply(R"({"sub":"fixture-user"})");}
    if(std::strstr(url,"/client_token"))return reply(R"({"client_token":"fixture-client"})");
    assert(verified);
    if(std::strstr(url,"serviceUrls")) {
        assert(!std::strcmp(method,"GET")&&!body&&!bearer&&!device);
        return reply(R"({"gfnServiceInfo":{"gfnServiceEndpoints":[{"idpId":"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg","streamingServiceUrl":"https://test.geforcenow.com/"}]}})");
    }
    assert(bearer&&!std::strcmp(bearer,"fixture-cloud")&&device&&std::strlen(device)==36);
    assert(std::strcmp(method,"PUT")&&"Forwarder escape must not resume another cloud game");
    if(std::strstr(url,"serverInfo"))return reply(serverInfo);
    if(std::strstr(url,"graphql")) {
        if(std::strstr(body,"variantIds")) {
            ++lookups;assert(!catalogs&&forwardLaunchMode.load());
            if(scenario==4)return reply(R"({"data":{"apps":{"items":[]}}})");
            return reply(R"({"data":{"apps":{"items":[{"title":"Fixture","gfn":{"playabilityState":"PLAYABLE"},"variants":[{"id":"42","appStore":"XBOX","gfn":{"status":"AVAILABLE","library":{"status":"MANUAL"}}}]}]}}})");
        }
        ++catalogs;assert((escaped||scenario==6)&&!forwardLaunchMode.load());
        return reply(R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[]}}})");
    }
    if(!std::strcmp(url,"https://test.geforcenow.com/v2/nettestsession")) {
        assert(!std::strcmp(method,"POST")&&lookups==1&&!catalogs&&!launches);
        ++networkTests;
        return reply(R"({"requestStatus":{"statusCode":1},"netTestSession":{"sessionId":"fixture-net"}})");
    }
    if(!std::strcmp(method,"GET")&&std::string(url).ends_with("/v2/session")) {
        assert((escaped||scenario==6)&&!forwardLaunchMode.load());++sessionLists;
        if(scenario==5&&!std::strcmp(url,"https://test.geforcenow.com/v2/session")) {
            static std::string response=std::string(R"({"requestStatus":{"statusCode":1},"sessions":[)")+existingSeat+"]}";
            return reply(response.c_str());
        }
        return reply(R"({"requestStatus":{"statusCode":1},"sessions":[]})");
    }
    if(!std::strcmp(method,"GET")&&!std::strcmp(url,"https://test.geforcenow.com/v2/session/fixture-existing")) {
        static std::string response=std::string(R"({"requestStatus":{"statusCode":1},"session":)")+existingSeat+"}";
        return reply(response.c_str());
    }
    if(!std::strcmp(method,"POST")&&!std::strcmp(url,"https://test.geforcenow.com/v2/session?keyboardLayout=en-US_qwerty&languageCode=en_US")) {
        ++launches;assert(lookups==1&&!catalogs);
        auto* root=cJSON_Parse(body);assert(root);
        const auto* request=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
        assert(cJSON_GetObjectItemCaseSensitive(request,"appId")->valueint==42);
        assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(request,"networkTestSessionId")->valuestring,"fixture-net"));
        const auto* features=cJSON_GetObjectItemCaseSensitive(request,"requestedStreamingFeatures");
        assert(cJSON_GetObjectItemCaseSensitive(features,"maxBitrateKbps")->valueint==20000);cJSON_Delete(root);
        if(scenario==5)return reply(R"({"requestStatus":{"statusCode":11},"otherUserSessions":[{"sessionId":"fixture-existing","status":2,"sessionRequestData":{"appId":43}}]})",403);
        return reply(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-new","status":0}})");
    }
    assert(!std::strcmp(method,"DELETE")&&!std::strcmp(url,"https://test.geforcenow.com/v2/session/fixture-new"));
    ++deletes;return reply("{}");
}
}
static void runScenario(unsigned value) {
    scenario=value;
    char root[]="/tmp/opennow-forward-worker-XXXXXX";assert(mkdtemp(root));testRoot=root;
    auto settings=opennow::settingsFor(StreamProfile::quality);settings.bitrate_kbps=20000;
    assert(!opennow::settingsFile::save(OPENNOW_SETTINGS_PATH,{true,settings,true}));
    opennow::Http http;activeHttp=&http;
    if(scenario!=2) {
        opennow::Login login(opennow::Http::request,&http,OPENNOW_ACCOUNT_PATH);
        login.begin("00000000-0000-4000-8000-000000000000",0);login.tick(5);
        assert(login.view().sessionSaved);
    }
    seed=false;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const char* args[]={"--gfn-app-id","42","--store","XBOX"};
    launchArgs=opennow::forwarder::parseLaunchArgs(4,args);forwardLaunchMode.store(true);
    if(scenario==6){launchArgs={};forwardLaunchMode.store(false);}
    worker(nullptr);assert(stopped);
    if(scenario<=2)assert(launches==1&&lookups==1&&networkTests==1&&!catalogs&&!sessionLists&&deletes==1&&publishedLaunch==settings);
    if(scenario==1)assert(refreshes==3);
    if(scenario==3)assert(!lookups&&!launches&&catalogs==2&&refreshes==3);
    if(scenario==4)assert(lookups==1&&!launches&&catalogs==2);
    if(scenario==3||scenario==4)assert(!sessionLists&&!networkTests&&!deletes);
    if(scenario==5)assert(lookups==1&&launches==1&&networkTests==1&&catalogs==2&&!publishedSession&&!sessionLists&&!deletes);
    if(scenario==6)assert(!lookups&&!launches&&!networkTests&&catalogs==2&&sessionLists==3&&!deletes);
    assert(std::filesystem::exists(testRoot+"/account.bin"));
    assert(!std::filesystem::exists(testRoot+"/session.bin"));
    std::filesystem::remove_all(testRoot);
}
int main() {
    for(unsigned scenario=0;scenario<7;++scenario) {
        const pid_t child=fork();assert(child>=0);
        if(!child){runScenario(scenario);std::exit(0);}
        int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
    std::puts("forward worker auth, refresh, saved settings and normal-launcher escape tests passed");
}
