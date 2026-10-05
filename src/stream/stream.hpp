#pragma once
#include "../cloud.hpp"
extern "C" {
#include "../platform/ps5-pad.h"
}
#include "media.hpp"
#include "WebSocketClient.hpp"
#include "sdp.hpp"
#include "nvst_qos.hpp"
#include "video_capture.hpp"
#include "input_wire.hpp"
#include "../input/input_queue.hpp"
extern "C" {
#include "peer_connection.h"
}
struct cJSON;
namespace opennow {
class Stream {
public:
 explicit Stream(Media& m):media_(m){}
 ~Stream(){stop();}
 bool start(const Session&,const char* device);
 void tick(std::uint64_t now);
 void input(const PS5_PadData&,std::uint64_t now);
 void events(InputQueue&,std::uint64_t now);
 void stop();
 bool active() const {return pc_||ws_||runtimeReady_;}
 bool failed() const {return failed_;}
 bool inputReady() const {return inputReady_&&pc_;}
 const char* status() const {return status_;}
private:
 void fail(const char*);void release();
 void sendInput(const wire::Bytes&);void releaseKey(std::uint64_t now);
 void message(const std::string&);void payload(cJSON*);void send(cJSON*);
 void sendCandidates();
 void recoverVideoLoss();
 void peerInfo();void data(const char*,std::size_t,std::uint16_t);
 static void ice(char*,void*);static void state(PeerConnectionState,void*);
 static void video(const PeerVideoPacket*,void*);static void audio(const PeerAudioPacket*,void*);
 static void dataMessage(char*,std::size_t,void*,std::uint16_t);
 static void dataOpen(void*);static void dataClose(void*);
 Media& media_;PeerConnection* pc_=nullptr;WebSocketClient* ws_=nullptr;
 StreamSettings settings_{};Session session_{};char status_[192]{};std::string name_;int peerId_=0,remoteId_=0,ack_=0,protocol_=2;
 bool failed_=false,runtimeReady_=false,answerSent_=false,inputReady_=false;std::vector<std::string> candidates_;
 std::string candidateMid_;int candidateMLine_=0;
 std::uint64_t nextHeartbeat_=0,started_=0,lastInput_=0;unsigned inputAttempts_=0;
 unsigned lastVideoLoss_=0;
 std::uint64_t inputOpened_=0,keyframeRequested_=0;
 webrtc::QosFeedback qos_;
 std::uint64_t nextQos_=0;
 bool qosRequested_=false;
 video::ShortCapture capture_;
 KeyStroke heldKey_{};
 bool keyHeld_=false;
 std::uint8_t mouseHeld_=0;
 std::uint64_t keyUpAt_=0,nextKeyAt_=0;
};
}
