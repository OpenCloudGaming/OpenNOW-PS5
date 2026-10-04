// SPDX-License-Identifier: GPL-3.0-or-later
#include "launch_age.hpp"
#include <cassert>
#include <cstdlib>
#include <string>

#ifdef OPENNOW_PS5
namespace {
bool interruptRead=false,interruptWrite=false,failWrite=false;
std::int64_t kernelResult(std::int64_t result) {
    return result<0 ? -65536+errno : result;
}
}
extern "C" {
int sceKernelOpen(const char* path,int flags,unsigned mode) {return static_cast<int>(kernelResult(::open(path,flags,mode)));}
std::int64_t sceKernelRead(int fd,void* bytes,std::size_t size) {
    if(interruptRead) {interruptRead=false;errno=EINTR;return kernelResult(-1);}
    return kernelResult(::read(fd,bytes,size>1?1:size));
}
std::int64_t sceKernelWrite(int fd,const void* bytes,std::size_t size) {
    if(interruptWrite) {interruptWrite=false;errno=EINTR;return kernelResult(-1);}
    if(failWrite) {errno=EIO;return kernelResult(-1);}
    return kernelResult(::write(fd,bytes,size>1?1:size));
}
int sceKernelClose(int fd) {return static_cast<int>(kernelResult(::close(fd)));}
int sceKernelFsync(int fd) {return static_cast<int>(kernelResult(::fsync(fd)));}
int sceKernelFchmod(int fd,unsigned mode) {return static_cast<int>(kernelResult(::fchmod(fd,mode)));}
int sceKernelRename(const char* from,const char* to) {return static_cast<int>(kernelResult(::rename(from,to)));}
int sceKernelUnlink(const char* path) {return static_cast<int>(kernelResult(::unlink(path)));}
}
#endif

namespace {
void writeFile(const char* path,std::string_view value) {
    const int fd=::open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    assert(fd>=0);
    assert(::write(fd,value.data(),value.size())==static_cast<ssize_t>(value.size()));
    assert(::close(fd)==0);
}
}

int main() {
    using opennow::LaunchAgeInput;
    using opennow::parseLaunchAge;
    assert(parseLaunchAge("0")==0);
    assert(parseLaunchAge("120")==120);
    assert(parseLaunchAge(" \t+021\r\n")==21);
    assert(parseLaunchAge("-0")==0);
    for(const auto value:{""," ","-1","121","999999999999999999999999","18 years","18 19","1.5","+","++18","1\n8"})
        assert(parseLaunchAge(value)==-1);
    assert(parseLaunchAge(std::string_view("18\0junk",7))==-1);

    char directory[]="/tmp/opennow-age-XXXXXX";
    assert(::mkdtemp(directory));
    const std::string path=std::string(directory)+"/launch-age.txt";
    assert(opennow::readLaunchAge(path.c_str())==-1);
    LaunchAgeInput input;
    input.begin(-1,true);
    assert(input.open&&!*input.text);
    assert(input.confirm(path.c_str())==LaunchAgeInput::Result::none);
    assert(input.open&&*input.error);
    input.selected=1;input.append();input.selected=8;input.append();
    assert(!std::strcmp(input.text,"18"));
    input.cancel();
    assert(!input.open&&!*input.text);
    assert(input.confirm(path.c_str())==LaunchAgeInput::Result::none);
    assert(opennow::readLaunchAge(path.c_str())==-1);

    input.begin(-1,true);
    input.selected=1;input.append();input.selected=2;input.append();input.selected=1;input.append();
    assert(input.confirm(path.c_str())==LaunchAgeInput::Result::none);
    input.erase();input.selected=0;input.append();input.append();
    assert(!std::strcmp(input.text,"120"));
    assert(input.confirm(path.c_str())==LaunchAgeInput::Result::launch);
    assert(!input.open&&opennow::readLaunchAge(path.c_str())==120);
    struct stat info{};
    assert(::stat(path.c_str(),&info)==0&&(info.st_mode&0777)==0600);

    input.begin(120,false);input.clear();input.selected=0;input.append();
    assert(input.confirm(path.c_str())==LaunchAgeInput::Result::saved);
    assert(opennow::readLaunchAge(path.c_str())==0);
    input.begin(0,false);input.clear();input.selected=7;input.append();input.cancel();
    assert(opennow::readLaunchAge(path.c_str())==0);
    input.begin(0,false);
    const auto unavailable=std::string(directory)+"/missing/launch-age.txt";
    assert(input.confirm(unavailable.c_str())==LaunchAgeInput::Result::none);
    assert(input.open&&*input.error);
    input.cancel();

    writeFile(path.c_str()," \n+018\t");
    assert(opennow::readLaunchAge(path.c_str())==18);
    for(const auto value:{"","121","18 19","2147483648","-1"}) {
        writeFile(path.c_str(),value);
        assert(opennow::readLaunchAge(path.c_str())==-1);
    }
    writeFile(path.c_str(),std::string_view("18\0junk",7));
    assert(opennow::readLaunchAge(path.c_str())==-1);
    writeFile(path.c_str(),std::string(10000,'1'));
    assert(opennow::readLaunchAge(path.c_str())==-1);
    assert(!opennow::saveLaunchAge(path.c_str(),-1));
    assert(!opennow::saveLaunchAge(path.c_str(),121));
    writeFile(path.c_str(),"21");
    assert(::chmod(path.c_str(),0644)==0);
    assert(opennow::saveLaunchAge(path.c_str(),25));
    assert(::stat(path.c_str(),&info)==0&&(info.st_mode&0777)==0600);
    const auto link=std::string(directory)+"/link";
    assert(::symlink(path.c_str(),link.c_str())==0);
    assert(opennow::readLaunchAge(link.c_str())==-1);
    assert(opennow::saveLaunchAge(link.c_str(),30));
    assert(opennow::readLaunchAge(link.c_str())==30);
    assert(opennow::readLaunchAge(path.c_str())==25);
#ifdef OPENNOW_PS5
    interruptRead=true;
    assert(opennow::readLaunchAge(path.c_str())==25&&!interruptRead);
    interruptWrite=true;
    assert(opennow::saveLaunchAge(path.c_str(),25)&&!interruptWrite);
    failWrite=true;
    assert(!opennow::saveLaunchAge(path.c_str(),40));
    failWrite=false;
    assert(opennow::readLaunchAge(path.c_str())==25);
    assert(::access((path+".tmp").c_str(),F_OK)!=0);
#endif
    assert(opennow::readLaunchAge(directory)==-1);
    const auto fifo=std::string(directory)+"/fifo";
    assert(::mkfifo(fifo.c_str(),0600)==0);
    assert(opennow::readLaunchAge(fifo.c_str())==-1);

    input.begin(-1,false);input.move(-1);assert(input.selected==9);
    input.move(1);assert(input.selected==0);
    input.move(-5);assert(input.selected==5);
    input.move(5);assert(input.selected==0);
    ::unlink(fifo.c_str());::unlink(link.c_str());::unlink(path.c_str());::rmdir(directory);
}
