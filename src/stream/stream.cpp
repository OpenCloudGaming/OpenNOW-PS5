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
int number(const cJSON* j,const char* k){auto* p=get(j,k);return cJSON_IsNumber(p)?p->valueint:0;}
void le(std::vector<std::uint8_t>& b,std::uint64_t n,unsigned bytes){while(bytes--){b.push_back(n&255);n>>=8;}}
void be(std::vector<std::uint8_t>& b,std::uint64_t n,unsigned bytes){while(bytes)b.push_back((n>>(--bytes*8))&255);}
struct ModifierKey{std::uint8_t bit;std::uint16_t vk,scan;};
constexpr ModifierKey modifierKeys[]={{modifierShift,0xa0,0x2a},{modifierCtrl,0x11,0x1d},{modifierAlt,0x12,0x38}};
}
bool Stream::start(const Session& s,const char* device) {
 if(validateSettings(s.settings)!=SettingsError::none){stop();fail("Invalid stream settings");return false;}
 qos_={};nextQos_=0;qosRequested_=false;
 stop();opennow_media_note("START " OPENNOW_VERSION);entropyFailed=false;session_=s;settings_=s.settings;name_=std::string("opennow-")+device;peerId_=remoteId_=ack_=0;answerSent_=inputReady_=false;nextHeartbeat_=started_=lastInput_=0;keyHeld_=false;mouseHeld_=0;keyUpAt_=nextKeyAt_=0;inputAttempts_=0;inputOpened_=keyframeRequested_=0;candidates_.clear();lastVideoLoss_=0;
 capture_.arm(OPENNOW_STORAGE_ROOT,settings_.codec()==VideoCodec::hevc,sceKernelGetProcessTime());
 if(std::strncmp(s.signaling,"wss://",6)){fail("Invalid secure signaling endpoint");release();return false;}
 if(!media_.start(settings_)){fail("Could not initialize video decoder / audio output");release();return false;}
 if(peer_init()!=0){fail("WebRTC runtime initialization failed");release();return false;}runtimeReady_=true;
 PeerConfiguration config{};config.video_codec=settings_.codec()==VideoCodec::hevc?CODEC_HEVC:CODEC_H264;config.audio_codec=CODEC_OPUS;config.datachannel=DATA_CHANNEL_STRING;config.user_data=this;config.onvideopacket=video;config.onaudiopacket=audio;
 config.ice_servers[0].urls="stun:s1.stun.gamestream.nvidia.com:19308";
 peer_connection_set_diagnostics_enabled(0);pc_=peer_connection_create(&config);
 if(!pc_||entropyFailed){fail("Unable to initialize WebRTC");release();return false;}
 PeerVideoRtpStats initial{};peer_connection_get_video_rtp_stats(pc_,&initial);
 if(!initial.assembler_ready){fail("Video assembler initialization failed");release();return false;}
 peer_connection_onicecandidate(pc_,ice);peer_connection_oniceconnectionstatechange(pc_,state);peer_connection_ondatachannel(pc_,dataMessage,dataOpen,dataClose);
 std::string url=s.signaling;auto q=url.find_first_of("?#");if(q!=std::string::npos)url.resize(q);while(url.size()&&url.back()=='/')url.pop_back();if(url.size()<7||url.substr(url.size()-7)!="sign_in")url+="/sign_in";
 url+="?peer_id="+name_+"&version=2&peer_role=1&pairing_id="+s.id;
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
void Stream::fail(const char* reason){if(!failed_){failed_=true;std::snprintf(status_,sizeof(status_),"%s",reason);}}
void Stream::stop(){release();failed_=false;}
void Stream::release() {
 capture_.close();
 if(pc_){
  const auto at=lastInput_+20000;
  if(inputReady_){releaseKey(at);for(std::uint8_t b=1;b<=5;++b)if(mouseHeld_&(1U<<b))sendInput(wire::mouseButton(false,b,protocol_,at));}
  mouseHeld_=0;PS5_PadData neutral{};input(neutral,at);peer_connection_close(pc_);peer_connection_destroy(pc_);pc_=nullptr;}
 if(ws_){ws_->disconnect();delete ws_;ws_=nullptr;}
 if(runtimeReady_){peer_deinit();runtimeReady_=false;}
 media_.stop();secureErase(&session_,sizeof(session_));session_={};inputReady_=false;answerSent_=false;candidates_.clear();candidateMid_.clear();candidateMLine_=0;
}
void Stream::send(cJSON* root){char* value=cJSON_PrintUnformatted(root);if(value&&ws_)ws_->send_message(value);cJSON_free(value);}
void Stream::payload(cJSON* value){if(!ws_)return;char* data=cJSON_PrintUnformatted(value);if(!data)return;auto* root=cJSON_CreateObject();auto* msg=cJSON_AddObjectToObject(root,"peer_msg");cJSON_AddNumberToObject(msg,"from",peerId_);cJSON_AddNumberToObject(msg,"to",remoteId_);cJSON_AddStringToObject(msg,"msg",data);cJSON_AddNumberToObject(root,"ackid",++ack_);send(root);cJSON_Delete(root);cJSON_free(data);}
void Stream::sendCandidates(){
 if(!answerSent_)return;
 for(const auto& line:candidates_){auto* c=cJSON_CreateObject();cJSON_AddStringToObject(c,"candidate",line.c_str());cJSON_AddStringToObject(c,"sdpMid",candidateMid_.c_str());cJSON_AddNumberToObject(c,"sdpMLineIndex",candidateMLine_);payload(c);cJSON_Delete(c);}
 candidates_.clear();
}
void Stream::peerInfo(){auto* root=cJSON_CreateObject();cJSON_AddNumberToObject(root,"ackid",++ack_);auto* info=cJSON_AddObjectToObject(root,"peer_info");cJSON_AddStringToObject(info,"browser","Chrome");cJSON_AddStringToObject(info,"browserVersion","131");cJSON_AddBoolToObject(info,"connected",true);cJSON_AddNumberToObject(info,"id",peerId_);cJSON_AddStringToObject(info,"name",name_.c_str());cJSON_AddNumberToObject(info,"peerRole",0);const auto resolution=std::to_string(settings_.width)+"x"+std::to_string(settings_.height);cJSON_AddStringToObject(info,"resolution",resolution.c_str());cJSON_AddNumberToObject(info,"version",2);send(root);cJSON_Delete(root);}
void Stream::message(const std::string& message) {
 if(failed_||message.size()>65536)return;auto* root=cJSON_ParseWithLength(message.c_str(),message.size());if(!root)return;
 auto* info=get(root,"peer_info");if(!std::strcmp(text(info,"name"),name_.c_str()))peerId_=number(info,"id");
 if(cJSON_IsNumber(get(root,"ackid"))&&(!info||number(info,"id")!=peerId_)){auto* a=cJSON_CreateObject();cJSON_AddNumberToObject(a,"ack",number(root,"ackid"));send(a);cJSON_Delete(a);}
 if(get(root,"hb")){auto* a=cJSON_CreateObject();cJSON_AddNumberToObject(a,"hb",1);send(a);cJSON_Delete(a);}
 if(!std::strcmp(text(root,"error"),"peerRemoved"))fail("Signaling: peerRemoved");
 auto* msg=get(root,"peer_msg");if(msg){remoteId_=number(msg,"from");if(!std::strcmp(text(msg,"msg"),"BYE"))fail("Signaling: server ended the stream");auto* data=cJSON_Parse(text(msg,"msg"));if(data){
  if(!std::strcmp(text(data,"type"),"offer")){
   std::string offer=sdp::PrepareGfnOfferSdp(text(data,"sdp"),session_.signaling,session_.mediaIp,session_.mediaPort);
   if(offer.size()<60000&&!offer.empty()){
    mediaSdp("OFFER",offer);peer_connection_set_remote_description(pc_,offer.c_str(),SDP_TYPE_OFFER);
    const char* raw=peer_connection_create_answer(pc_);
    if(raw&&!entropyFailed){auto answer=sdp::AdaptAnswerSdpToOffer(raw,offer,settings_);if(answer.empty()){fail("Server did not offer the selected video codec");}else{mediaSdp("ANSWER",answer);auto nvst=webrtc::BuildNvstSdp(answer,settings_,sdp::ParseRiInputCapabilities(offer));auto* a=cJSON_CreateObject();cJSON_AddStringToObject(a,"type","answer");cJSON_AddStringToObject(a,"sdp",answer.c_str());cJSON_AddStringToObject(a,"nvstSdp",nvst.c_str());payload(a);cJSON_Delete(a);answerSent_=true;
     const auto bundle=sdp::ExtractSdpValue(answer,"a=group:BUNDLE ");
     candidateMid_=bundle.empty()?sdp::ExtractSdpValue(answer,"a=mid:"):bundle.substr(0,bundle.find(' '));
     int section=-1;
     for(std::size_t pos=0;pos<answer.size();){auto end=answer.find("\r\n",pos);if(end==std::string::npos)end=answer.size();const auto line=answer.substr(pos,end-pos);if(line.rfind("m=",0)==0)++section;if(line=="a=mid:"+candidateMid_){candidateMLine_=section;break;}pos=end+2;}
     sendCandidates();
     int pairs=0;if(peer_connection_get_ice_candidate_pair_stats(pc_,&pairs,nullptr,nullptr,nullptr,nullptr)==0&&pairs==0){auto manual=sdp::BuildManualMediaCandidate(session_.signaling,session_.mediaIp,session_.mediaPort,100);if(!manual.empty())peer_connection_add_ice_candidate(pc_,manual.data());}
     std::snprintf(status_,sizeof(status_),"Negotiating secure media connection");
    }}else{fail(entropyFailed?"Secure entropy failed":"Unable to create WebRTC answer");}
   }else{fail("Invalid server stream offer");}
  }else if(*text(data,"candidate")){auto candidate=sdp::RewriteGfnMediaCandidate(text(data,"candidate"),session_.mediaIp,session_.mediaPort,session_.signaling);if(candidate.rfind("a=",0))candidate="a="+candidate;if(candidate.size()<2048)peer_connection_add_ice_candidate(pc_,candidate.data());}
  cJSON_Delete(data);
 }}cJSON_Delete(root);
}
void Stream::ice(char* s,void* ctx){auto& self=*static_cast<Stream*>(ctx);std::string lines=s?s:"";std::size_t pos=0;while(pos<lines.size()){auto end=lines.find('\n',pos);if(end==std::string::npos)end=lines.size();auto line=lines.substr(pos,end-pos);if(line.size()&&line.back()=='\r')line.pop_back();if(line.rfind("a=candidate:",0)==0&&self.candidates_.size()<16)self.candidates_.push_back(line.substr(2));pos=end+1;}}
void Stream::state(PeerConnectionState s,void* ctx){auto& self=*static_cast<Stream*>(ctx);if(self.failed_)return;const auto status=std::string("WebRTC: ")+peer_connection_state_to_string(s);if(s==PEER_CONNECTION_FAILED||s==PEER_CONNECTION_CLOSED)self.fail(status.c_str());else std::snprintf(self.status_,sizeof(self.status_),"%s",status.c_str());}
void Stream::recoverVideoLoss(){
 PeerVideoRtpStats stats{};peer_connection_get_video_rtp_stats(pc_,&stats);
 if(stats.access_units_dropped!=lastVideoLoss_){lastVideoLoss_=stats.access_units_dropped;media_.requireKeyframe();}
}
void Stream::video(const PeerVideoPacket* p,void* ctx){auto& self=*static_cast<Stream*>(ctx);self.recoverVideoLoss();if(p){self.qos_.received(p->size);const auto now=sceKernelGetProcessTime();self.capture_.receive(p->data,p->size,now,self.capture_.wantsIdr(now)&&video::Recovery::hasIdr(p->data,p->size,self.settings_.codec()==VideoCodec::hevc));if(!self.media_.video(p->data,p->size)){if(now-self.keyframeRequested_>=250000){peer_connection_request_video_keyframe(self.pc_);self.keyframeRequested_=now;}}}}
void Stream::audio(const PeerAudioPacket* p,void* ctx){if(p)static_cast<Stream*>(ctx)->media_.audio(p->data,p->size,p->sequence,p->payload_type,p->timestamp);}
void Stream::dataMessage(char* data,std::size_t size,void* ctx,std::uint16_t sid){static_cast<Stream*>(ctx)->data(data,size,sid);}
void Stream::dataOpen(void* ctx){auto& self=*static_cast<Stream*>(ctx);if(peer_connection_create_datachannel_sid(self.pc_,DATA_CHANNEL_RELIABLE,0,0,const_cast<char*>("input_channel_v1"),const_cast<char*>(""),0)>=0&&!self.inputOpened_)self.inputOpened_=sceKernelGetProcessTime();}
void Stream::dataClose(void* ctx){auto& self=*static_cast<Stream*>(ctx);self.inputReady_=false;self.inputOpened_=0;}
void Stream::data(const char* data,std::size_t size,std::uint16_t sid){if(sid||size<2||size>64)return;auto* b=reinterpret_cast<const unsigned char*>(data);int word=b[0]|(b[1]<<8);if(word!=526&&b[0]!=14)return;protocol_=word==526?(size>=4?(b[2]|(b[3]<<8)):2):word;protocol_=std::max(2,protocol_);if(peer_connection_datachannel_send_binary_sid(pc_,const_cast<char*>(data),size,0)>=0)inputReady_=true;}
void Stream::tick(std::uint64_t now){if(!active())return;if(entropyFailed)fail("Secure entropy failed");if(failed_){release();return;}if(!started_)started_=now;ws_->poll();
 if(!ws_->is_connected())fail(("Signaling: "+ws_->get_last_error()).c_str());
 if(failed_){release();return;}
 if(!inputReady_&&inputOpened_&&now-inputOpened_>1500000){inputReady_=true;protocol_=2;}
 sendCandidates();
for(unsigned i=0;i<64;++i)if(!peer_connection_loop(pc_)||failed_)break;
 if(failed_){release();return;}
 recoverVideoLoss();
 capture_.poll(OPENNOW_STORAGE_ROOT,settings_.codec()==VideoCodec::hevc,now);
 // The existing input stream stays on SID 0. QoS uses the source-pinned,
 // unordered 300 ms NVST control stream on SID 6, after DCEP acknowledgment.
 if(inputReady_&&!qosRequested_){
  if(peer_connection_create_datachannel_sid(pc_,DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED,0,300,const_cast<char*>("control_channel_partially_reliable"),const_cast<char*>(""),6)>=0){qosRequested_=true;opennow_media_note("NVST QoS control requested sid=6 lifetime=300");}
 }
 if(peer_connection_datachannel_is_open(pc_,6)&&now>=nextQos_){
  PeerVideoRtpStats stats{};peer_connection_get_video_rtp_stats(pc_,&stats);
  const auto sample=qos_.next(stats.latest_rtp_timestamp);auto bytes=qos_.packet(sample,now-started_>=1900000);
  if(peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(bytes.data()),bytes.size(),6)>=0)qos_.queued(sample);
  nextQos_=now+55556;
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
   std::string types="PT";for(unsigned pt=0;pt<128;++pt)if(stats.payload_counts[pt]){char item[32];std::snprintf(item,sizeof(item)," %u=%u",pt,stats.payload_counts[pt]);types+=item;}opennow_media_note(types.c_str());
   std::snprintf(status_,sizeof(status_),"VIDEO RTP %u AU %u LOST %u / DECODE %u ERR %d / AUDIO %u ERR %u",
    stats.packets_received,stats.access_units_completed,stats.access_units_dropped,media_.frames.load(),media_.decodeError.load(),media_.audioPackets.load(),media_.audioErrors.load());
   // Replace a small private snapshot, so late symptoms remain observable after
   // the bounded startup media log has filled. No session/network secrets.
   if(auto* live=std::fopen(OPENNOW_STORAGE_ROOT "/live-video.status","wb")){
    std::fprintf(live,"version=" OPENNOW_VERSION " elapsed_us=%llu width=%d height=%d bytes=%u decoded=%u presented=%u error=%d hdr=%d lost=%u gaps=%u queue_lost=%u resets=%u qos_open=%d qos_queued=%u capture_bytes=%zu capture_done=%d\n",
     static_cast<unsigned long long>(now-started_),media_.decodedWidth.load(),media_.decodedHeight.load(),media_.videoBytes.load(),media_.frames.load(),media_.presented.load(),media_.decodeError.load(),media_.actualHdr.load(),stats.access_units_dropped,stats.sequence_gaps,media_.queueDrops.load(),media_.recoveryResets.load(),peer_connection_datachannel_is_open(pc_,6),qos_.queuedCount(),capture_.bytes(),capture_.done());
    std::fprintf(live,"au_received=%u queue_depth=%u queue_peak=%u queue_max_us=%llu decode_calls=%u decode_us=%llu decode_max_us=%llu gpu_calls=%u gpu_us=%llu gpu_max_us=%llu\n",
     stats.access_units_completed,media_.queueDepth.load(),media_.queuePeak.load(),static_cast<unsigned long long>(media_.queueMaxUs.load()),media_.decodeCalls.load(),static_cast<unsigned long long>(media_.decodeUs.load()),static_cast<unsigned long long>(media_.decodeMaxUs.load()),media_.gpuCalls.load(),static_cast<unsigned long long>(media_.gpuUs.load()),static_cast<unsigned long long>(media_.gpuMaxUs.load()));
    const auto t=media_.nativeTiming();
    std::fprintf(live,"target_fps=%d target_hdr=%d codec=%d native_copy_us=%llu native_publish_us=%llu native_decode_us=%llu native_flush_us=%llu native_decode_calls=%llu native_flush_calls=%llu native_blocked_attempts=%llu native_worker_mask=%llu native_pipeline_depth=%u native_inflight=%u\n",
     settings_.fps,settings_.hdr(),static_cast<int>(settings_.codec()),static_cast<unsigned long long>(t.copy_us),static_cast<unsigned long long>(t.publish_us),static_cast<unsigned long long>(t.decode_us),static_cast<unsigned long long>(t.flush_us),static_cast<unsigned long long>(t.decode_calls),static_cast<unsigned long long>(t.flush_calls),static_cast<unsigned long long>(t.blocked_attempts),static_cast<unsigned long long>(t.worker_mask),t.pipeline_depth,t.in_flight);
    std::fclose(live);
   }
   if(!media_.frames&&now-keyframeRequested_>=2000000){peer_connection_request_video_keyframe(pc_);keyframeRequested_=now;}
  }
  auto* hb=cJSON_CreateObject();cJSON_AddNumberToObject(hb,"hb",1);send(hb);cJSON_Delete(hb);nextHeartbeat_=now+2000000;
  if(inputReady_){char beat[4]={2,0,0,0};peer_connection_datachannel_send_binary_sid(pc_,beat,sizeof(beat),0);}
if(!answerSent_)peerInfo();
  if(!inputReady_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED&&inputAttempts_++<10)dataOpen(this);
 }
 if(!media_.frames&&now-started_>45000000){const auto last=std::string("Video timeout: ")+status_;fail(last.c_str());}
 if(failed_)release();
}
void Stream::input(const PS5_PadData& pad,std::uint64_t now){if(!inputReady_||!pc_||now-lastInput_<16000)return;lastInput_=now;
 std::uint16_t buttons=0;const unsigned ps[]={PS5_PAD_BUTTON_UP,PS5_PAD_BUTTON_DOWN,PS5_PAD_BUTTON_LEFT,PS5_PAD_BUTTON_RIGHT,PS5_PAD_BUTTON_OPTIONS,PS5_PAD_BUTTON_TOUCH_PAD,PS5_PAD_BUTTON_L3,PS5_PAD_BUTTON_R3,PS5_PAD_BUTTON_L1,PS5_PAD_BUTTON_R1,PS5_PAD_BUTTON_CROSS,PS5_PAD_BUTTON_CIRCLE,PS5_PAD_BUTTON_SQUARE,PS5_PAD_BUTTON_TRIANGLE};const unsigned xb[]={1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};for(unsigned i=0;i<14;++i)if(pad.connected&&(pad.buttons&ps[i]))buttons|=xb[i];
 auto axis=[&](unsigned v,bool flip){if(!pad.connected)return 0;int n=(static_cast<int>(v)-128)*256;if(n>-3000&&n<3000)n=0;return flip?-std::max(-32767,n):n;};
 std::vector<std::uint8_t> data;le(data,12,4);le(data,26,2);le(data,0,2);le(data,1,2);le(data,20,2);le(data,buttons,2);le(data,pad.connected?(pad.analogButtons.l2|(pad.analogButtons.r2<<8)):0,2);le(data,axis(pad.leftStick.x,false),2);le(data,axis(pad.leftStick.y,true),2);le(data,axis(pad.rightStick.x,false),2);le(data,axis(pad.rightStick.y,true),2);le(data,0,2);le(data,85,2);le(data,0,2);le(data,now,8);
 if(protocol_>2){std::vector<std::uint8_t> wire{0x23};be(wire,now,8);wire.push_back(0x21);be(wire,data.size(),2);wire.insert(wire.end(),data.begin(),data.end());data=std::move(wire);}
 peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(data.data()),data.size(),0);
}
void Stream::sendInput(const wire::Bytes& bytes){peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())),bytes.size(),0);}
void Stream::releaseKey(std::uint64_t now){
 if(!keyHeld_)return;
 auto modifiers=heldKey_.modifiers;
 sendInput(wire::key(false,heldKey_.vk,heldKey_.scan,modifiers,protocol_,now));
 for(auto i=std::size(modifierKeys);i--;)if(const auto& m=modifierKeys[i];modifiers&m.bit){modifiers&=~m.bit;sendInput(wire::key(false,m.vk,m.scan,modifiers,protocol_,now));}
 keyHeld_=false;
}
void Stream::events(InputQueue& queue,std::uint64_t now){
 if(!inputReady_||!pc_){queue.cancel();return;}
 if(keyHeld_&&now>=keyUpAt_){releaseKey(now);nextKeyAt_=now+32000;}
 InputEvent e;
 while(queue.take(e,!keyHeld_&&now>=nextKeyAt_)){
  if(e.kind==InputEvent::Kind::cancel){
   if(keyHeld_){releaseKey(now);nextKeyAt_=now+32000;}
   for(std::uint8_t b=1;b<=5;++b)if(mouseHeld_&(1U<<b))sendInput(wire::mouseButton(false,b,protocol_,now));
   mouseHeld_=0;
  } else if(e.kind==InputEvent::Kind::move){
   while(e.dx||e.dy){const int x=std::clamp(e.dx,-32768,32767),y=std::clamp(e.dy,-32768,32767);sendInput(wire::mouseMove(static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),protocol_,now));e.dx-=x;e.dy-=y;}
  } else if(e.kind==InputEvent::Kind::button){
   if(e.button<1||e.button>5)continue;
   const unsigned bit=1U<<e.button;
   if(e.down==((mouseHeld_&bit)!=0))continue;
   sendInput(wire::mouseButton(e.down,e.button,protocol_,now));mouseHeld_^=bit;
  } else {
   heldKey_=e.key;keyHeld_=true;keyUpAt_=now+32000;
   std::uint8_t modifiers=0;
   for(const auto& m:modifierKeys)if(e.key.modifiers&m.bit){modifiers|=m.bit;sendInput(wire::key(true,m.vk,m.scan,modifiers,protocol_,now));}
   sendInput(wire::key(true,e.key.vk,e.key.scan,modifiers,protocol_,now));
  }
 }
}
}
extern "C" int mbedtls_hardware_poll(void*,unsigned char* out,std::size_t size,std::size_t* used){*used=0;if(!opennow::randomBytes(out,size)){entropyFailed=true;return -1;}*used=size;return 0;}

extern "C" int opennow_peer_random(unsigned char* out,std::size_t size){if(opennow::randomBytes(out,size))return 0;entropyFailed=true;return -1;}
extern "C" void arc4random_buf(void* out,std::size_t size){opennow_peer_random(static_cast<unsigned char*>(out),size);}
