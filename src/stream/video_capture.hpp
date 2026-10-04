// SPDX-License-Identifier: GPL-3.0-or-later
// Disabled unless a private capture-video.request marker exists. Diagnostic
// video stays in that private directory; it is never a public package asset.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <string>
namespace opennow::video {
class ShortCapture {
public:
 ~ShortCapture(){close();}
 void arm(const std::string& root,bool hevc,std::uint64_t now,std::uint64_t delay=10000000){
  close();bytes_=0;began_=0;done_=false;enabled_=false;due_=now+delay;
  statusPath_=root+"/capture-video.status";
  auto* marker=std::fopen((root+"/capture-video.request").c_str(),"rb");
  if(marker){std::fclose(marker);
   // Consume each request exactly once, including late requests during a stream.
   if(std::remove((root+"/capture-video.request").c_str())!=0)return;
   enabled_=true;path_=root+(hevc?"/capture-video.hevc":"/capture-video.h264");status("armed");
  }
 }
 void poll(const std::string& root,bool hevc,std::uint64_t now){
  if(now<nextPoll_)return;nextPoll_=now+1000000;
  if(enabled_)return;
  auto* marker=std::fopen((root+"/capture-video.request").c_str(),"rb");
  if(!marker)return;std::fclose(marker);
  arm(root,hevc,now,0);
 }
 void receive(const void* data,std::size_t size,std::uint64_t now,bool idr){
  if(!enabled_||done_||!data||!size||now<due_)return;
  if(!file_){
   if(!idr)return;
   file_=std::fopen(path_.c_str(),"wb");
   if(!file_){enabled_=false;done_=true;status("open-failed");return;}
   std::setvbuf(file_,nullptr,_IOFBF,64*1024);began_=now;status("recording");
  }
  if(now-began_>=3000000||size>limit-bytes_){close();done_=true;status("complete");return;}
  if(std::fwrite(data,1,size,file_)!=size){close();done_=true;status("write-failed");return;}
  bytes_+=size;
 }
 void close(){if(file_){std::fclose(file_);file_=nullptr;status("stopped");}enabled_=false;}
 std::size_t bytes() const{return bytes_;}
 bool done() const{return done_;}
 bool wantsIdr(std::uint64_t now) const{return enabled_&&!done_&&!file_&&now>=due_;}
 static constexpr std::size_t limit=32*1024*1024;
private:
 void status(const char* state) const{
  if(statusPath_.empty())return;
  if(auto* out=std::fopen(statusPath_.c_str(),"wb")){
   std::fprintf(out,"state=%s bytes=%zu began_us=%llu due_us=%llu\n",state,bytes_,
    static_cast<unsigned long long>(began_),static_cast<unsigned long long>(due_));std::fclose(out);
  }
 }
 std::FILE* file_=nullptr;
 std::string path_,statusPath_;
 std::uint64_t due_=0,began_=0,nextPoll_=0;
 std::size_t bytes_=0;
 bool enabled_=false,done_=false;
};
}
