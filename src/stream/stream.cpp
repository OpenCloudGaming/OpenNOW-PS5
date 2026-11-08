// SPDX-License-Identifier: GPL-3.0-or-later
// NVIDIA signaling and controller wire formats adapted from OpenNOW-Switch (MIT).
#include "stream.hpp"
#include "../version.hpp"
extern "C" {
#include "peer.h"
}
#include "../vendor/cJSON.h"
#include "../random.hpp"
#include "../app_storage.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iterator>
extern "C" unsigned long long sceKernelGetProcessTime();
extern "C" void opennow_media_note(const char*);
static std::atomic_bool entropyFailed{false};
namespace opennow {
namespace {
void mediaSdp(const char* stage,const std::string& s){
 opennow_media_note(stage);
 std::size_t start=0;
 while(start<s.size()){
  auto end=s.find('\n',start);if(end==std::string::npos)end=s.size();
  auto line=s.substr(start,end-start);if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(line.rfind("m=",0)==0||line.rfind("a=rtpmap:",0)==0||line=="a=recvonly"||line=="a=sendonly"||line=="a=inactive"||line=="a=sendrecv")opennow_media_note(line.c_str());
  start=end+1;
 }
}
const cJSON* get(const cJSON* j,const char* k){return cJSON_GetObjectItemCaseSensitive(j,k);}
const char* text(const cJSON* j,const char* k){auto* p=get(j,k);return cJSON_IsString(p)?p->valuestring:"";}
bool integer(const cJSON* value){return cJSON_IsNumber(value)&&value->valueint>=0&&value->valuedouble==value->valueint;}
std::string queryComponent(const std::string& value){
 std::string encoded;
 constexpr char hex[]="0123456789ABCDEF";
 for(unsigned char c:value){
  if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')encoded+=c;
  else{encoded+='%';encoded+=hex[c>>4];encoded+=hex[c&15];}
 }
 return encoded;
}
constexpr std::uint16_t inputSid=0,statsSid=2,partialInputSid=4,controlSid=6;
struct ModifierKey{std::uint8_t bit;std::uint16_t vk,scan;};
constexpr ModifierKey modifierKeys[]={{modifierShift,0xa0,0x2a},{modifierCtrl,0x11,0x1d},{modifierAlt,0x12,0x38}};
}
bool Stream::start(const Session& s,const char* device) {
 if(validateSettings(s.settings)!=SettingsError::none){stop();fail("Invalid stream settings");return false;}
 if((s.audioChannels!=2&&s.audioChannels!=6&&s.audioChannels!=8)||audio::requestedChannels(s.settings.audio_mode,s.audioChannels)!=s.audioChannels){stop();fail("Invalid audio channel request");return false;}
 stop();opennow_media_note("START " OPENNOW_VERSION);entropyFailed=false;session_=s;settings_=s.settings;name_=std::string("opennow-")+device;peerId_=ack_=0;remoteId_=1;answerSent_=inputReady_=false;nextHeartbeat_=started_=lastInput_=0;keyHeld_=false;mouseHeld_=0;keyUpAt_=nextKeyAt_=0;keyframeRequested_=0;candidates_.clear();lastVideoLoss_=0;
 qos_={};nextQos_=0;protocol_=2;channelRequests_=0;nextChannelAttempt_=0;handshakeReceived_=false;handshakeAt_=0;
 decodedFrames_=presentedFrames_=0;lastDecodedAt_=recoveryRequestedAt_=0;videoSeen_=false;sessionTimer_={};serverTermination_={};rumble_.reset();
 capture_.arm(OPENNOW_STORAGE_ROOT,settings_.codec()==VideoCodec::hevc,sceKernelGetProcessTime());
 std::string url=s.signaling;
 const auto authorityEnd=url.find_first_of("/?#",6);
 const auto authority=url.substr(std::min<std::size_t>(6,url.size()),authorityEnd==std::string::npos?std::string::npos:authorityEnd-6);
 if(url.rfind("wss://",0)||authority.empty()||authority.find('@')!=std::string::npos||url.find('#')!=std::string::npos||url.find_first_of("\r\n\t ")!=std::string::npos){fail("Invalid secure signaling endpoint");release();return false;}
 if(!media_.start(settings_,s.audioChannels)){fail("Could not initialize video decoder / audio output");release();return false;}
 if(peer_init()!=0){fail("WebRTC runtime initialization failed");release();return false;}runtimeReady_=true;
 PeerConfiguration config{};config.video_codec=settings_.codec()==VideoCodec::hevc?CODEC_HEVC:CODEC_H264;config.audio_codec=CODEC_OPUS;config.datachannel=DATA_CHANNEL_STRING;config.user_data=this;config.onvideopacket=video;config.onaudiopacket=audio;
 config.ice_servers[0].urls="stun:s1.stun.gamestream.nvidia.com:19308";
 peer_connection_set_diagnostics_enabled(0);pc_=peer_connection_create(&config);
 if(!pc_||entropyFailed){fail("Unable to initialize WebRTC");release();return false;}
 PeerVideoRtpStats initial{};peer_connection_get_video_rtp_stats(pc_,&initial);
 if(!initial.assembler_ready){fail("Video assembler initialization failed");release();return false;}
 peer_connection_onicecandidate(pc_,ice);peer_connection_oniceconnectionstatechange(pc_,state);peer_connection_ondatachannel(pc_,dataMessage,dataOpen,dataClose);
 auto q=url.find('?');if(q!=std::string::npos)url.resize(q);while(url.size()&&url.back()=='/')url.pop_back();if(url.size()<8||url.substr(url.size()-8)!="/sign_in")url+="/sign_in";
 url+="?peer_id="+queryComponent(name_)+"&version=2&peer_role=1&pairing_id="+queryComponent(s.id);
 ws_=new WebSocketClient(url);ws_->set_custom_headers({"Origin: https://play.geforcenow.com",std::string("Sec-WebSocket-Protocol: x-nv-sessionid.")+s.id,"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/131.0.0.0 Safari/537.36"});ws_->set_on_message([this](const std::string& m){message(m);});
 const char* route="none";
 switch(s.signalingSource){
 case SignalingSource::none:break;
 case SignalingSource::explicitUrl:route="explicit";break;
 case SignalingSource::streamConnection:route="stream";break;
 case SignalingSource::alternateConnection:route="alternate";break;
 case SignalingSource::sessionControl:route="control";break;
 }
 char diagnostic[192];std::snprintf(diagnostic,sizeof(diagnostic),"SIGNALING route=%s",route);opennow_media_note(diagnostic);
 if(!ws_->connect()){std::snprintf(diagnostic,sizeof(diagnostic),"Signaling: %s [route=%s]",ws_->get_last_error().c_str(),route);opennow_media_note(diagnostic);fail(diagnostic);release();return false;}
 peerInfo();std::snprintf(status_,sizeof(status_),"Waiting for NVIDIA stream offer");return true;
}
void Stream::fail(const char* reason){if(!failed_){failed_=true;inputReady_=false;rumble_.reset();sessionTimer_.active=false;std::snprintf(status_,sizeof(status_),"%s",reason);}}
void Stream::stop(){release();failed_=false;}
void Stream::release() {
 capture_.close();
 if(pc_){
  const auto at=std::max<std::uint64_t>(lastInput_,sceKernelGetProcessTime())+20000;
  if(inputReady_){releaseKey(at);for(std::uint8_t b=1;b<=5;++b)if(mouseHeld_&(1U<<b))sendInput(wire::mouseButton(false,b,protocol_,inputTime(at)));}
  mouseHeld_=0;if(inputReady_)sendInput(wire::gamepad({},protocol_,inputTime(at)),false,false);peer_connection_close(pc_);peer_connection_destroy(pc_);pc_=nullptr;}
 if(ws_){ws_->disconnect();delete ws_;ws_=nullptr;}
 if(runtimeReady_){peer_deinit();runtimeReady_=false;}
 media_.stop();secureErase(&session_,sizeof(session_));session_={};inputReady_=false;answerSent_=false;candidates_.clear();remoteCandidates_.clear();candidateMid_.clear();candidateMLine_=0;previousInput_={};
 handshakeReceived_=false;handshakeAt_=0;channelRequests_=0;rumble_.reset();sessionTimer_.active=false;inputSeen_=false;
 heldModifiers_=0;keyHeld_=false;pendingInputValid_=releasingInputs_=releasingKey_=cursorWanted_=false;pendingInput_={};nextCursorCapture_=0;
}
void Stream::send(cJSON* root){char* value=cJSON_PrintUnformatted(root);if(value&&ws_)ws_->send_message(value);cJSON_free(value);}
void Stream::payload(cJSON* value){if(!ws_)return;char* data=cJSON_PrintUnformatted(value);if(!data)return;auto* root=cJSON_CreateObject();auto* msg=cJSON_AddObjectToObject(root,"peer_msg");cJSON_AddNumberToObject(msg,"from",peerId_);cJSON_AddNumberToObject(msg,"to",remoteId_);cJSON_AddStringToObject(msg,"msg",data);cJSON_AddNumberToObject(root,"ackid",++ack_);send(root);cJSON_Delete(root);cJSON_free(data);}
void Stream::sendCandidates(){
 if(!answerSent_)return;
 for(const auto& line:candidates_){auto* c=cJSON_CreateObject();cJSON_AddStringToObject(c,"candidate",line.c_str());cJSON_AddStringToObject(c,"sdpMid",candidateMid_.c_str());cJSON_AddNumberToObject(c,"sdpMLineIndex",candidateMLine_);payload(c);cJSON_Delete(c);}
 candidates_.clear();
}
void Stream::remoteCandidate(std::string candidate){
 if(failed_||!pc_)return;
 if(candidate.empty()||candidate.size()>4096){fail("Invalid remote ICE candidate");return;}
 candidate=sdp::RewriteGfnMediaCandidate(candidate,session_.mediaIp,session_.mediaPort,session_.signaling);
 if(candidate.rfind("a=",0))candidate="a="+candidate;
 if(!answerSent_){
  if(remoteCandidates_.size()==64){fail("Too many queued ICE candidates");return;}
  remoteCandidates_.push_back(std::move(candidate));return;
 }
 if(peer_connection_add_ice_candidate(pc_,candidate.data())<0)fail("Could not add remote ICE candidate");
}
void Stream::peerInfo(){auto* root=cJSON_CreateObject();cJSON_AddNumberToObject(root,"ackid",++ack_);auto* info=cJSON_AddObjectToObject(root,"peer_info");cJSON_AddStringToObject(info,"browser","Chrome");cJSON_AddStringToObject(info,"browserVersion","131");cJSON_AddBoolToObject(info,"connected",true);cJSON_AddNumberToObject(info,"id",peerId_);cJSON_AddStringToObject(info,"name",name_.c_str());cJSON_AddNumberToObject(info,"peerRole",0);const auto resolution=std::to_string(settings_.width)+"x"+std::to_string(settings_.height);cJSON_AddStringToObject(info,"resolution",resolution.c_str());cJSON_AddNumberToObject(info,"version",2);send(root);cJSON_Delete(root);}
void Stream::message(const std::string& message) {
 if(failed_||!pc_)return;
 if(message.size()>65536){fail("Signaling message exceeds size limit");return;}
 if(message.find('\0')!=std::string::npos){fail("Invalid signaling JSON");return;}
 auto* root=cJSON_ParseWithLengthOpts(message.c_str(),message.size()+1,nullptr,true);
 if(!cJSON_IsObject(root)){cJSON_Delete(root);fail("Invalid signaling JSON");return;}
 auto* info=get(root,"peer_info");auto* id=get(info,"id");
 if(integer(id)&&!std::strcmp(text(info,"name"),name_.c_str()))peerId_=id->valueint;
 auto* ack=get(root,"ackid");
 if(integer(ack)&&(!integer(id)||id->valueint!=peerId_)){auto* a=cJSON_CreateObject();cJSON_AddNumberToObject(a,"ack",ack->valueint);send(a);cJSON_Delete(a);}
 const auto* hb=get(root,"hb");
 if(integer(hb)&&hb->valueint){auto* a=cJSON_CreateObject();cJSON_AddNumberToObject(a,"hb",1);send(a);cJSON_Delete(a);cJSON_Delete(root);return;}
 const auto* error=get(root,"error");
 if(error&&!cJSON_IsNull(error)){fail(!std::strcmp(text(root,"error"),"peerRemoved")?"Signaling: peerRemoved":"Server rejected signaling request");cJSON_Delete(root);return;}
 auto* msg=get(root,"peer_msg");if(cJSON_IsString(get(msg,"msg"))){auto* from=get(msg,"from");if(integer(from))remoteId_=from->valueint;
 const std::string nested=text(msg,"msg");const auto first=nested.find_first_not_of(" \t\r\n"),last=nested.find_last_not_of(" \t\r\n");
 if(first!=std::string::npos&&nested.substr(first,last-first+1)=="BYE"){fail("Signaling: server ended the stream");cJSON_Delete(root);return;}
 auto* data=cJSON_ParseWithLengthOpts(nested.c_str(),nested.size()+1,nullptr,true);
 if(!cJSON_IsObject(data)){cJSON_Delete(data);cJSON_Delete(root);fail("Invalid nested peer message");return;}
 {
  if(!std::strcmp(text(data,"type"),"offer")){
   if(answerSent_){cJSON_Delete(data);cJSON_Delete(root);fail("WebRTC renegotiation requires a fresh session start");return;}
   webrtc::RiInputCapabilities capabilities;
   if(!sdp::ParseRiInputCapabilities(text(data,"sdp"),capabilities)){cJSON_Delete(data);cJSON_Delete(root);fail("Invalid input capabilities");return;}
   std::string offer=sdp::PrepareGfnOfferSdp(text(data,"sdp"),session_.signaling,session_.mediaIp,session_.mediaPort);
   if(offer.size()<60000&&!offer.empty()){
    const auto audioFormat=audio::selectFormat(offer,std::min(session_.audioChannels,media_.audioCapacity()));
    if(!audio::validFormat(audioFormat)){fail("Server did not offer supported audio");}else{
    mediaSdp("OFFER",offer);peer_connection_set_remote_description(pc_,offer.c_str(),SDP_TYPE_OFFER);
    const bool audioReady=peer_connection_set_audio_payload_types(pc_,audioFormat.payload,audioFormat.redPayload)==0&&media_.configureAudio(audioFormat);
    const char* raw=audioReady?peer_connection_create_answer(pc_):nullptr;
    if(!audioReady){fail("Could not configure offered audio");}
    else if(raw&&!entropyFailed){auto answer=sdp::AdaptAnswerSdpToOffer(raw,offer,settings_,&audioFormat);if(answer.empty()){fail("Server did not offer the selected video codec");}else{mediaSdp("ANSWER",answer);auto nvst=webrtc::BuildNvstSdp(answer,settings_,capabilities);
     if(nvst.empty()){cJSON_Delete(data);cJSON_Delete(root);fail("Local answer lacks secure transport credentials");return;}
     auto* a=cJSON_CreateObject();cJSON_AddStringToObject(a,"type","answer");cJSON_AddStringToObject(a,"sdp",answer.c_str());cJSON_AddStringToObject(a,"nvstSdp",nvst.c_str());payload(a);cJSON_Delete(a);answerSent_=true;
     const auto bundle=sdp::ExtractSdpValue(answer,"a=group:BUNDLE ");
     candidateMid_=bundle.empty()?sdp::ExtractSdpValue(answer,"a=mid:"):bundle.substr(0,bundle.find(' '));
     int section=-1;
     for(std::size_t pos=0;pos<answer.size();){auto end=answer.find("\r\n",pos);if(end==std::string::npos)end=answer.size();const auto line=answer.substr(pos,end-pos);if(line.rfind("m=",0)==0)++section;if(line=="a=mid:"+candidateMid_){candidateMLine_=section;break;}pos=end+2;}
     sendCandidates();
     for(auto& candidate:remoteCandidates_)remoteCandidate(std::move(candidate));remoteCandidates_.clear();
     int pairs=0;if(!failed_&&peer_connection_get_ice_candidate_pair_stats(pc_,&pairs,nullptr,nullptr,nullptr,nullptr)==0&&pairs==0){auto manual=sdp::BuildManualMediaCandidate(session_.signaling,session_.mediaIp,session_.mediaPort,100);if(!manual.empty()&&peer_connection_add_ice_candidate(pc_,manual.data())<0)fail("Could not add fallback ICE candidate");}
     if(!failed_)std::snprintf(status_,sizeof(status_),"Negotiating secure media connection");
    }}else{fail(entropyFailed?"Secure entropy failed":"Unable to create WebRTC answer");}
    }
   }else{fail("Invalid server stream offer");}
  }else if(cJSON_IsString(get(data,"candidate")))remoteCandidate(text(data,"candidate"));
  cJSON_Delete(data);
 }}cJSON_Delete(root);
}
void Stream::ice(char* s,void* ctx){auto& self=*static_cast<Stream*>(ctx);if(!self.pc_||self.failed_)return;std::string lines=s?s:"";std::size_t pos=0;while(pos<lines.size()){auto end=lines.find('\n',pos);if(end==std::string::npos)end=lines.size();auto line=lines.substr(pos,end-pos);if(line.size()&&line.back()=='\r')line.pop_back();if(line.rfind("a=candidate:",0)==0){if(self.candidates_.size()==16||line.size()>4096){self.fail("Local ICE candidate exceeds limits");return;}self.candidates_.push_back(line.substr(2));}pos=end+1;}}
void Stream::state(PeerConnectionState s,void* ctx){auto& self=*static_cast<Stream*>(ctx);if(self.failed_||!self.pc_)return;const auto status=std::string("WebRTC: ")+peer_connection_state_to_string(s);if(s==PEER_CONNECTION_FAILED||s==PEER_CONNECTION_CLOSED||s==PEER_CONNECTION_DISCONNECTED)self.fail(status.c_str());else std::snprintf(self.status_,sizeof(self.status_),"%s",status.c_str());}
void Stream::recoverVideoLoss(){
 PeerVideoRtpStats stats{};peer_connection_get_video_rtp_stats(pc_,&stats);
 if(stats.access_units_dropped!=lastVideoLoss_){lastVideoLoss_=stats.access_units_dropped;media_.requireKeyframe();}
}
void Stream::video(const PeerVideoPacket* p,void* ctx){auto& self=*static_cast<Stream*>(ctx);if(!self.pc_||self.failed_)return;self.recoverVideoLoss();if(p){const auto now=sceKernelGetProcessTime();self.capture_.receive(p->data,p->size,now,self.capture_.wantsIdr(now)&&video::Recovery::hasIdr(p->data,p->size,self.settings_.codec()==VideoCodec::hevc));if(!self.media_.video(p->data,p->size)){if(now-self.keyframeRequested_>=250000){peer_connection_request_video_keyframe(self.pc_);self.keyframeRequested_=now;}}}}
void Stream::audio(const PeerAudioPacket* p,void* ctx){auto& self=*static_cast<Stream*>(ctx);if(p&&self.pc_&&!self.failed_)self.media_.audio(p->data,p->size,p->sequence,p->payload_type,p->timestamp);}
void Stream::dataMessage(char* data,std::size_t size,void* ctx,std::uint16_t sid){static_cast<Stream*>(ctx)->data(data,size,sid);}
void Stream::openChannels(){
 if(!pc_||failed_)return;
 struct Channel{std::uint16_t sid;DecpChannelType type;std::uint32_t reliability;const char* label;};
 constexpr Channel channels[]={{inputSid,DATA_CHANNEL_RELIABLE,0,"input_channel_v1"},{statsSid,DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED,0,"stats_channel"},{partialInputSid,DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED,300,"input_channel_partially_reliable"},{controlSid,DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED,300,"control_channel_partially_reliable"}};
 for(unsigned i=0;i<std::size(channels);++i){const auto& c=channels[i];if(!(channelRequests_&(1U<<i))&&peer_connection_create_datachannel_sid(pc_,c.type,0,c.reliability,const_cast<char*>(c.label),const_cast<char*>(""),c.sid)>=0)channelRequests_|=1U<<i;}
 updateInputReady();
}
void Stream::updateInputReady(){
 if(!pc_||failed_)return;
 const bool open=peer_connection_datachannel_is_open(pc_,inputSid)&&peer_connection_datachannel_is_open(pc_,partialInputSid);
 if(inputReady_&&!open){fail("Input data channel closed");return;}
 inputReady_=handshakeReceived_&&open;
}
void Stream::dataOpen(void* ctx){static_cast<Stream*>(ctx)->openChannels();}
void Stream::dataClose(void* ctx){auto& self=*static_cast<Stream*>(ctx);if(self.pc_&&!self.failed_)self.fail("Input data channel closed");}
bool Stream::takeRumble(Rumble& result){
 if(!inputReady()||!rumble_)return false;
 result=*rumble_;rumble_.reset();return true;
}
void Stream::data(const char* data,std::size_t size,std::uint16_t sid){
 if(!pc_||failed_||!data||size>65539||(sid!=inputSid&&sid!=controlSid))return;
 const auto* b=reinterpret_cast<const std::uint8_t*>(data);
 control::Reader commands(b,size);control::Command command;
 while(commands.next(command)){
  if((command.code==0x104||command.code==0x105)&&command.size>=4){sessionTimer_={static_cast<std::uint8_t>(command.code==0x104?4:5),control::le32(command.payload),command.code==0x104,sceKernelGetProcessTime()};}
  else if(command.code==0x109&&command.size>=4){serverTermination_=control::termination(command.payload);char reason[96];std::snprintf(reason,sizeof(reason),"Server ended the stream (0x%08x)",serverTermination_.wireCode);fail(reason);return;}
  else if(command.code==0x10b&&inputReady_){const auto rumble=wire::decodeRumble(command.payload-4,command.size+4);if(rumble[0])rumble_=rumble[0];}
 }
 if(sid!=inputSid||handshakeReceived_||size<2||size>64)return;
 const int word=b[0]|(b[1]<<8);
 if(word!=526&&b[0]!=14)return;
 const int version=word==526?(size>=4?(b[2]|(b[3]<<8)):2):word;
 if(!version)return;
 protocol_=version;handshakeReceived_=true;handshakeAt_=sceKernelGetProcessTime();updateInputReady();
}
void Stream::checkVideoProgress(std::uint64_t now){
 const auto decoded=media_.frames.load(),presented=media_.presented.load();
 if(decoded!=decodedFrames_||presented!=presentedFrames_){decodedFrames_=decoded;presentedFrames_=presented;lastDecodedAt_=now;videoSeen_=true;recoveryRequestedAt_=0;}
 if(!videoSeen_||now-lastDecodedAt_<8000000)return;
 if(!recoveryRequestedAt_){media_.requireKeyframe();peer_connection_request_video_keyframe(pc_);keyframeRequested_=now;recoveryRequestedAt_=now;}
 else if(now-recoveryRequestedAt_>=4000000)fail("WebRTC video stopped decoding after a keyframe request");
}
void Stream::tick(std::uint64_t now){if(!active())return;if(entropyFailed)fail("Secure entropy failed");if(failed_){release();return;}if(!started_)started_=now;if(ws_)ws_->poll();
 if(ws_&&!ws_->is_connected()){
  // After the answer, media no longer needs signaling: keep the stream and leave ending it to the media watchdogs.
  if(!answerSent_)fail(("Signaling: "+ws_->get_last_error()).c_str());
  else{opennow_media_note(("SIGNALING lost after answer: "+ws_->get_last_error()).c_str());delete ws_;ws_=nullptr;}
 }
 if(failed_){release();return;}
 sendCandidates();
for(unsigned i=0;i<64;++i)if(!peer_connection_loop(pc_)||failed_)break;
 if(failed_){release();return;}
 if(now>=nextChannelAttempt_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED){openChannels();nextChannelAttempt_=now+1000000;}
 updateInputReady();
 if(failed_){release();return;}
 recoverVideoLoss();
 checkVideoProgress(now);
 if(failed_){release();return;}
 capture_.poll(OPENNOW_STORAGE_ROOT,settings_.codec()==VideoCodec::hevc,now);
 if(cursorWanted_&&inputReady_&&peer_connection_datachannel_is_open(pc_,6)&&now>=nextCursorCapture_){
  char cursor[]={8,3,1,0,1};
  if(peer_connection_datachannel_send_binary_sid(pc_,cursor,sizeof(cursor),6)>=0)nextCursorCapture_=now+1000000;
 }
 if(peer_connection_datachannel_is_open(pc_,6)&&now>=nextQos_){
  const auto sample=qos_.next(now-started_);auto bytes=qos_.packet(sample);
  if(peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(bytes.data()),bytes.size(),6)>=0)qos_.queued(sample);
  nextQos_=now+webrtc::QosFeedback::interval_us;
 }
 if(now>=nextHeartbeat_){
  if(peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED){
   PeerVideoRtpStats stats{};peer_connection_get_video_rtp_stats(pc_,&stats);
   char qosNote[120];std::snprintf(qosNote,sizeof(qosNote),"NVST QoS open=%d queued=%u newestTimestamp=%u",peer_connection_datachannel_is_open(pc_,6),qos_.queuedCount(),stats.latest_rtp_timestamp);opennow_media_note(qosNote);
   std::snprintf(qosNote,sizeof(qosNote),"CAPTURE bytes=%zu complete=%d",capture_.bytes(),capture_.done());if(capture_.bytes())opennow_media_note(qosNote);
   char diagnostic[384];std::snprintf(diagnostic,sizeof(diagnostic),"RTP total=%u decryptFail=%u unmatched=%u videoRouted=%u h264=%u decoded=%u error=%d",stats.transport_rtp,stats.decrypt_failures,stats.unmatched,stats.video_routed,stats.packets_received,media_.frames.load(),media_.decodeError.load());opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"PRESENTER frames=%u hardware=%d hdr=%d",media_.presented.load(),settings_.hardware(),media_.actualHdr.load());opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"VIDEO quality actual=%dx%d bytes=%u idr=%u / AU lost=%u gaps=%u late=%u skips=%u nack=%u / QUEUE lost=%u corrupt=%u resets=%u",
    media_.decodedWidth.load(),media_.decodedHeight.load(),media_.videoBytes.load(),media_.idrFrames.load(),
    stats.access_units_dropped,stats.sequence_gaps,stats.late_packets_dropped,stats.forced_sequence_skips,stats.nack_requests,
    media_.queueDrops.load(),media_.corruptFrames.load(),media_.recoveryResets.load());opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"ASSEMBLER ready=%d lastResult=%d",stats.assembler_ready,stats.last_video_result);opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"AUDIO recovered=%u concealed=%u underruns=%u / TARGET %dx%d %d FPS %d kbps",
    media_.audioRecovered.load(),media_.audioConcealed.load(),media_.audioUnderruns.load(),settings_.width,settings_.height,settings_.fps,settings_.bitrate_kbps);opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"AUDIO requested=%u decoded=%u apiOutput=%u bytes=%u samples=%u queue=%u dropped=%u fecAttempts=%u stereoFallbacks=%u",
    session_.audioChannels,media_.audioChannels.load(),media_.audioCapacity(),media_.audioBytes.load(),media_.audioSamples.load(),media_.audioQueueFrames.load(),media_.audioDroppedFrames.load(),media_.audioFecAttempts.load(),media_.audioStereoFallbacks.load());opennow_media_note(diagnostic);
   std::string types="PT";for(unsigned pt=0;pt<128;++pt)if(stats.payload_counts[pt]){char item[32];std::snprintf(item,sizeof(item)," %u=%u",pt,stats.payload_counts[pt]);types+=item;}opennow_media_note(types.c_str());
   std::snprintf(status_,sizeof(status_),"VIDEO RTP %u AU %u LOST %u / DECODE %u ERR %d / AUDIO %u ERR %u",
    stats.packets_received,stats.access_units_completed,stats.access_units_dropped,media_.frames.load(),media_.decodeError.load(),media_.audioPackets.load(),media_.audioErrors.load());
   // Replace a small private snapshot, so late symptoms remain observable after
   // the bounded startup media log has filled. No session/network secrets.
   if(auto* live=std::fopen(opennow_storage_file("live-video.status"),"wb")){
    std::fprintf(live,"version=" OPENNOW_VERSION " elapsed_us=%llu width=%d height=%d bytes=%u decoded=%u presented=%u error=%d hdr=%d lost=%u gaps=%u queue_lost=%u resets=%u qos_open=%d qos_queued=%u capture_bytes=%zu capture_done=%d\n",
     static_cast<unsigned long long>(now-started_),media_.decodedWidth.load(),media_.decodedHeight.load(),media_.videoBytes.load(),media_.frames.load(),media_.presented.load(),media_.decodeError.load(),media_.actualHdr.load(),stats.access_units_dropped,stats.sequence_gaps,media_.queueDrops.load(),media_.recoveryResets.load(),peer_connection_datachannel_is_open(pc_,6),qos_.queuedCount(),capture_.bytes(),capture_.done());
    std::fprintf(live,"au_received=%u queue_depth=%u queue_peak=%u queue_max_us=%llu decode_calls=%u decode_us=%llu decode_max_us=%llu gpu_calls=%u gpu_us=%llu gpu_max_us=%llu\n",
     stats.access_units_completed,media_.queueDepth.load(),media_.queuePeak.load(),static_cast<unsigned long long>(media_.queueMaxUs.load()),media_.decodeCalls.load(),static_cast<unsigned long long>(media_.decodeUs.load()),static_cast<unsigned long long>(media_.decodeMaxUs.load()),media_.gpuCalls.load(),static_cast<unsigned long long>(media_.gpuUs.load()),static_cast<unsigned long long>(media_.gpuMaxUs.load()));
    const auto t=media_.nativeTiming();
    std::fprintf(live,"target_fps=%d target_hdr=%d codec=%d native_copy_us=%llu native_publish_us=%llu native_decode_us=%llu native_flush_us=%llu native_decode_calls=%llu native_flush_calls=%llu native_blocked_attempts=%llu native_worker_mask=%llu native_pipeline_depth=%u native_inflight=%u\n",
     settings_.fps,settings_.hdr(),static_cast<int>(settings_.codec()),static_cast<unsigned long long>(t.copy_us),static_cast<unsigned long long>(t.publish_us),static_cast<unsigned long long>(t.decode_us),static_cast<unsigned long long>(t.flush_us),static_cast<unsigned long long>(t.decode_calls),static_cast<unsigned long long>(t.flush_calls),static_cast<unsigned long long>(t.blocked_attempts),static_cast<unsigned long long>(t.worker_mask),t.pipeline_depth,t.in_flight);
    std::fprintf(live,"audio_requested_channels=%u audio_channels=%u audio_api_output=%u audio_bytes=%u audio_samples=%u audio_queue_frames=%u audio_queue_peak=%u audio_dropped_frames=%u audio_output_errors=%u audio_recovered=%u audio_concealed=%u audio_underruns=%u audio_fec_attempts=%u audio_stereo_fallbacks=%u\n",
     session_.audioChannels,media_.audioChannels.load(),media_.audioCapacity(),media_.audioBytes.load(),media_.audioSamples.load(),media_.audioQueueFrames.load(),media_.audioQueuePeak.load(),media_.audioDroppedFrames.load(),media_.audioOutputErrors.load(),media_.audioRecovered.load(),media_.audioConcealed.load(),media_.audioUnderruns.load(),media_.audioFecAttempts.load(),media_.audioStereoFallbacks.load());
    std::fclose(live);
   }
   if(!media_.frames&&now-keyframeRequested_>=2000000){peer_connection_request_video_keyframe(pc_);keyframeRequested_=now;}
  }
  auto* hb=cJSON_CreateObject();cJSON_AddNumberToObject(hb,"hb",1);send(hb);cJSON_Delete(hb);nextHeartbeat_=now+2000000;
  if(inputReady_){char beat[4]={2,0,0,0};if(peer_connection_datachannel_send_binary_sid(pc_,beat,sizeof(beat),inputSid)<0)fail("Input heartbeat is backpressured");}
