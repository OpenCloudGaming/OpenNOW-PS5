#include "stream/video_capture.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <chrono>
int main(){
 const auto root=std::filesystem::temp_directory_path()/("opennow-capture-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 assert(std::filesystem::create_directory(root));const auto path=root.string();
 const unsigned char data[]={1,2,3};opennow::video::ShortCapture capture;
 capture.arm(path,true,0);capture.receive(data,3,10000000,true);
 assert(!std::filesystem::exists(root/"capture-video.hevc"));
 std::ofstream(root/"capture-video.request").put('1');capture.arm(path,true,0);
 assert(!std::filesystem::exists(root/"capture-video.request"));
 capture.receive(data,3,9999999,true);assert(!std::filesystem::exists(root/"capture-video.hevc"));
 capture.receive(data,3,10000000,false);assert(!std::filesystem::exists(root/"capture-video.hevc"));
 capture.receive(data,3,10000000,true);capture.receive(data,3,11000000,false);
 capture.receive(data,3,13000000,false);assert(capture.done()&&capture.bytes()==6);
 assert(std::filesystem::file_size(root/"capture-video.hevc")==6);
 capture.receive(data,3,14000000,true);assert(capture.bytes()==6);
 std::ofstream(root/"capture-video.request").put('1');capture.arm(path,false,0);capture.receive(data,3,10000000,true);
 capture.receive(data,opennow::video::ShortCapture::limit,11000000,false);
 assert(capture.done()&&capture.bytes()==3);assert(std::filesystem::file_size(root/"capture-video.h264")==3);
 // A fresh operator request after two minutes works without stream restart.
 std::ofstream(root/"capture-video.request").put('1');capture.poll(path,true,120000000);
 assert(!std::filesystem::exists(root/"capture-video.request"));
 capture.receive(data,3,120000000,false);assert(capture.bytes()==0);
 capture.receive(data,3,120000001,true);assert(capture.bytes()==3);
 std::ofstream(root/"capture-video.request").put('1');capture.poll(path,true,121000000);
 assert(std::filesystem::exists(root/"capture-video.request")); // active capture cannot truncate
 capture.receive(data,3,123000001,false);assert(capture.done());
 std::ifstream status(root/"capture-video.status");std::string state;std::getline(status,state);
 assert(state.find("state=complete bytes=3")!=std::string::npos);
 capture.poll(path,true,124000000);capture.receive(data,3,124000001,true);
 assert(capture.bytes()==3&&!capture.done()); // queued request starts a new capture
 capture.close();assert(std::filesystem::file_size(root/"capture-video.hevc")==3);
 std::filesystem::remove_all(root);
}
