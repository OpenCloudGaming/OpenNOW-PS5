// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/audio_playout.hpp"
#include "stream/sdp.hpp"
#include <cassert>
#include <vector>

int main() {
 using namespace opennow;
 const std::string stereo="m=audio 9 UDP/TLS/RTP/SAVPF 111 63 112 113 114\r\na=mid:audio\r\n"
  "a=rtpmap:111 opus/48000/2\r\na=rtpmap:63 red/48000/2\r\na=fmtp:63 111/111\r\n";
 const std::string five="a=rtpmap:112 multiopus/48000/6\r\na=fmtp:112 num_streams=4;coupled_streams=2;channel_mapping=0,4,1,2,3,5\r\n";
 const std::string seven="a=rtpmap:113 multiopus/48000/8\r\na=fmtp:113 num_streams=5;coupled_streams=3;channel_mapping=0,6,1,2,3,4,5,7\r\n"
  "a=rtpmap:114 red/48000/8\r\na=fmtp:114 113/113\r\n";
 const std::string video="m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:video\r\na=rtpmap:96 H264/90000\r\n";
 const auto offer=video+stereo+five+seven;
 for(unsigned channels:{2u,6u,8u}){
  const auto format=audio::selectFormat(offer,channels);assert(audio::validFormat(format)&&format.channels==channels&&format.mid=="audio"&&format.mediaIndex==1);
  assert(format.redPayload==(channels==2?63:channels==8?114:-1));
  const auto answer=sdp::AdaptAnswerSdpToOffer(video+stereo,offer,StreamSettings{},&format);
  assert(answer.find("m=audio 9 UDP/TLS/RTP/SAVPF "+std::to_string(format.payload))!=std::string::npos);
  assert(answer.find("maxaveragebitrate="+std::to_string(format.bitrate()))!=std::string::npos);
  assert(answer.find("useinbandfec=1")!=std::string::npos);
  assert((answer.find("a=rtpmap:111 ")!=std::string::npos)==(channels==2));
  assert((answer.find("a=rtpmap:63 ")!=std::string::npos)==(channels==2));
 }
 assert(audio::selectFormat(stereo,8).channels==2);
 for(const char* mapping:{"0,4,1,2,3,3","0,4,1,2,3,6","0,4,1,2,3","0,4,1,2,3,5,","0,4,1,2,3,5junk"}){
  const auto malformed=stereo+"a=rtpmap:112 multiopus/48000/6\r\na=fmtp:112 num_streams=4;coupled_streams=2;channel_mapping="+mapping+"\r\n";
  assert(audio::selectFormat(malformed,8).channels==2);
 }
 for(const char* params:{"num_streams=4;num_streams=4;coupled_streams=2;channel_mapping=0,4,1,2,3,5",
      "num_streams=4294967295;coupled_streams=7;channel_mapping=0,4,1,2,3,5",
      "num_streams=3;coupled_streams=2;channel_mapping=0,4,1,2,3,5"})
  assert(audio::selectFormat(stereo+"a=rtpmap:112 multiopus/48000/6\r\na=fmtp:112 "+params+"\r\n",8).channels==2);
 assert(!audio::selectFormat(stereo+"m=audio 9 RTP/AVP 112\r\n"+five,8).channels);
 assert(!audio::selectFormat("m=audio 0 RTP/AVP 112\r\n"+five+stereo,8).channels);
 assert(!audio::selectFormat("m=audio 0 RTP/AVP 111\r\na=rtpmap:111 opus/48000/2\r\n",8).channels);
 assert(!audio::selectFormat(stereo+"a=recvonly\r\n",8).channels);
 assert(!audio::selectFormat(stereo+"a=inactive\r\n",8).channels);
 assert(!audio::selectFormat(stereo+"a=rtpmap:111 multiopus/48000/6\r\n",8).channels);
 assert(!audio::selectFormat("m=audio 9 RTP/AVP 111\r\na=rtpmap:112 opus/48000/2\r\n",8).channels);
 assert(!audio::selectFormat("m=audio 9 RTP/AVP 128\r\na=rtpmap:128 opus/48000/2\r\n",8).channels);
 assert(!audio::selectFormat(std::string(65537,'a'),8).channels);
 assert(audio::selectFormat(stereo+video+five,8).channels==2);
 assert(!audio::selectFormat("m=audio 9 RTP/AVP 112\r\na=rtpmap:112 multiopus/48000/6\r\n"+video+five,8).channels);
 const auto crossRed="m=audio 9 RTP/AVP 111 63\r\na=rtpmap:111 opus/48000/2\r\na=rtpmap:63 red/48000/2\r\n"+video+"a=fmtp:63 111/111\r\n";
 assert(audio::selectFormat(crossRed,2).redPayload==-1);
 const auto collision="m=video 9 RTP/AVP 111\r\na=rtpmap:111 H264/90000\r\n"+stereo;
 assert(!audio::selectFormat(collision,2).channels);
 assert(sdp::AdaptAnswerSdpToOffer(video+stereo,collision,StreamSettings{}).empty());
 const auto chosen=audio::selectFormat(offer,8);
 assert(sdp::AdaptAnswerSdpToOffer(video+stereo,offer+stereo,StreamSettings{},&chosen).empty());
 assert(sdp::AdaptAnswerSdpToOffer(video+stereo,offer+video,StreamSettings{},&chosen).empty());
 auto dynamic=audio::selectFormat("m=audio 9 RTP/AVP 107 108\r\na=rtpmap:107 opus/48000/2\r\na=rtpmap:108 red/48000/2\r\na=fmtp:108 107/107\r\n",2);
 assert(dynamic.payload==107&&dynamic.redPayload==108);
 for(auto mode:{audio::Mode::automatic,audio::Mode::stereo,audio::Mode::surround51,audio::Mode::surround71})assert(audio::requestedChannels(mode,2)==2);
 assert(audio::requestedChannels(audio::Mode::automatic,8)==8&&audio::requestedChannels(audio::Mode::surround51,8)==6&&audio::requestedChannels(audio::Mode::surround71,6)==6);

 const std::int16_t input[]={100,200,300,400,500,600,700,800};std::int16_t mapped[8];
 audio::outputFrame(input,6,mapped,8);const std::int16_t expected51[]={100,300,200,600,400,500,0,0};assert(std::equal(mapped,mapped+8,expected51));
 audio::outputFrame(input,8,mapped,8);const std::int16_t expected71[]={100,300,200,800,600,700,400,500};assert(std::equal(mapped,mapped+8,expected71));
 audio::outputFrame(input,2,mapped,8);assert(mapped[0]==100&&mapped[1]==200&&mapped[2]==0&&mapped[7]==0);
 audio::PlayoutQueue queue;assert(queue.open(8));
 std::vector<std::int16_t> pcm(5760*8,1234);std::int16_t block[256*8];
 queue.push(pcm.data(),480,8);queue.pop(block);assert(queue.frames()==480&&block[0]==0&&queue.underruns()==0);
 queue.push(pcm.data(),480,8);queue.pop(block);assert(queue.frames()==960&&block[0]==0);
 queue.push(pcm.data(),480,8);queue.pop(block);assert(queue.frames()==1184&&block[0]==1234&&block[2047]==1234);
 for(int i=0;i<5;++i)queue.pop(block);
 assert(queue.frames()==0&&queue.underruns()==1&&queue.target()==1440&&block[160*8]==0);
 for(int round=0;round<5;++round){queue.clear();queue.push(pcm.data(),1920,8);queue.push(pcm.data(),1920,8);while(queue.frames())queue.pop(block);queue.pop(block);}
 assert(queue.target()==1920);
 queue.clear();queue.pop(block);assert(block[0]==0);
 assert(queue.open(2));queue.push(pcm.data(),1920,2);std::fill(pcm.begin(),pcm.end(),4321);queue.push(pcm.data(),960,2);
 assert(queue.frames()==1920&&queue.dropped()==960);
 for(int i=0;i<4;++i)queue.pop(block);
 assert(block[0]==1234&&block[192*2]==4321);
 queue.push(pcm.data(),5760,2);assert(queue.frames()==5760);queue.push(pcm.data(),5761,2);assert(queue.frames()==5760);
 while(queue.frames())queue.pop(block);
 queue.push(pcm.data(),960,2);queue.push(pcm.data(),960,2);queue.push(pcm.data(),960,2);queue.pop(block);assert(block[0]==4321);
 assert(queue.open(2));queue.push(pcm.data(),960,2);queue.push(pcm.data(),960,2);
 unsigned elapsed=0,next=960;
 for(int i=0;i<1000;++i){while(elapsed>=next){queue.push(pcm.data(),960,2);next+=960;}queue.pop(block);assert(block[0]==4321&&block[511]==4321);elapsed+=256;}
 assert(queue.underruns()==0);
}
