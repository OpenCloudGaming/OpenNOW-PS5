#include "cloud.hpp"
#include "vendor/cJSON.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
using namespace opennow;
static Response response(const char* s){return {200,const_cast<char*>(s),std::strlen(s),nullptr};}
struct Mock {unsigned posts=0,deletes=0,nettests=0;bool reject=false;const char* poll=nullptr;std::string body,netBody,catalogBody;const char* catalog=nullptr;const char* created=nullptr;long stopStatus=200;bool stopError=false;};
static Response request(void* p,const char* method,const char* url,const char* body,const char*,const char*) {
 auto& m=*static_cast<Mock*>(p);
 if(std::strstr(url,"serviceUrls"))return response(R"({"gfnServiceInfo":{"gfnServiceEndpoints":[{"idpId":"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg","streamingServiceUrl":"https://test.geforcenow.com/"}]}})");
 if(std::strstr(url,"serverInfo"))return response(R"({"requestStatus":{"serverId":"GFN-PC"}})");
 if(std::strstr(url,"graphql")){m.catalogBody=body;if(m.catalog)return response(m.catalog);return response(R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Fixture Game","variants":[{"id":"42","appStore":"XBOX"},{"id":"43","appStore":"STEAM"}]}]}}})");}
 if(std::strstr(url,"nettestsession")){++m.nettests;m.netBody=body;return response(R"({"requestStatus":{"statusCode":1},"netTestSession":{"sessionId":"net-fixture"}})");}
 if(!std::strcmp(method,"POST")){if(m.reject){auto r=response(R"({"requestStatus":{"statusCode":4,"statusDescription":"INTERNAL_ERROR_STATUS","unifiedErrorCode":123}})");r.status=500;return r;}++m.posts;m.body=body;if(m.created)return response(m.created);return response(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":0,"queuePosition":5}})");}
 if(!std::strcmp(method,"DELETE")){++m.deletes;auto r=response("{}");r.status=m.stopStatus;if(m.stopError)r.error="fixture network failure";return r;}
 if(m.poll)return response(m.poll);
 return response(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":2,"signalingUrl":"wss://test.geforcenow.com/nvst/"}})");
}
int main(){
 {
  Mock fresh;Cloud client(request,&fresh);client.load("fixture-jwt","fixture-device");
  assert(client.view().revision==1);
  client.focus(7);assert(client.view().selected==0);
  client.focus(1);assert(client.view().selected==1);client.focus(0);
  client.launch("fixture-jwt","fixture-device",1);
  assert(fresh.posts==1&&client.view().state==CloudState::queued);
  assert(client.view().queuePosition==5&&client.view().setupStep==-1);
  assert(fresh.body.find("\"userAge\":25")!=std::string::npos);
  assert(client.stop("fixture-jwt","fixture-device"));
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
 c.select(1);c.launch("fixture-jwt","fixture-device",10);assert(m.posts==1&&c.view().state==CloudState::queued);assert(m.body.find("\"cmsId\":\"43\"")!=std::string::npos);assert(m.body.find("\"bitDepth\":0")!=std::string::npos);
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
 m.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":1,"seatSetupInfo":{"seatSetupStep":3}}})";c.launch("fixture-jwt","fixture-device",40);c.tick("fixture-jwt","fixture-device",43);assert(std::strstr(c.view().message,"setup step 3"));assert(c.view().setupStep==3&&c.view().queuePosition==-1);c.stop("fixture-jwt","fixture-device");
 m.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":6}})";
 c.launch("fixture-jwt","fixture-device",40);c.tick("fixture-jwt","fixture-device",43);
 assert(c.view().state==CloudState::queued);assert(std::strstr(c.view().message,"resuming"));
 m.poll=nullptr;c.tick("fixture-jwt","fixture-device",46);assert(c.view().state==CloudState::ready);assert(c.stop("fixture-jwt","fixture-device"));
 m.created=R"({"requestStatus":{"statusCode":41,"statusDescription":"APP_PATCHING_STATUS"},"session":{"sessionId":"patching-fixture","status":1}})";
 c.launch("fixture-jwt","fixture-device",40);
 assert(c.view().state==CloudState::queued);assert(!std::strcmp(c.session().id,"patching-fixture"));assert(std::strstr(c.view().message,"patching"));
 m.created=nullptr;c.tick("fixture-jwt","fixture-device",43);assert(c.view().state==CloudState::ready);assert(c.stop("fixture-jwt","fixture-device"));
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
  c.launch("fixture-jwt","fixture-device",50,profile);
  const auto settings=settingsFor(profile);
  auto* root=cJSON_Parse(m.body.c_str());assert(root);
  auto* request=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
  auto* features=cJSON_GetObjectItemCaseSensitive(request,"requestedStreamingFeatures");
  auto* monitor=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(request,"clientRequestMonitorSettings"),0);
  assert(cJSON_GetObjectItemCaseSensitive(features,"maxBitrateKbps")->valueint==settings.bitrate_kbps);
  assert(cJSON_GetObjectItemCaseSensitive(features,"codec")->valueint==(settings.codec==VideoCodec::hevc?2:1));
  assert(cJSON_GetObjectItemCaseSensitive(features,"bitDepth")->valueint==(settings.hdr?1:0));
  assert(cJSON_GetObjectItemCaseSensitive(features,"audioChannelCount")->valueint==2);
  assert((cJSON_GetObjectItemCaseSensitive(features,"dynamicStreamingMode")==nullptr)==settings.hardware);
  assert(cJSON_GetObjectItemCaseSensitive(request,"sdrHdrMode")->valueint==(settings.hdr?1:0));
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"widthInPixels")->valueint==settings.width);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"heightInPixels")->valueint==settings.height);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"framesPerSecond")->valueint==settings.fps);
  const auto* display=cJSON_GetObjectItemCaseSensitive(monitor,"displayData");
  if(settings.hdr){
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
  assert(c.session().profile==profile);c.stop("fixture-jwt","fixture-device");
 }
 m.reject=true;c.launch("fixture-jwt","fixture-device",20);assert(c.view().state==CloudState::failed);assert(std::strstr(c.view().message,"INTERNAL_ERROR_STATUS"));assert(std::strstr(c.view().message,"unified 123"));
 puts("Catalog and cloud lifecycle regressions passed");
}