if(!answerSent_)peerInfo();
 }
 if(!media_.frames&&now-started_>45000000){const auto last=std::string("Video timeout: ")+status_;fail(last.c_str());}
 if(failed_)release();
}
void Stream::input(const PS5_PadData& pad,std::uint64_t now){if(!inputReady_||!pc_||failed_)return;
 const auto state=gamepadState(pad,settings_.controller_left_deadzone,settings_.controller_right_deadzone);
 const auto neutral=state.neutral();
 const bool immediate=!inputSeen_||state.buttons!=previousInput_.buttons||state.triggers!=previousInput_.triggers||
  state.connected!=previousInput_.connected||(neutral&~previousInput_.neutral())!=0;
 if(!immediate&&now-lastInput_<16000)return;
 const bool changed=inputSeen_&&(state.buttons!=previousInput_.buttons||state.triggers!=previousInput_.triggers||state.connected!=previousInput_.connected||state.lx!=previousInput_.lx||state.ly!=previousInput_.ly||state.rx!=previousInput_.rx||state.ry!=previousInput_.ry);
 const bool meaningful=changed||state.buttons||state.triggers||neutral!=3;
 if(!sendInput(wire::gamepad(state,protocol_,inputTime(now)),true,meaningful))return;
 previousInput_=state;inputSeen_=true;
 lastInput_=now;
}
bool Stream::sendInput(const wire::Bytes& bytes,bool partial,bool meaningful){
 if(!pc_||!inputReady_||failed_||peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())),bytes.size(),partial?partialInputSid:inputSid)<0)return false;
 if(meaningful)sessionTimer_.active=false;return true;
}
bool Stream::releaseKey(std::uint64_t now){
 if(keyHeld_){
  if(!sendInput(wire::key(false,heldKey_.vk,heldKey_.scan,heldModifiers_,protocol_,inputTime(now))))return false;
  keyHeld_=false;
 }
 for(auto i=std::size(modifierKeys);i--;)if(const auto& m=modifierKeys[i];heldModifiers_&m.bit){
  const auto next=static_cast<std::uint8_t>(heldModifiers_&~m.bit);
  if(!sendInput(wire::key(false,m.vk,m.scan,next,protocol_,inputTime(now))))return false;
  heldModifiers_=next;
 }
 return true;
}
void Stream::events(InputQueue& queue,std::uint64_t now){
 if(!inputReady_||!pc_){queue.cancel();return;}
 for(;;){
  if(queue.takeCancel()){pendingInputValid_=false;releasingInputs_=true;}
  if(keyHeld_&&now>=keyUpAt_)releasingKey_=true;
  if(releasingInputs_||releasingKey_){
   const bool held=keyHeld_||heldModifiers_;
   if(!releaseKey(now))return;
   if(held)nextKeyAt_=now+32000;
   releasingKey_=false;
   if(releasingInputs_){
    for(std::uint8_t b=1;b<=5;++b)if(mouseHeld_&(1U<<b)){
     if(!sendInput(wire::mouseButton(false,b,protocol_,inputTime(now))))return;
     mouseHeld_&=static_cast<std::uint8_t>(~(1U<<b));
    }
    releasingInputs_=false;
   }
  }
  if(!pendingInputValid_){
   if(!queue.take(pendingInput_,!keyHeld_&&now>=nextKeyAt_))return;
   pendingInputValid_=true;
  }
  auto& e=pendingInput_;
  if(e.kind==InputEvent::Kind::cancel){pendingInputValid_=false;releasingInputs_=true;continue;}
  cursorWanted_=true;
  if(e.kind==InputEvent::Kind::move){
   while(e.dx||e.dy){const int x=std::clamp(e.dx,-32768,32767),y=std::clamp(e.dy,-32768,32767);if(!sendInput(wire::mouseMove(static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),protocol_,inputTime(now),settings_.mouse_sensitivity),true))return;e.dx-=x;e.dy-=y;}
  } else if(e.kind==InputEvent::Kind::wheel){
   if(!sendInput(wire::mouseWheel(static_cast<std::int16_t>(e.dx),static_cast<std::int16_t>(e.dy),protocol_,inputTime(now))))return;
  } else if(e.kind==InputEvent::Kind::button){
   if(e.button>=1&&e.button<=5){
    const unsigned bit=1U<<e.button;
    if(e.down!=((mouseHeld_&bit)!=0)){
     if(!sendInput(wire::mouseButton(e.down,e.button,protocol_,inputTime(now))))return;
     mouseHeld_^=bit;
    }
   }
  } else {
   heldKey_=e.key;
   for(const auto& m:modifierKeys)if((e.key.modifiers&m.bit)&&!(heldModifiers_&m.bit)){
    const auto next=static_cast<std::uint8_t>(heldModifiers_|m.bit);
    if(!sendInput(wire::key(true,m.vk,m.scan,next,protocol_,inputTime(now))))return;
    heldModifiers_=next;
   }
   if(!sendInput(wire::key(true,e.key.vk,e.key.scan,heldModifiers_,protocol_,inputTime(now))))return;
   keyHeld_=true;keyUpAt_=now+32000;
  }
  pendingInputValid_=false;
 }
}
}
extern "C" int mbedtls_hardware_poll(void*,unsigned char* out,std::size_t size,std::size_t* used){*used=0;if(!opennow::randomBytes(out,size)){entropyFailed=true;return -1;}*used=size;return 0;}

extern "C" int opennow_peer_random(unsigned char* out,std::size_t size){if(opennow::randomBytes(out,size))return 0;entropyFailed=true;return -1;}
extern "C" void arc4random_buf(void* out,std::size_t size){opennow_peer_random(static_cast<unsigned char*>(out),size);}
