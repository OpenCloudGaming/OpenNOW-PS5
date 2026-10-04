// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "account_file.hpp"
#include <string_view>

namespace opennow {
inline constexpr const char* launchAgePath="/data/opennow/launch-age.txt";

inline int parseLaunchAge(std::string_view text) noexcept {
    const auto whitespace=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';};
    while(!text.empty()&&whitespace(text.front()))text.remove_prefix(1);
    while(!text.empty()&&whitespace(text.back()))text.remove_suffix(1);
    bool negative=false;
    if(!text.empty()&&(text.front()=='+'||text.front()=='-')) {
        negative=text.front()=='-';text.remove_prefix(1);
    }
    if(text.empty())return -1;
    int age=0;
    for(const char digit:text) {
        if(digit<'0'||digit>'9')return -1;
        age=age*10+digit-'0';
        if(age>120)return -1;
    }
    return negative&&age ? -1 : age;
}

inline int readLaunchAge(const char* path=launchAgePath) noexcept {
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return -1;
    char bytes[64];std::size_t size=0;
    bool complete=false;
    while(size<sizeof(bytes)) {
        const auto count=accountFile::read(fd,bytes+size,sizeof(bytes)-size);
        if(count<0) {if(errno==EINTR)continue;break;}
        if(count==0) {complete=true;break;}
        size+=static_cast<std::size_t>(count);
    }
    const bool closed=accountFile::close(fd)==0;
    return complete&&closed ? parseLaunchAge(std::string_view(bytes,size)) : -1;
}

inline bool saveLaunchAge(const char* path,int age) noexcept {
    if(age<0||age>120)return false;
    char temporary[1024];
    const int length=std::snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    if(length<0||static_cast<std::size_t>(length)>=sizeof(temporary))return false;
    if(accountFile::remove(temporary)!=0&&errno!=ENOENT)return false;
    const int fd=accountFile::open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return false;
    char bytes[8];const auto size=static_cast<std::size_t>(std::snprintf(bytes,sizeof(bytes),"%d\n",age));
    bool ok=accountFile::permissions(fd,0600)==0;
    std::size_t written=0;
    while(ok&&written<size) {
        const auto count=accountFile::write(fd,bytes+written,size-written);
        if(count<0&&errno==EINTR)continue;
        if(count<=0) {ok=false;break;}
        written+=static_cast<std::size_t>(count);
    }
    if(ok)ok=accountFile::sync(fd)==0;
    if(accountFile::close(fd)!=0)ok=false;
    if(ok)ok=accountFile::rename(temporary,path)==0;
    if(!ok) {accountFile::remove(temporary);return false;}
    return accountFile::syncParent(path);
}

struct LaunchAgeInput {
    enum class Result { none,saved,launch };
    char text[4]{};
    const char* error="";
    unsigned selected=0;
    bool open=false;
    bool playAfterSave=false;

    void begin(int savedAge,bool play) noexcept {
        *this={};open=true;playAfterSave=play;
        if(savedAge>=0&&savedAge<=120)std::snprintf(text,sizeof(text),"%d",savedAge);
    }
    void move(int delta) noexcept {selected=static_cast<unsigned>((static_cast<int>(selected)+10+delta)%10);}
    void append() noexcept {
        const auto size=std::strlen(text);
        if(size+1<sizeof(text)) {text[size]=static_cast<char>('0'+selected);text[size+1]=0;error="";}
    }
    void erase() noexcept {const auto size=std::strlen(text);if(size)text[size-1]=0;error="";}
    void clear() noexcept {text[0]=0;error="";}
    void cancel() noexcept {*this={};}
    Result confirm(const char* path=launchAgePath) noexcept {
        if(!open)return Result::none;
        const int age=parseLaunchAge(text);
        if(age<0) {error="Enter your age from 0 to 120.";return Result::none;}
        if(!saveLaunchAge(path,age)) {error="Could not save your age. Retry or cancel.";return Result::none;}
        const auto result=playAfterSave ? Result::launch : Result::saved;
        cancel();return result;
    }
};
}
