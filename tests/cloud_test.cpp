#include "cloud.hpp"
#include "vendor/cJSON.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
#include <map>
#include <vector>
using namespace opennow;
static Response response(const char* s){return {200,const_cast<char*>(s),std::strlen(s),nullptr};}
struct Mock {
 std::map<std::string,const char*> byCursor,listsByUrl,seatByUrl;
 std::map<std::string,long> listStatuses,seatStatuses;
 unsigned graphql=0,posts=0,deletes=0,nettests=0,puts=0,lists=0,attempts=0;
 bool reject=false,stopError=false,pollError=false,rejectTrust=false;
 const char* poll=nullptr;std::string body,netBody,catalogBody,putBody,deleteUrl;
 const char* catalog=nullptr;const char* created=nullptr;long stopStatus=200,pollStatus=200,putStatus=200,createStatus=200;
 StreamSettings* mutateSettings=nullptr;
 const char* stopBody="{}";
 const char* serverInfo=R"({"requestStatus":{"statusCode":1,"serverId":"GFN-PC"},"metaData":[]})";
 const char* remote=R"({"requestStatus":{"statusCode":1},"sessions":[]})";
 const char* lookup=nullptr;
 const char* putResponse=R"({"requestStatus":{"statusCode":1}})";
 std::vector<std::string> urls,trusted;
};
static bool trust(void* p,const char* url){auto& mock=*static_cast<Mock*>(p);mock.trusted.emplace_back(url);return !mock.rejectTrust;}
static Response request(void* p,const char* method,const char* url,const char* body,const char*,const char*) {
 auto& m=*static_cast<Mock*>(p);
 m.urls.emplace_back(url);
 if(std::strstr(url,"serviceUrls"))return response(R"({"gfnServiceInfo":{"gfnServiceEndpoints":[{"idpId":"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg","streamingServiceUrl":"https://test.geforcenow.com/"}]}})");
 if(std::strstr(url,"serverInfo"))return response(m.serverInfo);
 if(std::strstr(url,"graphql")){m.catalogBody=body;++m.graphql;
  if(!m.byCursor.empty()){auto* root=cJSON_Parse(body);std::string cursor=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"variables"),"cursor")->valuestring;cJSON_Delete(root);
   auto found=m.byCursor.find(cursor);if(found==m.byCursor.end()){auto r=response("{}");r.status=500;return r;}return response(found->second);}
  if(m.catalog)return response(m.catalog);
  return response(R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Fixture Game","variants":[{"id":"42","appStore":"XBOX"},{"id":"43","appStore":"STEAM"}]}]}}})");}
 if(std::strstr(url,"nettestsession")){++m.nettests;m.netBody=body;if(m.mutateSettings){*m.mutateSettings={};m.mutateSettings=nullptr;}return response(R"({"requestStatus":{"statusCode":1},"netTestSession":{"sessionId":"net-fixture"}})");}
 if(!std::strcmp(method,"PUT")){++m.puts;m.putBody=body;auto r=response(m.putResponse);r.status=m.putStatus;return r;}
 if(!std::strcmp(method,"GET")&&std::string(url).ends_with("/v2/session")){++m.lists;auto found=m.listsByUrl.find(url);auto r=response(found==m.listsByUrl.end()?m.remote:found->second);auto status=m.listStatuses.find(url);if(status!=m.listStatuses.end())r.status=status->second;return r;}
 if(!std::strcmp(method,"POST")){++m.attempts;if(m.reject){auto r=response(R"({"requestStatus":{"statusCode":4,"statusDescription":"INTERNAL_ERROR_STATUS","unifiedErrorCode":123}})");r.status=500;return r;}++m.posts;m.body=body;if(m.created){auto r=response(m.created);r.status=m.createStatus;return r;}return response(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":0,"queuePosition":5}})");}
 if(!std::strcmp(method,"DELETE")){++m.deletes;m.deleteUrl=url;auto r=response(m.stopBody);r.status=m.stopStatus;if(m.stopError)r.error="fixture network failure";return r;}
 if(auto found=m.seatByUrl.find(url);found!=m.seatByUrl.end()){auto r=response(found->second);if(auto status=m.seatStatuses.find(url);status!=m.seatStatuses.end())r.status=status->second;return r;}
 if(m.lookup)return response(m.lookup);
 if(m.poll||m.pollStatus!=200||m.pollError){auto r=response(m.poll?m.poll:"{}");r.status=m.pollStatus;if(m.pollError)r.error="fixture network failure";return r;}
 return response(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":2,"signalingUrl":"wss://test.geforcenow.com/nvst/"}})");
}
static std::string existingSeat(int status=4) {
 return std::string(R"({"sessionId":"fixture-id","status":)")+std::to_string(status)+R"(,"sessionControlInfo":{"ip":["203.0.113.50"],"port":"443"},"sessionRequestData":{"appId":"42","internalTitle":"Fixture Game","clientPlatformName":"Windows","appLaunchMode":1,"enablePersistingInGameSettings":true,"streamerVersion":1,"secureRTSPSupported":false,"sdrHdrMode":0,"clientRequestMonitorSettings":[{"widthInPixels":1280,"heightInPixels":720,"framesPerSecond":60,"sdrHdrMode":0}],"requestedStreamingFeatures":{"codec":1,"bitDepth":0,"chromaFormat":0,"maxBitrateKbps":12000,"dynamicStreamingMode":3,"audioChannelCount":2},"metaData":[{"key":"GSStreamerType","value":"WebRTC"}]},"signalingUrl":"wss://stale.geforcenow.com/nvst/"})";
}
static std::string envelope(const std::string& seat){return std::string(R"({"requestStatus":{"statusCode":1},"session":)")+seat+"}";}
static std::string remoteList(const std::string& seats){return std::string(R"({"requestStatus":{"statusCode":1},"sessions":[)")+seats+"]}";}
static void requestParityTests() {
 Mock mock;Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");std::string previous;
 for(unsigned i=0;i<2;++i) {
  client.launch("fixture-jwt","fixture-device",1);
  auto* body=cJSON_Parse(mock.body.c_str());const auto* data=cJSON_GetObjectItemCaseSensitive(body,"sessionRequestData");assert(data);
  assert(cJSON_GetObjectItemCaseSensitive(data,"appId")->valueint==42&&!cJSON_GetObjectItemCaseSensitive(data,"cmsId"));
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(data,"internalTitle")->valuestring,"Fixture Game"));
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(data,"clientPlatformName")->valuestring,"Windows"));
  assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(data,"partnerCustomData"))&&cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(data,"accountLinked")));
  assert(cJSON_GetObjectItemCaseSensitive(data,"preferredController")->valueint==2&&cJSON_GetObjectItemCaseSensitive(data,"clientTimezoneOffset")->valueint==0);
  assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(data,"clientRequestMonitorSettings"),0),"dpi")->valueint==96);
  const auto* dlss=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(data,"requestedStreamingFeatures"),"dlssOverrides");
  for(const char* key:{"srMode","srModel","rrModel","fgMode","fgModel"})assert(cJSON_GetObjectItemCaseSensitive(dlss,key)->valueint==0);
  assert(cJSON_GetObjectItemCaseSensitive(dlss,"srScale")->valueint==100&&!cJSON_GetObjectItemCaseSensitive(dlss,"fgMultiplier"));
  std::string subSession;const cJSON* entry;cJSON_ArrayForEach(entry,cJSON_GetObjectItemCaseSensitive(data,"metaData"))if(!std::strcmp(cJSON_GetObjectItemCaseSensitive(entry,"key")->valuestring,"SubSessionId"))subSession=cJSON_GetObjectItemCaseSensitive(entry,"value")->valuestring;
  assert(subSession.size()==36&&subSession[14]=='4'&&std::strchr("89ab",subSession[19])&&subSession!=previous&&subSession!="fixture-device");previous=subSession;
  cJSON_Delete(body);assert(mock.netBody.find("\"clientPlatformName\":\"GFN-PC\"")!=std::string::npos);assert(client.stop("fixture-jwt","fixture-device"));
 }
 for(const char* features:{R"([{"key":"IN_GAME_SETTINGS_PERSISTENCE_ENABLED","value":true}])",R"({"key":"IN_GAME_SETTINGS_PERSISTENCE_ENABLED","value":"TRUE"})",R"([{"key":"IN_GAME_SETTINGS_PERSISTENCE_ENABLED","value":false}])",R"([{"key":"OTHER","value":true}])"}) {
  const bool supported=(std::strstr(features,"true")&&!std::strstr(features,"OTHER"))||std::strstr(features,"TRUE");
  const auto catalog=std::string(R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Owned fixture","variants":[{"id":"42","appStore":"STEAM","gfn":{"library":{"status":"MANUAL"},"features":)")+features+"}}]}]}}}";
  mock.catalog=catalog.c_str();client.load("fixture-jwt","fixture-device");assert(client.view().games[0].supportsPersistence==supported);
  assert(mock.catalogBody.find("GfnSubscriptionFeatureValue")!=std::string::npos);
  for(bool enabled:{false,true}){StreamSettings settings;settings.persist_game_settings=enabled;client.launch("fixture-jwt","fixture-device",1,settings);auto* body=cJSON_Parse(mock.body.c_str());const auto* data=cJSON_GetObjectItemCaseSensitive(body,"sessionRequestData");assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data,"accountLinked")));assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data,"enablePersistingInGameSettings"))==(enabled&&supported));cJSON_Delete(body);assert(client.stop("fixture-jwt","fixture-device"));}
 }
 for(const char* id:{"0","2147483648","99999999999999999999999999999999999999999","-42","2147483647"}) {
  const auto catalog=std::string(R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"ID fixture","variants":[{"id":")")+id+R"(","appStore":"STEAM"}]}]}}})";
  mock.catalog=catalog.c_str();client.load("fixture-jwt","fixture-device");const auto posts=mock.posts,nettests=mock.nettests;client.launch("fixture-jwt","fixture-device",1);
  if(!std::strcmp(id,"2147483647")){assert(mock.posts==posts+1&&*client.session().id);assert(client.stop("fixture-jwt","fixture-device"));}
  else assert(mock.posts==posts&&mock.nettests==nettests&&client.view().launchError);
 }
}
static void conflictTests() {
 for(const std::string& status:{std::string(R"({"statusCode":11})"),std::string(R"({"statusCode":4,"statusDescription":"session_limit"})"),std::string(R"({"statusCode":4,"unifiedErrorCode":"0x4af1201e"})"),std::string(R"({"statusCode":4,"unifiedErrorCode":)")+std::to_string(0x4af1201e)+"}"}) {
  Mock mock;const auto seat=existingSeat(),created=std::string("{\"requestStatus\":")+status+",\"otherUserSessions\":["+seat+"]}",lookup=envelope(seat);mock.created=created.c_str();mock.createStatus=403;mock.lookup=lookup.c_str();Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");client.launch("fixture-jwt","fixture-device",1);
  assert(client.view().sessionConflict&&!*client.session().id&&mock.attempts==1&&mock.puts==0&&mock.deletes==0);
  client.dismissLaunchError();client.launch("fixture-jwt","fixture-device",2);assert(mock.attempts==1);
  assert(client.recover("fixture-jwt","fixture-device",3,StreamSettings{})==RecoveryResult::pending&&mock.lists==0&&mock.puts==1&&mock.attempts==1&&mock.deletes==0);
  assert(client.stop("fixture-jwt","fixture-device"));
 }
 {
  Mock mock;auto seat=existingSeat();seat.replace(seat.find("\"appId\":\"42\""),12,"\"appId\":\"43\"");const auto created=std::string(R"({"requestStatus":{"statusCode":11},"otherUserSessions":[)")+seat+"]}";mock.created=created.c_str();Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");client.launch("fixture-jwt","fixture-device",1);
  assert(client.recover("fixture-jwt","fixture-device",2,StreamSettings{})==RecoveryResult::conflict&&mock.puts==0&&mock.deletes==0&&!*client.session().id);
  assert(client.recover("fixture-jwt","fixture-device",32,StreamSettings{})==RecoveryResult::none&&mock.lists==1&&mock.attempts==1);
 }
 {
  Mock mock;mock.createStatus=401;mock.created=R"({"requestStatus":{"statusCode":11}})";Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");client.launch("fixture-jwt","fixture-device",1);assert(!client.view().sessionConflict&&client.view().launchError&&!*client.session().id);
 }
}
static void retainedProfileTests() {
 for(int retainedKind:{0,1,2}) {
  Mock mock;auto* root=cJSON_Parse(existingSeat().c_str());auto* data=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");cJSON_DeleteItemFromObjectCaseSensitive(data,"requestedStreamingFeatures");cJSON_DeleteItemFromObjectCaseSensitive(data,"clientRequestMonitorSettings");cJSON_DeleteItemFromObjectCaseSensitive(data,"sdrHdrMode");cJSON_DeleteItemFromObjectCaseSensitive(data,"metaData");char* encoded=cJSON_PrintUnformatted(root);const std::string seat=encoded;cJSON_free(encoded);cJSON_Delete(root);
  const auto listed=remoteList(seat),lookup=envelope(seat);mock.remote=listed.c_str();mock.lookup=lookup.c_str();Session saved;std::snprintf(saved.id,sizeof(saved.id),"%s",retainedKind==2?"different-id":"fixture-id");saved.settings=settingsFor(StreamProfile::smooth);saved.settings.bitrate_kbps=17000;
  Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{},2,retainedKind?&saved:nullptr)==(retainedKind==1?RecoveryResult::pending:RecoveryResult::failed));
  if(retainedKind==1)assert(client.session().settings.width==1280&&client.session().settings.height==720&&client.session().settings.fps==60&&client.session().settings.bitrate_kbps==17000&&mock.puts==1);
  else assert(mock.puts==0);
  assert(mock.deletes==0&&mock.posts==0);assert(client.stop("fixture-jwt","fixture-device"));
 }
 {
  Mock mock;auto* root=cJSON_Parse(existingSeat().c_str());auto* data=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");auto* features=cJSON_GetObjectItemCaseSensitive(data,"requestedStreamingFeatures");cJSON_ReplaceItemInObjectCaseSensitive(features,"codec",cJSON_CreateNumber(2));cJSON_ReplaceItemInObjectCaseSensitive(features,"bitDepth",cJSON_CreateNumber(1));cJSON_AddNumberToObject(root,"sdrHdrMode",0);char* encoded=cJSON_PrintUnformatted(root);const std::string seat=encoded;cJSON_free(encoded);cJSON_Delete(root);
  const auto listed=remoteList(seat),lookup=envelope(seat);mock.remote=listed.c_str();mock.lookup=lookup.c_str();Session saved;std::strcpy(saved.id,"fixture-id");saved.settings=settingsFor(StreamProfile::native_hdr120);saved.audioChannels=8;
  Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{},8,&saved)==RecoveryResult::pending);
  assert(client.session().settings.mode==VideoMode::hevcMain10SdrHardware&&client.session().settings.width==1280&&client.session().settings.fps==60&&client.session().settings.bitrate_kbps==12000&&client.session().audioChannels==2);
  assert(saved.settings.hdr());assert(client.stop("fixture-jwt","fixture-device"));
 }
}
static void acceptedProfileTests() {
 {
  Mock mock;mock.created=R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":1,"sdrHdrMode":0,"monitorSettings":[{"widthInPixels":1920,"heightInPixels":1080,"framesPerSecond":60}],"finalizedStreamingFeatures":{"codec":1,"bitDepth":0,"chromaFormat":0}}})";
  Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");client.launch("fixture-jwt","fixture-device",1,settingsFor(StreamProfile::native_hdr120));
  assert(client.view().state==CloudState::queued&&client.session().settings.mode==VideoMode::h264Hardware&&!client.session().settings.hdr());
  assert(client.session().settings.width==1920&&client.session().settings.height==1080&&client.session().settings.fps==60);
  mock.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":2,"signalingUrl":"wss://test.geforcenow.com/nvst/","negotiatedStreamProfile":{"resolution":"1280x720","width":1280,"height":720,"fps":60,"codec":"hevc","colorQuality":"10bit_420","bitDepth":10,"chromaFormat":0,"enableHdr":false}}})";
  client.tick("fixture-jwt","fixture-device",4);
  assert(client.view().state==CloudState::ready&&client.session().settings.mode==VideoMode::hevcMain10SdrHardware&&client.session().settings.width==1280&&client.session().settings.height==720);
  assert(client.stop("fixture-jwt","fixture-device"));
 }
 for(const char* invalid:{R"(,"sdrHdrMode":"bad")",R"(,"sdrHdrMode":2)",R"(,"finalizedStreamingFeatures":{"sdrHdrMode":"1x"})",R"(,"finalizedStreamingFeatures":{"sdrHdrMode":2})",R"(,"negotiatedStreamProfile":{"enableHdr":2})",R"(,"negotiatedStreamProfile":{"enableHdr":"false"})",R"(,"negotiatedStreamProfile":{"codec":"AV1"})",R"(,"finalizedStreamingFeatures":{"codec":99},"negotiatedStreamProfile":{"codec":"H264"})",R"(,"finalizedStreamingFeatures":{"bitDepth":99},"negotiatedStreamProfile":{"colorQuality":"8bit_420"})",R"(,"negotiatedStreamProfile":{"resolution":"1920xbad"})",R"(,"negotiatedStreamProfile":{"resolution":"1280x720","width":1920})",R"(,"negotiatedStreamProfile":{"colorQuality":"10bit_444"})",R"(,"negotiatedStreamProfile":{"fps":60.5})",R"(,"negotiatedStreamProfile":{"chromaFormat":1})"}) {
  Mock mock;const auto original=existingSeat(1);const auto seat=original.substr(0,original.size()-1)+invalid+"}",created=envelope(seat),listed=remoteList(seat);
  mock.created=created.c_str();Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");client.launch("fixture-jwt","fixture-device",1);
  assert(client.view().state==CloudState::failed&&*client.session().id&&mock.deletes==0);assert(client.stop("fixture-jwt","fixture-device"));
  mock.remote=listed.c_str();mock.lookup=created.c_str();Session saved;std::strcpy(saved.id,"fixture-id");saved.settings=settingsFor(StreamProfile::smooth);
  assert(client.recover("fixture-jwt","fixture-device",2,StreamSettings{},2,&saved)==RecoveryResult::failed);
  assert(client.session().recovered&&*client.session().id&&mock.puts==0&&mock.deletes==1);assert(client.stop("fixture-jwt","fixture-device"));
 }
 {
  Mock mock;auto* root=cJSON_Parse(existingSeat().c_str());auto* data=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");cJSON_DeleteItemFromObjectCaseSensitive(data,"clientRequestMonitorSettings");cJSON_DeleteItemFromObjectCaseSensitive(data,"requestedStreamingFeatures");cJSON_DeleteItemFromObjectCaseSensitive(data,"sdrHdrMode");auto* normalized=cJSON_AddObjectToObject(root,"negotiatedStreamProfile");cJSON_AddStringToObject(normalized,"resolution","1280x720");cJSON_AddNumberToObject(normalized,"fps",60);cJSON_AddStringToObject(normalized,"codec","H264");cJSON_AddStringToObject(normalized,"colorQuality","8bit_420");cJSON_AddBoolToObject(normalized,"enableHdr",false);char* encoded=cJSON_PrintUnformatted(root);const std::string seat=encoded;cJSON_free(encoded);cJSON_Delete(root);
  const auto listed=remoteList(seat),lookup=envelope(seat);mock.remote=listed.c_str();mock.lookup=lookup.c_str();Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==RecoveryResult::pending);
  assert(client.session().settings.width==1280&&client.session().settings.height==720&&client.session().settings.fps==60&&client.session().settings.codec()==VideoCodec::h264);assert(client.stop("fixture-jwt","fixture-device"));
 }
 for(bool matches:{false,true}) {
  Mock mock;const auto seat=existingSeat(),listed=remoteList(seat),lookup=envelope(seat);mock.remote=listed.c_str();mock.lookup=lookup.c_str();Session saved;std::strcpy(saved.id,matches?"fixture-id":"different-id");Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{},2,&saved,false)==(matches?RecoveryResult::failed:RecoveryResult::pending));
  assert(mock.puts==static_cast<unsigned>(!matches)&&*client.session().id&&client.session().recovered&&mock.deletes==0);assert(client.stop("fixture-jwt","fixture-device"));
 }
 for(long status:{200L,500L,503L}) {
  Mock mock;mock.created="{}";mock.createStatus=status;Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");client.launch("fixture-jwt","fixture-device",1);assert(client.recoveryRequired()&&!*client.session().id);client.dismissLaunchError();client.launch("fixture-jwt","fixture-device",2);assert(mock.attempts==1);
  assert(client.recover("fixture-jwt","fixture-device",3,StreamSettings{})==RecoveryResult::none&&!client.recoveryRequired());
 }
}
static void recoveryTests() {
 for(int status:{1,2,3,4,5,6}) {
  Mock mock;const auto seat=existingSeat(status),listed=remoteList(seat),lookup=envelope(seat);
  mock.remote=listed.c_str();mock.lookup=lookup.c_str();Cloud client(request,&mock,trust);
  client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",10,StreamSettings{})==RecoveryResult::pending);
  assert(mock.posts==0&&mock.nettests==0&&mock.deletes==0&&mock.puts==static_cast<unsigned>(status>=2&&status<=5));
  assert(client.session().recovered&&!std::strcmp(client.session().id,"fixture-id"));
  assert(client.session().settings.width==1280&&client.session().settings.height==720&&client.session().settings.fps==60&&client.session().settings.bitrate_kbps==12000);
  assert(client.view().state==CloudState::queued&&!*client.session().signaling);
  assert(!mock.trusted.empty()&&mock.trusted.back()=="https://203.0.113.50/");
  assert(mock.urls.back().starts_with("https://203.0.113.50/v2/session/fixture-id"));
  if(mock.puts) {
   auto* root=cJSON_Parse(mock.putBody.c_str());assert(root);
   assert(cJSON_GetObjectItemCaseSensitive(root,"action")->valueint==2);
   assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(root,"data")->valuestring,"RESUME"));
   const auto* data=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
   assert(cJSON_GetObjectItemCaseSensitive(data,"appLaunchMode")->valueint==1&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data,"enablePersistingInGameSettings")));
   assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(data,"deviceHashId")->valuestring,"fixture-device"));
   for(const char* key:{"clientRequestMonitorSettings","requestedStreamingFeatures","codec","maxBitrateKbps","requestedAudioFormat","cmsId"})assert(!cJSON_GetObjectItemCaseSensitive(data,key));
   cJSON_Delete(root);
  }
  client.launch("fixture-jwt","fixture-device",10);assert(mock.posts==0);
  mock.lookup=nullptr;client.tick("fixture-jwt","fixture-device",10);assert(client.view().state==CloudState::queued);
  client.tick("fixture-jwt","fixture-device",11);assert(client.view().state==CloudState::ready);
  assert(client.stop("fixture-jwt","fixture-device")&&!client.session().recovered);
  assert(mock.deleteUrl=="https://203.0.113.50/v2/session/fixture-id");
 }
 {
  Mock mock;const auto seat=existingSeat(),listed=remoteList(seat);auto* root=cJSON_Parse(seat.c_str());cJSON_DeleteItemFromObjectCaseSensitive(root,"sessionControlInfo");char* encoded=cJSON_PrintUnformatted(root);const auto lookup=envelope(encoded);cJSON_free(encoded);cJSON_Delete(root);
  mock.remote=listed.c_str();mock.lookup=lookup.c_str();mock.seatByUrl["https://203.0.113.50/v2/session/fixture-id"]=R"({"requestStatus":{"statusCode":4}})";
  Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==RecoveryResult::pending);
  assert(mock.urls.back().starts_with("https://test.geforcenow.com/v2/session/fixture-id?")&&mock.puts==1);assert(client.stop("fixture-jwt","fixture-device"));
  mock.seatStatuses["https://203.0.113.50/v2/session/fixture-id"]=401;
  assert(client.recover("fixture-jwt","fixture-device",2,StreamSettings{})==RecoveryResult::failed&&mock.puts==1);
  assert(mock.urls.back()=="https://203.0.113.50/v2/session/fixture-id");
 }
 {
  Mock mock;Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==RecoveryResult::none&&mock.posts==0&&mock.lists==1);
  mock.remote="{}";
  assert(client.recover("fixture-jwt","fixture-device",2,StreamSettings{})==RecoveryResult::failed);
  client.dismissLaunchError();client.launch("fixture-jwt","fixture-device",3);assert(mock.posts==0);
  mock.remote=R"({"requestStatus":{"statusCode":1},"sessions":[]})";
  assert(client.recover("fixture-jwt","fixture-device",4,StreamSettings{})==RecoveryResult::none);
  client.launch("fixture-jwt","fixture-device",5);assert(mock.posts==1);assert(client.stop("fixture-jwt","fixture-device"));
 }
 {
  Mock mock;Cloud client(request,&mock,trust);client.load("fixture-jwt","fixture-device");
  mock.serverInfo=R"({"requestStatus":{"statusCode":1},"metaData":[{"key":"local-region","value":"west"},{"key":"gfn-regions","value":"west, east"},{"key":"west","value":"https://west.geforcenow.com/"},{"key":"east","value":"https://east.geforcenow.com/"}]})";
  const auto seat=existingSeat(),listed=remoteList(seat),lookup=envelope(seat);
  mock.listsByUrl["https://east.geforcenow.com/v2/session"]=listed.c_str();mock.lookup=lookup.c_str();
  mock.listStatuses["https://west.geforcenow.com/v2/session"]=503;
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==RecoveryResult::pending);
  assert(mock.lists==3&&mock.puts==1&&mock.posts==0);assert(client.stop("fixture-jwt","fixture-device"));
  mock.listsByUrl.clear();mock.lookup=nullptr;
  assert(client.recover("fixture-jwt","fixture-device",2,StreamSettings{})==RecoveryResult::failed&&mock.posts==0);
 }
 {
  Mock mock;const auto seat=existingSeat(),listed=remoteList(seat+","+seat);mock.remote=listed.c_str();
  const auto lookup=envelope(seat);mock.lookup=lookup.c_str();Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==RecoveryResult::pending&&mock.puts==1);
  assert(client.stop("fixture-jwt","fixture-device"));
  std::string other=seat;other.replace(other.find("fixture-id"),10,"different-id");const auto multiple=remoteList(seat+","+other);mock.remote=multiple.c_str();
  assert(client.recover("fixture-jwt","fixture-device",2,StreamSettings{})==RecoveryResult::conflict&&mock.deletes==1&&mock.puts==1);
  client.dismissLaunchError();client.launch("fixture-jwt","fixture-device",3);assert(mock.posts==0);
 }
 for(const char* replacement:{"native","profile","dimensions","audio"}) {
  Mock mock;auto* root=cJSON_Parse(existingSeat().c_str());auto* data=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");auto* features=cJSON_GetObjectItemCaseSensitive(data,"requestedStreamingFeatures");
  if(!std::strcmp(replacement,"native"))cJSON_ReplaceItemInObjectCaseSensitive(data,"secureRTSPSupported",cJSON_CreateBool(true));
  if(!std::strcmp(replacement,"profile"))cJSON_ReplaceItemInObjectCaseSensitive(features,"codec",cJSON_CreateNumber(3));
  if(!std::strcmp(replacement,"audio"))cJSON_ReplaceItemInObjectCaseSensitive(features,"audioChannelCount",cJSON_CreateNumber(8));
  if(!std::strcmp(replacement,"dimensions"))cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(data,"clientRequestMonitorSettings"),0),"widthInPixels",cJSON_CreateNumber(7680));
  char* encoded=cJSON_PrintUnformatted(root);const std::string seat=encoded; cJSON_free(encoded);cJSON_Delete(root);
  const auto listed=remoteList(seat),lookup=envelope(seat);mock.remote=listed.c_str();mock.lookup=lookup.c_str();Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==RecoveryResult::failed);
  assert(client.session().recovered&&*client.session().id&&mock.puts==0&&mock.posts==0&&mock.deletes==0);
  assert(client.stop("fixture-jwt","fixture-device")&&mock.deletes==1);
 }
 for(long http:{200L,409L,401L,403L}) {
  Mock mock;const auto seat=existingSeat(),listed=remoteList(seat),lookup=envelope(seat);mock.remote=listed.c_str();mock.lookup=lookup.c_str();
  mock.putStatus=http;mock.putResponse=R"({"requestStatus":{"statusCode":"34","statusDescription":"SESSION_NOT_PAUSED"}})";
  Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==(http==401||http==403?RecoveryResult::failed:RecoveryResult::pending));
  assert(mock.posts==0&&mock.deletes==0&&client.session().recovered);assert(client.stop("fixture-jwt","fixture-device"));
 }
 {
  Mock mock;const auto seat=existingSeat(6),listed=remoteList(seat),lookup=envelope(seat);mock.remote=listed.c_str();mock.lookup=lookup.c_str();Cloud client(request,&mock);client.load("fixture-jwt","fixture-device");
  assert(client.recover("fixture-jwt","fixture-device",1,StreamSettings{})==RecoveryResult::pending);
  mock.lookup=nullptr;client.tick("fixture-jwt","fixture-device",91);
  assert(client.view().state==CloudState::failed&&client.session().recovered&&mock.deletes==0&&mock.posts==0);
  assert(client.stop("fixture-jwt","fixture-device"));
 }
}
static void progressAndCleanupTests() {
 Mock mock;Cloud client(request,&mock,trust);client.load("fixture-jwt","fixture-device");
 for(int step:{0,1,2,3,4,5,6,99,-1}) {
  client.launch("fixture-jwt","fixture-device",1);
  const auto fixture=std::string(R"({"requestStatus":{"statusCode":"1"},"session":{"status":"1","seatSetupInfo":{"seatSetupStep":)")+std::to_string(step)+R"(,"queuePosition":"5"}}})";
  mock.poll=fixture.c_str();client.tick("fixture-jwt","fixture-device",4);
  assert(client.view().state==CloudState::queued&&client.view().queuePosition==5&&client.view().setupStep==(step>=0&&step<=6?step:-1));
  if(step==5)assert(std::strstr(client.view().message,"Cleaning"));else if(step==6)assert(std::strstr(client.view().message,"storage"));else assert(std::strstr(client.view().message,"queue position 5"));
  assert(client.stop("fixture-jwt","fixture-device"));
 }
 for(const char* body:{R"({"queuePosition":"7","seatSetupInfo":{"queuePosition":3}})",R"({"sessionProgress":{"queuePosition":"7"}})",R"({"progressInfo":{"queuePosition":7}})"}) {
  client.launch("fixture-jwt","fixture-device",1);auto* seat=cJSON_Parse(body);cJSON_AddStringToObject(seat,"status","1");char* encoded=cJSON_PrintUnformatted(seat);const auto fixture=envelope(encoded);cJSON_free(encoded);cJSON_Delete(seat);
  mock.poll=fixture.c_str();client.tick("fixture-jwt","fixture-device",4);assert(client.view().queuePosition==7);assert(client.stop("fixture-jwt","fixture-device"));
 }
 for(const char* value:{"0","-5","1.5","true","\"1x\"","2147483648"}) {
  client.launch("fixture-jwt","fixture-device",1);const auto fixture=std::string(R"({"requestStatus":{"statusCode":1},"session":{"status":1,"queuePosition":)")+value+R"(,"seatSetupInfo":{"seatSetupStep":99}}})";
  mock.poll=fixture.c_str();client.tick("fixture-jwt","fixture-device",4);assert(client.view().queuePosition==-1&&client.view().setupStep==-1&&std::strstr(client.view().message,"Preparing"));assert(client.stop("fixture-jwt","fixture-device"));
 }
 mock.poll=nullptr;client.launch("fixture-jwt","fixture-device",1);mock.pollStatus=503;
 client.tick("fixture-jwt","fixture-device",4);assert(client.view().state==CloudState::queued);
 client.tick("fixture-jwt","fixture-device",5);assert(client.view().state==CloudState::queued);
 client.tick("fixture-jwt","fixture-device",6);assert(client.view().state==CloudState::failed&&*client.session().id);
 assert(client.stop("fixture-jwt","fixture-device"));mock.pollStatus=200;
 for(const char* body:{"not json",R"({"requestStatus":{"statusCode":"4"}})"}){client.launch("fixture-jwt","fixture-device",1);mock.stopBody=body;assert(!client.stop("fixture-jwt","fixture-device")&&*client.session().id);mock.stopBody="{}";assert(client.stop("fixture-jwt","fixture-device"));}
 for(bool missing:{false,true}) {
  client.launch("fixture-jwt","fixture-device",1);const auto deletes=mock.deletes;mock.pollStatus=missing?404:200;mock.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":7}})";
  client.tick("fixture-jwt","fixture-device",4);assert(!*client.session().id&&mock.deletes==deletes);client.dismissLaunchError();assert(client.view().state==CloudState::catalog);mock.pollStatus=200;
 }
 mock.poll=R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"foreign-id","status":2}})";client.launch("fixture-jwt","fixture-device",1);client.tick("fixture-jwt","fixture-device",4);assert(client.view().state==CloudState::failed&&!std::strcmp(client.session().id,"fixture-id"));assert(client.stop("fixture-jwt","fixture-device"));
}
int main(){
 requestParityTests();conflictTests();retainedProfileTests();acceptedProfileTests();recoveryTests();progressAndCleanupTests();
 {
  Mock rejectedStop;Cloud client(request,&rejectedStop,trust);client.load("fixture-jwt","fixture-device");
  client.launch("fixture-jwt","fixture-device",1);
  rejectedStop.stopBody=R"({"requestStatus":{"statusCode":4}})";
  assert(!client.stop("fixture-jwt","fixture-device"));
  assert(!std::strcmp(client.session().id,"fixture-id"));
  rejectedStop.stopBody=R"({"requestStatus":{"statusCode":1}})";
  assert(client.stop("fixture-jwt","fixture-device"));
 }
 {
  Mock fresh;Cloud client(request,&fresh);client.load("fixture-jwt","fixture-device");
  assert(client.view().revision==1);
  client.focus(7);assert(client.view().selected==0);
  client.focus(1);assert(client.view().selected==1);client.focus(0);
  client.launch("fixture-jwt","fixture-device",1);
  assert(fresh.posts==1&&client.view().state==CloudState::queued);
  assert(client.view().queuePosition==5&&client.view().setupStep==-1);
  assert(fresh.body.find("\"userAge\":25")!=std::string::npos);
  auto* launchBody=cJSON_Parse(fresh.body.c_str());assert(launchBody);
  auto* launchData=cJSON_GetObjectItemCaseSensitive(launchBody,"sessionRequestData");
  auto* controllers=cJSON_GetObjectItemCaseSensitive(launchData,"availableSupportedControllers");
  assert(cJSON_IsArray(controllers)&&cJSON_GetArraySize(controllers)==1);
  assert(cJSON_GetArrayItem(controllers,0)->valueint==2);
  assert(cJSON_GetObjectItemCaseSensitive(launchData,"remoteControllersBitmap")->valueint==1);
  assert(cJSON_GetObjectItemCaseSensitive(launchData,"appLaunchMode")->valueint==2);
  cJSON_Delete(launchBody);
  assert(client.stop("fixture-jwt","fixture-device"));
 }
 {
  Mock audioMock;Cloud client(request,&audioMock);client.load("fixture-jwt","fixture-device");
  for(auto mode:{audio::Mode::automatic,audio::Mode::stereo,audio::Mode::surround51,audio::Mode::surround71}){
   for(unsigned capacity:{2u,8u}){
    StreamSettings settings;settings.audio_mode=mode;const auto channels=audio::requestedChannels(mode,capacity);
    client.launch("fixture-jwt","fixture-device",1,settings,channels);
    assert(client.session().audioChannels==channels&&client.session().settings.audio_mode==mode);
    auto* root=cJSON_Parse(audioMock.body.c_str());assert(root);
    const auto* request=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
    const auto* features=cJSON_GetObjectItemCaseSensitive(request,"requestedStreamingFeatures");
    assert(cJSON_GetObjectItemCaseSensitive(features,"audioChannelCount")->valueint==static_cast<int>(channels));
    const auto* format=cJSON_GetObjectItemCaseSensitive(request,"requestedAudioFormat");
    assert(channels==2?!format:format->valueint==(channels==8?3:2));
    bool found=false;const auto* metadata=cJSON_GetObjectItemCaseSensitive(request,"metaData");
    for(const auto* item=metadata->child;item;item=item->next){const auto* key=cJSON_GetObjectItemCaseSensitive(item,"key");if(!std::strcmp(key->valuestring,"surroundAudioInfo")){found=true;assert(std::to_string(channels)==cJSON_GetObjectItemCaseSensitive(item,"value")->valuestring);}}
    assert(found);cJSON_Delete(root);client.tick("fixture-jwt","fixture-device",4);assert(client.session().audioChannels==channels);
    assert(client.stop("fixture-jwt","fixture-device")&&client.session().audioChannels==2);
   }
  }
  const auto posts=audioMock.posts;
  for(unsigned bad:{0u,3u,8u}){client.launch("fixture-jwt","fixture-device",10,StreamSettings{},bad);assert(client.view().launchError&&audioMock.posts==posts);client.dismissLaunchError();}
 }
 assert(trustedCloudUrl("https://games.geforce.com/graphql"));
 for(const char* bad:{"http://games.geforce.com/graphql","https://games.geforce.com.evil.test/graphql","https://evil.test/@games.geforce.com/","https://geforce.com@evil.test/","https://games.geforce.com:443/"})assert(!trustedCloudUrl(bad));
 CloudView view;char cursor[128]{};
 assert(!parseCatalog(response(R"({"data":{"apps":{"items":[]}}})"),view,cursor,sizeof(cursor)));
 assert(!parseCatalog(response(R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":""},"items":[]}}})"),view,cursor,sizeof(cursor)));
 Mock m;Cloud c(request,&m);c.load("fixture-jwt","fixture-device","Fixture");assert(c.view().count==2);assert(!std::strcmp(c.view().games[0].store,"XBOX"));
 // Square browses without sending an empty searchQuery, including subsequent pages.
 auto checkCatalog=[&](bool search,const char* expectedCursor){
  auto* root=cJSON_Parse(m.catalogBody.c_str());assert(root);
  const char* query=cJSON_GetObjectItemCaseSensitive(root,"query")->valuestring;
  assert((std::strstr(query,"searchQuery:")!=nullptr)==search);
  auto* vars=cJSON_GetObjectItemCaseSensitive(root,"variables");
  auto* text=cJSON_GetObjectItemCaseSensitive(vars,"searchString");
  if(search){assert(cJSON_IsString(text));assert(!std::strcmp(text->valuestring,"Fixture"));}
  else assert(!text);
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(vars,"cursor")->valuestring,expectedCursor));
  cJSON_Delete(root);
 };
 checkCatalog(true,"");
 m.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"page-2"},"items":[]}}})";
 c.load("fixture-jwt","fixture-device","");assert(c.view().state==CloudState::catalog&&c.view().hasNext);checkCatalog(false,"");
 m.catalog=nullptr;
 c.load("fixture-jwt","fixture-device","",true);assert(c.view().count==2);checkCatalog(false,"page-2");
 m.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"search-page-2"},"items":[]}}})";
 c.load("fixture-jwt","fixture-device","Fixture");checkCatalog(true,"");
 m.catalog=nullptr;
 c.load("fixture-jwt","fixture-device","",true);assert(c.view().count==2);checkCatalog(true,"search-page-2");
 c.select(1);c.launch("fixture-jwt","fixture-device",10);assert(m.posts==1&&c.view().state==CloudState::queued);assert(m.body.find("\"appId\":43")!=std::string::npos&&m.body.find("\"cmsId\"")==std::string::npos);assert(m.body.find("\"bitDepth\":0")!=std::string::npos);
 assert(m.body.find("\"userAge\":25")!=std::string::npos);assert(m.nettests==1);assert(m.body.find("\"networkTestSessionId\":\"net-fixture\"")!=std::string::npos);
 c.launch("fixture-jwt","fixture-device",11);assert(m.posts==1);
 c.tick("fixture-jwt","fixture-device",12);assert(c.view().state==CloudState::queued);c.tick("fixture-jwt","fixture-device",13);assert(c.view().state==CloudState::ready);
 assert(c.view().queuePosition==-1&&c.view().setupStep==-1);
 const unsigned pageRevision=c.view().revision;assert(pageRevision==5);
 assert(c.stop("fixture-jwt","fixture-device")&&m.deletes==1);assert(c.view().queuePosition==-1);assert(!*c.session().id);assert(c.view().state==CloudState::catalog);
 {
  Mock transition;
  transition.created=R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":1,"sessionControlInfo":{"ip":"control.geforcenow.com","port":443}}})";
  transition.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":2,"sessionControlInfo":{"ip":"control.geforcenow.com","port":443},"connectionInfo":[{"usage":14,"ip":"stream.geforcenow.com","resourcePath":"/nvst/","port":48322}]}})";
  Cloud client(request,&transition);client.load("fixture-jwt","fixture-device");client.launch("fixture-jwt","fixture-device",20);
  assert(client.view().state==CloudState::queued);
  assert(client.session().signalingSource==SignalingSource::sessionControl);
  client.tick("fixture-jwt","fixture-device",23);
  assert(client.view().state==CloudState::ready);
  assert(!std::strcmp(client.session().signaling,"wss://stream.geforcenow.com:443/nvst/"));
  assert(client.session().signalingSource==SignalingSource::streamConnection);
  assert(!std::strcmp(client.session().mediaIp,"stream.geforcenow.com"));
  assert(client.session().mediaPort==48322);
  assert(client.stop("fixture-jwt","fixture-device"));
  transition.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":2}})";
  client.launch("fixture-jwt","fixture-device",30);client.tick("fixture-jwt","fixture-device",33);
  assert(client.view().state==CloudState::queued);
  assert(!*client.session().signaling&&!*client.session().mediaIp&&client.session().mediaPort==0);
  assert(client.session().signalingSource==SignalingSource::none);
  assert(client.stop("fixture-jwt","fixture-device"));
  transition.created=R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":1,"connectionInfo":[{"usage":14,"ip":"old.geforcenow.com","port":48322}]}})";
  transition.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":2,"sessionControlInfo":{"ip":"new.geforcenow.com","port":443}}})";
  client.launch("fixture-jwt","fixture-device",40);client.tick("fixture-jwt","fixture-device",43);
  assert(client.view().state==CloudState::ready);
  assert(!std::strcmp(client.session().signaling,"wss://new.geforcenow.com:443/nvst/"));
  assert(client.session().signalingSource==SignalingSource::sessionControl);
  assert(!std::strcmp(client.session().mediaIp,"new.geforcenow.com")&&client.session().mediaPort==0);
  assert(client.stop("fixture-jwt","fixture-device"));
 }
 for(const char* address:{
 R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"resourcePath":"rtsps://stream.geforcenow.com:48322","port":48322}]}})",
 R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"resourcePath":"rtsp://stream.geforcenow.com:322"}]}})"}){
 m.poll=address;c.launch("fixture-jwt","fixture-device",20);c.tick("fixture-jwt","fixture-device",23);assert(c.view().state==CloudState::ready);assert(!std::strcmp(c.session().signaling,"wss://stream.geforcenow.com/nvst/"));assert(c.stop("fixture-jwt","fixture-device"));
 }
 struct NetworkCase {const char* response;const char* signaling;const char* ip;int port;};
 for(const auto& fixture:{
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":16,"ip":"control.geforcenow.com","resourcePath":"/control/","port":443},{"usage":14,"ip":"stream.geforcenow.com","resourcePath":"/nvst/","port":48322}]}})","wss://stream.geforcenow.com:443/nvst/","stream.geforcenow.com",48322},
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"ip":"203.0.113.16","resourcePath":"/nvst/","port":48010}]}})","wss://203.0.113.16:443/nvst/","203.0.113.16",48010},
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"resourcePath":"wss://stream.geforcenow.com:8443/nvst/","port":48322}]}})","wss://stream.geforcenow.com:8443/nvst/","stream.geforcenow.com",48322},
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"sessionControlInfo":{"ip":"control.geforcenow.com","port":443}}})","wss://control.geforcenow.com:443/nvst/","control.geforcenow.com",0},
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"resourcePath":"rtsps://203.0.113.10:48322"}]}})","wss://203.0.113.10/nvst/","203.0.113.10",48322},
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"ip":"203.0.113.10","port":443},{"usage":14,"ip":["203.0.113.11"],"port":48322}]}})","wss://203.0.113.10:443/nvst/","203.0.113.11",48322},
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"signalingUrl":"wss://explicit.geforcenow.com/nvst/","connectionInfo":[{"usage":2,"resourcePath":"udp://203.0.113.12:48010"},{"usage":17,"ip":"203.0.113.13","port":48011},{"usage":14,"ip":"203.0.113.14","port":48322}]}})","wss://explicit.geforcenow.com/nvst/","203.0.113.12",48010},
  NetworkCase{R"({"requestStatus":{"statusCode":1},"session":{"status":2,"serverIp":"203.0.113.15","connectionInfo":[{"usage":14,"resourcePath":"/nvst/","port":48322}]}})","wss://203.0.113.15:443/nvst/","203.0.113.15",48322}
 }){
  m.poll=fixture.response;c.launch("fixture-jwt","fixture-device",20);c.tick("fixture-jwt","fixture-device",23);
  assert(c.view().state==CloudState::ready);
  assert(!std::strcmp(c.session().signaling,fixture.signaling));
  assert(!std::strcmp(c.session().mediaIp,fixture.ip));
  assert(c.session().mediaPort==fixture.port);
  assert(c.stop("fixture-jwt","fixture-device"));
 }
 m.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":2}})";c.launch("fixture-jwt","fixture-device",30);c.tick("fixture-jwt","fixture-device",33);assert(std::strstr(c.view().message,"no supported streaming address"));c.stop("fixture-jwt","fixture-device");
 m.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":1,"seatSetupInfo":{"seatSetupStep":3}}})";c.launch("fixture-jwt","fixture-device",40);c.tick("fixture-jwt","fixture-device",43);assert(std::strstr(c.view().message,"Configuring"));assert(c.view().setupStep==3&&c.view().queuePosition==-1);c.stop("fixture-jwt","fixture-device");
 m.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":6}})";
 c.launch("fixture-jwt","fixture-device",40);c.tick("fixture-jwt","fixture-device",43);
 assert(c.view().state==CloudState::queued);assert(std::strstr(c.view().message,"resuming"));
 m.poll=nullptr;c.tick("fixture-jwt","fixture-device",46);assert(c.view().state==CloudState::ready);assert(c.stop("fixture-jwt","fixture-device"));
 m.created=R"({"requestStatus":{"statusCode":41,"statusDescription":"APP_PATCHING_STATUS"},"session":{"sessionId":"patching-fixture","status":1}})";
 c.launch("fixture-jwt","fixture-device",40);
 assert(c.view().state==CloudState::queued);assert(!std::strcmp(c.session().id,"patching-fixture"));assert(std::strstr(c.view().message,"patching"));
 m.created=nullptr;m.poll=R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"patching-fixture","status":2,"signalingUrl":"wss://test.geforcenow.com/nvst/"}})";c.tick("fixture-jwt","fixture-device",43);assert(c.view().state==CloudState::ready);assert(c.stop("fixture-jwt","fixture-device"));m.poll=nullptr;
 for(long status:{404L,410L}){
  c.launch("fixture-jwt","fixture-device",40);m.stopStatus=status;
  assert(c.stop("fixture-jwt","fixture-device"));assert(!*c.session().id);assert(c.view().state==CloudState::catalog);
 }
 for(bool networkError:{false,true}){
  m.stopStatus=200;c.launch("fixture-jwt","fixture-device",40);m.stopStatus=networkError?200:503;m.stopError=networkError;
  assert(!c.stop("fixture-jwt","fixture-device"));assert(!std::strcmp(c.session().id,"fixture-id"));
  const auto posts=m.posts;c.launch("fixture-jwt","fixture-device",41);assert(m.posts==posts);
  m.stopStatus=200;m.stopError=false;assert(c.stop("fixture-jwt","fixture-device"));assert(c.view().state==CloudState::catalog);
 }
 // The allocation request and net-test must match every negotiated profile.
 for(auto profile:{StreamProfile::quality,StreamProfile::smooth,StreamProfile::experimental,StreamProfile::compatibility,StreamProfile::native_hdr120,StreamProfile::native_hdr60,StreamProfile::native_4k120,StreamProfile::native_1080,StreamProfile::native_hdr90,StreamProfile::native_4k90}){
  c.launch("fixture-jwt","fixture-device",50,settingsFor(profile));
  const auto settings=settingsFor(profile);
  auto* root=cJSON_Parse(m.body.c_str());assert(root);
  auto* request=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
  auto* controllers=cJSON_GetObjectItemCaseSensitive(request,"availableSupportedControllers");
  assert(cJSON_IsArray(controllers)&&cJSON_GetArraySize(controllers)==1&&cJSON_GetArrayItem(controllers,0)->valueint==2);
  assert(cJSON_GetObjectItemCaseSensitive(request,"remoteControllersBitmap")->valueint==1);
  assert(cJSON_GetObjectItemCaseSensitive(request,"appLaunchMode")->valueint==2);
  auto* features=cJSON_GetObjectItemCaseSensitive(request,"requestedStreamingFeatures");
  auto* monitor=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(request,"clientRequestMonitorSettings"),0);
  assert(cJSON_GetObjectItemCaseSensitive(features,"maxBitrateKbps")->valueint==settings.bitrate_kbps);
  assert(cJSON_GetObjectItemCaseSensitive(features,"codec")->valueint==(settings.codec()==VideoCodec::hevc?2:1));
  assert(cJSON_GetObjectItemCaseSensitive(features,"bitDepth")->valueint==(settings.tenBit()?1:0));
  assert(cJSON_GetObjectItemCaseSensitive(features,"audioChannelCount")->valueint==2);
  assert((cJSON_GetObjectItemCaseSensitive(features,"dynamicStreamingMode")==nullptr)==(settings.network==NetworkPolicy::fixed));
  assert(cJSON_GetObjectItemCaseSensitive(request,"sdrHdrMode")->valueint==(settings.hdr()?1:0));
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"widthInPixels")->valueint==settings.width);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"heightInPixels")->valueint==settings.height);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"framesPerSecond")->valueint==settings.fps);
  const auto* display=cJSON_GetObjectItemCaseSensitive(monitor,"displayData");
  if(settings.hdr()){
   assert(cJSON_IsObject(display));
   assert(cJSON_GetObjectItemCaseSensitive(display,"desiredContentMaxLuminance")->valueint==1000);
   assert(cJSON_GetObjectItemCaseSensitive(display,"desiredContentMinLuminance")->valueint==0);
   assert(cJSON_GetObjectItemCaseSensitive(display,"desiredContentMaxFrameAverageLuminance")->valueint==400);
  }else assert(cJSON_IsNull(display));
  cJSON_Delete(root);
  root=cJSON_Parse(m.netBody.c_str());assert(root);
  auto* net=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"netTestRequestData"),"netTestProfile");
  assert(cJSON_GetObjectItemCaseSensitive(net,"widthInPixels")->valueint==settings.width);
  assert(cJSON_GetObjectItemCaseSensitive(net,"heightInPixels")->valueint==settings.height);
  assert(cJSON_GetObjectItemCaseSensitive(net,"framesPerSecond")->valueint==settings.fps);
  cJSON_Delete(root);
  assert(c.session().settings==settings);c.stop("fixture-jwt","fixture-device");
 }
 {
  Mock snapshot;Cloud client(request,&snapshot);client.load("fixture-jwt","fixture-device");
  StreamSettings custom{1440,1080,47,32000,VideoMode::hevcMain10SdrHardware,QualityMode::clarity,NetworkPolicy::adaptive};
  const auto expected=custom;snapshot.mutateSettings=&custom;
  client.launch("fixture-jwt","fixture-device",50,custom);
  assert(custom==StreamSettings{}&&client.session().settings==expected);
  auto* root=cJSON_Parse(snapshot.body.c_str());assert(root);
  auto* req=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
  auto* controllers=cJSON_GetObjectItemCaseSensitive(req,"availableSupportedControllers");
  assert(cJSON_IsArray(controllers)&&cJSON_GetArraySize(controllers)==1&&cJSON_GetArrayItem(controllers,0)->valueint==2);
  assert(cJSON_GetObjectItemCaseSensitive(req,"remoteControllersBitmap")->valueint==1);
  assert(cJSON_GetObjectItemCaseSensitive(req,"appLaunchMode")->valueint==2);
  auto* features=cJSON_GetObjectItemCaseSensitive(req,"requestedStreamingFeatures");
  auto* monitor=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(req,"clientRequestMonitorSettings"),0);
  assert(cJSON_GetObjectItemCaseSensitive(features,"bitDepth")->valueint==1);
  assert(cJSON_GetObjectItemCaseSensitive(features,"codec")->valueint==2);
  assert(cJSON_GetObjectItemCaseSensitive(features,"dynamicStreamingMode")->valueint==3);
  assert(cJSON_GetObjectItemCaseSensitive(features,"maxBitrateKbps")->valueint==32000);
  assert(cJSON_GetObjectItemCaseSensitive(req,"sdrHdrMode")->valueint==0);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"widthInPixels")->valueint==1440);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"heightInPixels")->valueint==1080);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"framesPerSecond")->valueint==47);cJSON_Delete(root);
  client.tick("fixture-jwt","fixture-device",53);assert(client.session().settings==expected);
  assert(client.stop("fixture-jwt","fixture-device"));
  assert(client.session().settings==StreamSettings{}&&!*client.session().id&&!*client.session().signaling&&!*client.session().mediaIp);
  custom=expected;custom.fps=240;const auto requests=snapshot.nettests;
  client.launch("fixture-jwt","fixture-device",60,custom);
  assert(client.view().launchError&&snapshot.nettests==requests&&!*client.session().id);
 }
 m.reject=true;c.launch("fixture-jwt","fixture-device",20);assert(c.view().state==CloudState::failed);assert(std::strstr(c.view().message,"INTERNAL_ERROR_STATUS"));assert(std::strstr(c.view().message,"unified 123"));
 assert(c.view().launchError&&!*c.session().id);
 c.dismissLaunchError();assert(!c.view().launchError&&c.view().state==CloudState::catalog);
 m.reject=false;
 {
  assert(artworkUrl("https://img.nvidiagrid.net/apps/101611411/ZZ/GAME_BOX_ART_01_1f4d5087.jpg"));
  for(const char* bad:{"http://img.nvidiagrid.net/apps/1/a.jpg","https://img.nvidiagrid.net.evil.test/apps/1/a.jpg","https://img.nvidiagrid.net/apps/../token",
      "https://img.nvidiagrid.net/apps/1/a.jpg?token=1","https://img.nvidiagrid.net/apps/1/a b.jpg","https://img.nvidiagrid.net/apps/","https://evil.test/apps/1.jpg",""})
   assert(!artworkUrl(bad));
  std::string longUrl="https://img.nvidiagrid.net/apps/";longUrl.append(200,'a');assert(!artworkUrl(longUrl.c_str()));
  for(const char* status:{"MANUAL","PLATFORM_SYNC","IN_LIBRARY"})assert(ownedLibraryStatus(status));
  for(const char* status:{"NOT_OWNED","","UNKNOWN","manual"})assert(!ownedLibraryStatus(status));
  assert(!ownedLibraryStatus(nullptr));
  CloudView parsed;char next[128]{};
  const char* mixed=R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[
   {"title":"Owned","images":{"GAME_BOX_ART":"https://img.nvidiagrid.net/apps/1/ZZ/GAME_BOX_ART_01_a.jpg","KEY_ART":"https://img.nvidiagrid.net/apps/1/ZZ/KEY_ART_01_b.jpg","HERO_IMAGE":"https://img.nvidiagrid.net/apps/1/ZZ/HERO_IMAGE_01_c.jpg"},
    "variants":[{"id":"11","appStore":"STEAM","gfn":{"library":{"status":"PLATFORM_SYNC"}}},{"id":"12","appStore":"EPIC","gfn":{"library":{"status":"NOT_OWNED"}}},{"id":"13","appStore":"XBOX"}]},
   {"title":"Fallback art","images":{"GAME_BOX_ART":"https://evil.test/a.jpg","KEY_ART":"https://img.nvidiagrid.net/apps/2/ZZ/KEY_ART_01_d.jpg","TV_BANNER":"https://img.nvidiagrid.net/apps/2/ZZ/TV_BANNER_01_e.jpg"},
    "variants":[{"id":"21","appStore":"GOG","gfn":{"library":{"status":"MANUAL"}}}]},
   {"title":"No art","variants":[{"id":"31","appStore":"STEAM","gfn":{"library":{"status":"IN_LIBRARY"}}}]}]}}})";
  assert(parseCatalog(response(mixed),parsed,next,sizeof(next),true));
  assert(parsed.count==3);
  assert(!std::strcmp(parsed.games[0].id,"11")&&parsed.games[0].owned);
  assert(!std::strcmp(parsed.games[0].art,"https://img.nvidiagrid.net/apps/1/ZZ/GAME_BOX_ART_01_a.jpg"));
  assert(!std::strcmp(parsed.games[0].hero,"https://img.nvidiagrid.net/apps/1/ZZ/HERO_IMAGE_01_c.jpg"));
  assert(!std::strcmp(parsed.games[1].id,"21")&&!std::strcmp(parsed.games[1].art,"https://img.nvidiagrid.net/apps/2/ZZ/KEY_ART_01_d.jpg"));
  assert(!std::strcmp(parsed.games[1].hero,"https://img.nvidiagrid.net/apps/2/ZZ/TV_BANNER_01_e.jpg"));
  assert(!std::strcmp(parsed.games[2].id,"31")&&!*parsed.games[2].art&&!*parsed.games[2].hero);
  next[0]=0;
  assert(parseCatalog(response(mixed),parsed,next,sizeof(next),false));
  assert(parsed.count==5&&parsed.games[0].owned&&!parsed.games[1].owned&&!parsed.games[2].owned&&parsed.games[3].owned);
  const char* empty=R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Unowned","variants":[{"id":"41","appStore":"STEAM","gfn":{"library":{"status":"NOT_OWNED"}}}]}]}}})";
  next[0]=0;assert(parseCatalog(response(empty),parsed,next,sizeof(next),true));assert(parsed.count==0&&std::strstr(parsed.message,"library"));
 }
 {
  Mock pages;Cloud client(request,&pages);
  pages.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"lib-2"},"items":[{"title":"Library One","variants":[{"id":"51","appStore":"STEAM","gfn":{"library":{"status":"MANUAL"}}},{"id":"52","appStore":"EPIC","gfn":{"library":{"status":"NOT_OWNED"}}}]}]}}})";
  client.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device");
  assert(client.library().state==CloudState::catalog&&client.library().count==1&&client.library().hasNext&&client.library().page==0);
  assert(client.view().state==CloudState::idle&&client.view().count==0);
  auto* root=cJSON_Parse(pages.catalogBody.c_str());assert(root);
  assert(std::strstr(cJSON_GetObjectItemCaseSensitive(root,"query")->valuestring,"variants.gfn.library.lastPlayedDate:DESC,computedValues.libraryAddedDate:DESC,sortName:ASC"));
  assert(std::strstr(cJSON_GetObjectItemCaseSensitive(root,"query")->valuestring,"images{GAME_BOX_ART KEY_ART HERO_IMAGE TV_BANNER}"));
  auto* vars=cJSON_GetObjectItemCaseSensitive(root,"variables");
  auto* status=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(vars,"filters"),"variants"),"gfn"),"library"),"status");
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(status,"notEquals")->valuestring,"NOT_OWNED"));
  assert(cJSON_GetObjectItemCaseSensitive(vars,"fetchCount")->valueint==static_cast<int>(catalogPageSize));
  assert(!cJSON_GetObjectItemCaseSensitive(vars,"searchString"));
  cJSON_Delete(root);
  const unsigned firstRevision=client.library().revision;
  pages.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Library Two","variants":[{"id":"61","appStore":"XBOX","gfn":{"library":{"status":"IN_LIBRARY"}}}]}]}}})";
  client.loadPage(CatalogSource::library,1,"fixture-jwt","fixture-device");
  root=cJSON_Parse(pages.catalogBody.c_str());assert(root);
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"variables"),"cursor")->valuestring,"lib-2"));
  cJSON_Delete(root);
  assert(client.library().page==1&&!client.library().hasNext&&!std::strcmp(client.library().games[0].id,"61")&&client.library().revision==firstRevision+1);
  client.loadPage(CatalogSource::library,2,"fixture-jwt","fixture-device");
  assert(client.library().state==CloudState::failed&&client.library().count==1&&!std::strcmp(client.library().games[0].id,"61"));
  pages.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"lib-2"},"items":[{"title":"Library One","variants":[{"id":"51","appStore":"STEAM","gfn":{"library":{"status":"MANUAL"}}}]}]}}})";
  client.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device");
  root=cJSON_Parse(pages.catalogBody.c_str());assert(root);
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"variables"),"cursor")->valuestring,""));
  cJSON_Delete(root);
  assert(client.library().page==0&&!std::strcmp(client.library().games[0].id,"51"));
  pages.catalog=R"({"errors":[{"message":"denied"}]})";
  client.loadPage(CatalogSource::library,1,"fixture-jwt","fixture-device");
  assert(client.library().state==CloudState::failed&&client.library().count==1&&!std::strcmp(client.library().games[0].id,"51"));
  assert(std::strstr(client.library().message,"Library request failed"));
  assert(client.view().state==CloudState::idle&&!client.view().count);
  client.launchEntry(CatalogSource::library,0,"fixture-jwt","fixture-device",50,settingsFor(StreamProfile::quality));
  assert(pages.posts==0&&!*client.session().id);
  pages.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"lib-2"},"items":[{"title":"Library One","variants":[{"id":"51","appStore":"STEAM","gfn":{"library":{"status":"MANUAL"}}}]}]}}})";
  client.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device");
  pages.catalog=nullptr;
  client.load("fixture-jwt","fixture-device");
  assert(client.view().count==2&&client.library().count==1&&client.library().state==CloudState::catalog);
  client.launchEntry(CatalogSource::library,0,"fixture-jwt","fixture-device",50,settingsFor(StreamProfile::quality));
  assert(pages.body.find("\"appId\":51")!=std::string::npos);
  assert(!std::strcmp(client.view().current.id,"51")&&!std::strcmp(client.view().current.title,"Library One"));
  client.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device");
  assert(client.library().state==CloudState::catalog&&!std::strcmp(client.library().games[0].id,"51"));
  assert(client.stop("fixture-jwt","fixture-device"));
  client.launchEntry(CatalogSource::library,5,"fixture-jwt","fixture-device",60,settingsFor(StreamProfile::quality));
  assert(pages.posts==1&&!*client.session().id);
 }
 {
  Mock filtered;Cloud client(request,&filtered);
  filtered.byCursor[""]=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"c1"},"items":[{"title":"Unknown","variants":[{"id":"70","appStore":"STEAM"}]},{"title":"Not owned","variants":[{"id":"71","appStore":"EPIC","gfn":{"library":{"status":"NOT_OWNED"}}}]}]}}})";
  filtered.byCursor["c1"]=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"c2"},"items":[{"title":"Owned later","variants":[{"id":"72","appStore":"XBOX","gfn":{"library":{"status":"PLATFORM_SYNC"}}}]}]}}})";
  filtered.byCursor["c2"]=R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Last owned","variants":[{"id":"73","appStore":"GOG","gfn":{"library":{"status":"MANUAL"}}}]}]}}})";
  client.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device");
  assert(client.library().state==CloudState::catalog&&client.library().count==1&&client.library().page==1);
  assert(!std::strcmp(client.library().games[0].id,"72")&&client.library().hasNext&&filtered.graphql==2);
  client.loadPage(CatalogSource::library,2,"fixture-jwt","fixture-device");
  assert(client.library().page==2&&!std::strcmp(client.library().games[0].id,"73")&&!client.library().hasNext);
  client.loadPage(CatalogSource::library,1,"fixture-jwt","fixture-device",-1);
  assert(client.library().page==1&&!std::strcmp(client.library().games[0].id,"72"));
  filtered.graphql=0;
  client.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device",-1);
  assert(client.library().page==1&&!std::strcmp(client.library().games[0].id,"72")&&filtered.graphql==2);
  Mock barren;Cloud scanning(request,&barren);
  std::string pages[emptyPageSkips+3];
  for(unsigned i=0;i<emptyPageSkips+3;++i) {
   pages[i]=std::string(R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"e)")+std::to_string(i+1)+R"("},"items":[{"title":"Nope","variants":[{"id":"80","appStore":"STEAM","gfn":{"library":{"status":"NOT_OWNED"}}}]}]}}})";
   barren.byCursor[i?"e"+std::to_string(i):std::string()]=pages[i].c_str();
  }
  scanning.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device");
  assert(scanning.library().state==CloudState::catalog&&!scanning.library().count&&scanning.library().hasNext);
  assert(scanning.library().page==emptyPageSkips&&barren.graphql==emptyPageSkips+1);
  scanning.loadPage(CatalogSource::library,scanning.library().page+1,"fixture-jwt","fixture-device");
  assert(scanning.library().page>emptyPageSkips);
  Mock lonely;Cloud none(request,&lonely);
  lonely.byCursor[""]=R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Nope","variants":[{"id":"81","appStore":"STEAM"}]}]}}})";
  none.loadPage(CatalogSource::library,0,"fixture-jwt","fixture-device");
  assert(none.library().state==CloudState::catalog&&!none.library().count&&!none.library().hasNext&&lonely.graphql==1);
 }
 {
  Mock queries;Cloud client(request,&queries);
  queries.byCursor[""]=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"a1"},"items":[{"title":"Query A","variants":[{"id":"90","appStore":"STEAM"}]}]}}})";
  queries.byCursor["a1"]=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"a2"},"items":[{"title":"Query A page 2","variants":[{"id":"91","appStore":"STEAM"}]}]}}})";
  client.load("fixture-jwt","fixture-device","alpha");
  client.load("fixture-jwt","fixture-device","",true);
  assert(client.view().page==1&&client.view().hasNext);
  client.loadPage(CatalogSource::browse,0,"fixture-jwt","fixture-device");
  assert(client.view().page==0&&client.view().hasNext);
  queries.graphql=0;
  client.loadPage(CatalogSource::browse,2,"fixture-jwt","fixture-device");
  assert(client.view().state==CloudState::failed&&queries.graphql==0);
  queries.byCursor.erase("");
  client.load("fixture-jwt","fixture-device","beta");
  assert(client.view().state==CloudState::failed&&client.view().count==0&&!client.view().hasNext&&client.view().page==0&&queries.graphql==1);
  client.load("fixture-jwt","fixture-device","",true);
  client.loadPage(CatalogSource::browse,1,"fixture-jwt","fixture-device");
  assert(queries.graphql==1&&client.view().count==0);
  auto* root=cJSON_Parse(queries.catalogBody.c_str());assert(root);
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"variables"),"searchString")->valuestring,"beta"));
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"variables"),"cursor")->valuestring,""));
  cJSON_Delete(root);
 }
 puts("Catalog and cloud lifecycle regressions passed");
}
