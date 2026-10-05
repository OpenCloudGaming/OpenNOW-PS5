// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../stream/stream_settings.hpp"
#include <algorithm>
#include <cstdio>

namespace opennow::ui {
enum class StreamRow : unsigned { preset, decoding, codec, resolution, fps, bitrate, hdr, reset, count };
constexpr unsigned rowBit(StreamRow row) {return 1U<<static_cast<unsigned>(row);}
struct StreamCaps { bool h264Hardware=false, hevcSdr=false, hevcHdr=false; };

inline int maxWidth(const StreamSettings& s) noexcept {return s.hardware()?3840:1920;}
inline int maxHeight(const StreamSettings& s) noexcept {return s.hardware()?2160:1080;}
inline int maxFps(const StreamSettings& s) noexcept {return s.hardware()?120:60;}

inline unsigned changedRows(const StreamSettings& a,const StreamSettings& b) noexcept {
    unsigned rows=0;
    if(a.hardware()!=b.hardware())rows|=rowBit(StreamRow::decoding);
    if(a.codec()!=b.codec())rows|=rowBit(StreamRow::codec);
    if(a.width!=b.width||a.height!=b.height)rows|=rowBit(StreamRow::resolution);
    if(a.fps!=b.fps)rows|=rowBit(StreamRow::fps);
    if(a.bitrate_kbps!=b.bitrate_kbps||a.quality!=b.quality||a.network!=b.network)rows|=rowBit(StreamRow::bitrate);
    if(a.hdr()!=b.hdr())rows|=rowBit(StreamRow::hdr);
    return rows;
}
inline unsigned rowCount(unsigned rows) noexcept {unsigned n=0;for(;rows;rows&=rows-1)++n;return n;}

inline StreamSettings withHardware(StreamSettings s,bool hardware,const StreamCaps& caps) noexcept {
    if(hardware==s.hardware())return s;
    if(!hardware) {
        s.mode=VideoMode::h264Software;
        s.width=std::min(s.width,1920)&~1;s.height=std::min(s.height,1080)&~1;s.fps=std::min(s.fps,60);
        return s;
    }
    if(caps.h264Hardware)s.mode=VideoMode::h264Hardware;
    else if(caps.hevcSdr)s.mode=VideoMode::hevcMain10SdrHardware;
    else if(caps.hevcHdr)s.mode=VideoMode::hevcMain10HdrHardware;
    return s;
}
inline StreamSettings withHevc(StreamSettings s,bool hevc,const StreamCaps& caps) noexcept {
    if(!s.hardware()||hevc==s.tenBit())return s;
    if(!hevc){if(caps.h264Hardware)s.mode=VideoMode::h264Hardware;}
    else if(caps.hevcSdr)s.mode=VideoMode::hevcMain10SdrHardware;
    else if(caps.hevcHdr)s.mode=VideoMode::hevcMain10HdrHardware;
    return s;
}
inline StreamSettings withHdr(StreamSettings s,bool hdr,const StreamCaps& caps) noexcept {
    if(!s.tenBit()||hdr==s.hdr())return s;
    if(hdr&&caps.hevcHdr)s.mode=VideoMode::hevcMain10HdrHardware;
    if(!hdr&&caps.hevcSdr)s.mode=VideoMode::hevcMain10SdrHardware;
    return s;
}

inline bool needsFixedNetwork(const StreamSettings& s) noexcept {return s.mode==VideoMode::h264Hardware&&s.network==NetworkPolicy::adaptive;}
inline StreamSettings modeProbe(VideoMode mode) noexcept {return {320,180,30,4000,mode,QualityMode::original,NetworkPolicy::fixed};}

struct CommonSize { int width,height; };
constexpr CommonSize commonSizes[]={{1280,720},{1600,900},{1920,1080},{2560,1440},{3840,2160}};
constexpr int commonFps[]={30,60,90,120};
constexpr int commonBitrates[]={10,20,25,50,75,100};

inline StreamSettings stepCommon(StreamSettings s,StreamRow row,int direction) noexcept {
    if(row==StreamRow::resolution) {
        const CommonSize* pick=nullptr;
        for(const auto& size:commonSizes) {
            if(size.width>maxWidth(s)||size.height>maxHeight(s))continue;
            const long area=long(size.width)*size.height, current=long(s.width)*s.height;
            if(direction>0?area>current&&!pick:area<current)pick=&size;
        }
        if(pick){s.width=pick->width;s.height=pick->height;}
        return s;
    }
    const int* values=row==StreamRow::fps?commonFps:commonBitrates;
    const unsigned count=row==StreamRow::fps?4:6;
    const int current=row==StreamRow::fps?s.fps:s.bitrate_kbps/1000;
    int next=current;
    for(unsigned i=0;i<count;++i) {
        const int v=values[i];
        if(row==StreamRow::fps&&v>maxFps(s))continue;
        if(direction>0?v>current&&next==current:v<current)next=v;
    }
    if(row==StreamRow::fps)s.fps=next;else s.bitrate_kbps=next*1000;
    return s;
}
inline StreamSettings stepValue(StreamSettings s,StreamRow row,int direction) noexcept {
    if(row==StreamRow::fps)s.fps=std::clamp(s.fps+direction,30,maxFps(s));
    if(row==StreamRow::bitrate)s.bitrate_kbps=std::clamp((s.bitrate_kbps/1000+direction)*1000,4000,100000);
    return s;
}

struct NumberEdit {
    StreamRow row=StreamRow::count;
    char digits[9]{};
    unsigned length=0,cursor=0;
    bool open() const noexcept {return row!=StreamRow::count;}
};
inline NumberEdit beginEdit(StreamRow row,const StreamSettings& s) noexcept {
    NumberEdit e;e.row=row;
    if(row==StreamRow::resolution){std::snprintf(e.digits,sizeof(e.digits),"%04d%04d",std::clamp(s.width,0,9999),std::clamp(s.height,0,9999));e.length=8;}
    else if(row==StreamRow::fps){std::snprintf(e.digits,sizeof(e.digits),"%03d",std::clamp(s.fps,0,999));e.length=3;}
    else if(row==StreamRow::bitrate){std::snprintf(e.digits,sizeof(e.digits),"%03d",std::clamp(s.bitrate_kbps/1000,0,999));e.length=3;}
    else e.row=StreamRow::count;
    e.cursor=e.length?e.length-1:0;
    return e;
}
inline int editNumber(const NumberEdit& e,unsigned from,unsigned count) noexcept {
    int n=0;for(unsigned i=from;i<from+count;++i)n=n*10+(e.digits[i]-'0');return n;
}
inline StreamSettings applyEdit(const NumberEdit& e,StreamSettings s) noexcept {
    if(e.row==StreamRow::resolution){s.width=editNumber(e,0,4);s.height=editNumber(e,4,4);}
    if(e.row==StreamRow::fps)s.fps=editNumber(e,0,3);
    if(e.row==StreamRow::bitrate)s.bitrate_kbps=editNumber(e,0,3)*1000;
    return s;
}
inline void changeDigit(NumberEdit& e,int delta) noexcept {
    char& d=e.digits[e.cursor];
    d=static_cast<char>('0'+((d-'0'+delta)%10+10)%10);
}
inline void setEdit(NumberEdit& e,const StreamSettings& s) noexcept {
    const unsigned cursor=e.cursor;e=beginEdit(e.row,s);e.cursor=std::min(cursor,e.length-1);
}

inline const char* settingsProblem(const StreamSettings& s,bool available) noexcept {
    switch(validateSettings(s)) {
    case SettingsError::dimensions: return s.hardware()?"Resolution must be even, 320\xC3\x97" "180 to 3840\xC3\x97" "2160":"Software decoding allows even sizes up to 1920\xC3\x97" "1080";
    case SettingsError::fps: return s.hardware()?"Frame rate must be 30 to 120":"Software decoding allows 30 to 60 FPS";
    case SettingsError::bitrate: return "Bitrate limit must be 4 to 100 Mb/s";
    case SettingsError::network: return "Hardware H.264 needs fixed-resolution streaming";
    case SettingsError::mode: case SettingsError::quality: return "These settings aren\xE2\x80\x99t valid";
    case SettingsError::none: break;
    }
    return available?nullptr:"Not qualified for this PS5 and output";
}
}
