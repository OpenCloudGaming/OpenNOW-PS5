// SPDX-License-Identifier: GPL-3.0-or-later
#include "font.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x) ((void)0)
#define STBTT_ifloor(x) static_cast<int>(std::floor(x))
#define STBTT_iceil(x) static_cast<int>(std::ceil(x))
#define STBTT_sqrt(x) std::sqrt(x)
#define STBTT_pow(x, y) std::pow(x, y)
#define STBTT_fmod(x, y) std::fmod(x, y)
#define STBTT_cos(x) std::cos(x)
#define STBTT_acos(x) std::acos(x)
#define STBTT_fabs(x) std::fabs(x)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "../vendor/stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace opennow::ui {
namespace {
using ps5::demo::Canvas;
using ps5::demo::Color;
constexpr const char* files[]={"Nunito-SemiBold.ttf","Nunito-Bold.ttf","Nunito-ExtraBold.ttf","Nunito-Black.ttf","OpenNOWMono-Medium.ttf"};
constexpr unsigned faceCount=static_cast<unsigned>(Face::count);
struct Loaded { unsigned char* data=nullptr; stbtt_fontinfo info{}; int ascent=0, descent=0; };
Loaded faces[faceCount];
bool attempted=false, ready=false;

struct Glyph { unsigned key=0; int glyph=0; int x=0, y=0; unsigned width=0, height=0, offset=0; bool used=false; };
constexpr unsigned slots=4096;
constexpr std::size_t arenaBytes=std::size_t(6)<<20;
Glyph cache[slots];
unsigned char* arena=nullptr;
std::size_t arenaUsed=0;
unsigned cached=0;

unsigned char* readFile(const char* path) noexcept {
    FILE* file=std::fopen(path,"rb");
    if(!file)return nullptr;
    unsigned char* data=nullptr;
    if(std::fseek(file,0,SEEK_END)==0) {
        const long size=std::ftell(file);
        if(size>0&&size<(4<<20)&&std::fseek(file,0,SEEK_SET)==0) {
            data=static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(size)));
            if(data&&std::fread(data,1,static_cast<std::size_t>(size),file)!=static_cast<std::size_t>(size)){std::free(data);data=nullptr;}
        }
    }
    std::fclose(file);
    return data;
}

Loaded& at(Face face) noexcept {return faces[static_cast<unsigned>(face)];}
float scaleFor(Face face,float size) noexcept {return stbtt_ScaleForMappingEmToPixels(&at(face).info,size);}

int glyphFor(Face face,char32_t codepoint) noexcept {
    const auto& info=at(face).info;
    int glyph=stbtt_FindGlyphIndex(&info,static_cast<int>(codepoint));
    if(!glyph&&codepoint>=0x2000&&codepoint<0x2070&&codepoint!=0x2026)glyph=stbtt_FindGlyphIndex(&info,' ');
    if(!glyph&&codepoint>' ')glyph=stbtt_FindGlyphIndex(&info,'?');
    return glyph;
}

const Glyph* rasterize(Face face,float size,int glyph) noexcept {
    const unsigned sizeKey=static_cast<unsigned>(size*4+0.5f);
    const unsigned key=(static_cast<unsigned>(face)<<28)^(sizeKey<<16)^static_cast<unsigned>(glyph);
    unsigned slot=(key*2654435761U)%slots;
    for(unsigned probe=0;probe<slots;++probe,slot=(slot+1)%slots) {
        Glyph& entry=cache[slot];
        if(entry.used&&entry.key==key&&entry.glyph==glyph)return &entry;
        if(!entry.used)break;
    }
    if(!arena)arena=static_cast<unsigned char*>(std::malloc(arenaBytes));
    if(!arena)return nullptr;
    const float scale=scaleFor(face,size);
    int x0=0,y0=0,x1=0,y1=0;
    stbtt_GetGlyphBitmapBox(&at(face).info,glyph,scale,scale,&x0,&y0,&x1,&y1);
    const unsigned width=x1>x0?static_cast<unsigned>(x1-x0):0, height=y1>y0?static_cast<unsigned>(y1-y0):0;
    const std::size_t bytes=std::size_t(width)*height;
    if(bytes>arenaBytes/4)return nullptr;
    if(cached>slots*3/4||arenaUsed+bytes>arenaBytes) {
        for(auto& entry:cache)entry=Glyph{};
        arenaUsed=0;cached=0;
        slot=(key*2654435761U)%slots;
    }
    while(cache[slot].used)slot=(slot+1)%slots;
    Glyph& entry=cache[slot];
    entry=Glyph{key,glyph,x0,y0,width,height,static_cast<unsigned>(arenaUsed),true};
    if(bytes)stbtt_MakeGlyphBitmap(&at(face).info,arena+arenaUsed,static_cast<int>(width),static_cast<int>(height),static_cast<int>(width),scale,scale,glyph);
    arenaUsed+=bytes;++cached;
    return &entry;
}

unsigned bitmapScale(float size) noexcept {return size<26?3U:size<44?4U:size<80?6U:size<140?10U:16U;}
std::string_view upperFallback(std::string_view text,char* buffer,std::size_t capacity) noexcept {
    std::size_t length=0;
    for(std::size_t i=0;i<text.size()&&length+1<capacity;) {
        const char32_t cp=nextCodepoint(text,i);
        char c=cp<0x80?static_cast<char>(cp):'?';
        if(c>='a'&&c<='z')c=static_cast<char>(c-32);
        buffer[length++]=c;
    }
    return {buffer,length};
}

template<typename Visit>
float layout(Face face,float size,std::string_view text,float tracking,Visit visit) noexcept {
    const float scale=scaleFor(face,size);
    float pen=0;int previous=0;
    for(std::size_t i=0;i<text.size();) {
        const int glyph=glyphFor(face,nextCodepoint(text,i));
        if(previous)pen+=scale*static_cast<float>(stbtt_GetGlyphKernAdvance(&at(face).info,previous,glyph));
        visit(glyph,pen);
        int advance=0,bearing=0;
        stbtt_GetGlyphHMetrics(&at(face).info,glyph,&advance,&bearing);
        pen+=scale*static_cast<float>(advance)+tracking*size;
        previous=glyph;
    }
    return text.empty()?0:pen-tracking*size;
}
}

bool loadFonts() noexcept {
    if(attempted)return ready;
    attempted=true;
    for(unsigned i=0;i<faceCount;++i) {
        char path[160];
        std::snprintf(path,sizeof(path),"/app0/assets/fonts/%s",files[i]);
        faces[i].data=readFile(path);
#ifdef OPENNOW_HOST_PREVIEW
        if(!faces[i].data){std::snprintf(path,sizeof(path),"assets/fonts/%s",files[i]);faces[i].data=readFile(path);}
#endif
        if(!faces[i].data||!stbtt_InitFont(&faces[i].info,faces[i].data,stbtt_GetFontOffsetForIndex(faces[i].data,0)))return false;
        int gap=0;stbtt_GetFontVMetrics(&faces[i].info,&faces[i].ascent,&faces[i].descent,&gap);
    }
    ready=true;
    return true;
}
bool fontsLoaded() noexcept {return ready;}

char32_t nextCodepoint(std::string_view text,std::size_t& at) noexcept {
    const auto byte=[&](std::size_t i){return static_cast<unsigned char>(text[i]);};
    const unsigned char lead=byte(at);
    unsigned length=lead<0x80?1:(lead>>5)==6?2:(lead>>4)==14?3:(lead>>3)==30?4:0;
    if(!length||at+length>text.size()){++at;return 0xFFFD;}
    char32_t cp=length==1?lead:length==2?lead&31:length==3?lead&15:lead&7;
    for(unsigned i=1;i<length;++i) {
        if((byte(at+i)&0xC0)!=0x80){++at;return 0xFFFD;}
        cp=(cp<<6)|(byte(at+i)&63);
    }
    at+=length;
    if((length==2&&cp<0x80)||(length==3&&cp<0x800)||(length==4&&(cp<0x10000||cp>0x10FFFF))||(cp>=0xD800&&cp<0xE000))return 0xFFFD;
    return cp;
}

float ascent(Face face,float size) noexcept {return ready?scaleFor(face,size)*static_cast<float>(at(face).ascent):size*0.8f;}
float descent(Face face,float size) noexcept {return ready?-scaleFor(face,size)*static_cast<float>(at(face).descent):size*0.2f;}

float textWidth(Face face,float size,std::string_view text,float tracking) noexcept {
    if(!ready) {
        std::size_t count=0;for(std::size_t i=0;i<text.size();++count)nextCodepoint(text,i);
        return static_cast<float>(count*6*bitmapScale(size));
    }
    return layout(face,size,text,tracking,[](int,float){});
}

float drawText(Canvas& canvas,Face face,float size,float x,float baseline,std::string_view text,Color color,float tracking,unsigned alpha) noexcept {
    if(!ready) {
        char buffer[256];const auto upper=upperFallback(text,buffer,sizeof(buffer));
        const unsigned scale=bitmapScale(size);
        canvas.text(static_cast<unsigned>(std::max(x,0.0f)),static_cast<unsigned>(std::max(baseline-7.0f*scale,0.0f)),upper,scale,color);
        return static_cast<float>(upper.size()*6*scale);
    }
    return layout(face,size,text,tracking,[&](int glyph,float pen){
        const Glyph* g=rasterize(face,size,glyph);
        if(!g||!g->width)return;
        canvas.coverage(static_cast<int>(std::lround(x+pen))+g->x,static_cast<int>(std::lround(baseline))+g->y,g->width,g->height,arena+g->offset,g->width,color,alpha);
    });
}

std::size_t fittingPrefix(Face face,float size,std::string_view text,float maxWidth,float tracking) noexcept {
    std::size_t best=0;
    for(std::size_t i=0;i<text.size();) {
        nextCodepoint(text,i);
        if(textWidth(face,size,text.substr(0,i),tracking)>maxWidth)break;
        best=i;
    }
    return best;
}

float drawFitted(Canvas& canvas,Face face,float size,float x,float baseline,std::string_view text,float maxWidth,Color color,float tracking,unsigned alpha) noexcept {
    if(textWidth(face,size,text,tracking)<=maxWidth+0.5f)return drawText(canvas,face,size,x,baseline,text,color,tracking,alpha);
    constexpr std::string_view ellipsis="\xE2\x80\xA6";
    const float room=maxWidth-textWidth(face,size,ellipsis,tracking);
    std::size_t cut=fittingPrefix(face,size,text,room,tracking);
    while(cut&&text[cut-1]==' ')--cut;
    const float width=drawText(canvas,face,size,x,baseline,text.substr(0,cut),color,tracking,alpha);
    return width+drawText(canvas,face,size,x+width+tracking*size,baseline,ellipsis,color,tracking,alpha);
}

unsigned drawWrapped(Canvas& canvas,Face face,float size,float x,float top,float lineHeight,std::string_view text,float maxWidth,unsigned maxLines,Color color,unsigned alpha,bool draw) noexcept {
    const float baseline=top+(lineHeight-(ascent(face,size)+descent(face,size)))/2+ascent(face,size);
    unsigned line=0;
    while(!text.empty()&&line<maxLines) {
        while(!text.empty()&&text.front()==' ')text.remove_prefix(1);
        if(text.empty())break;
        std::size_t take=fittingPrefix(face,size,text,maxWidth);
        if(take<text.size()) {
            std::size_t space=text.substr(0,take+1).rfind(' ');
            if(space!=std::string_view::npos&&space>0)take=space;
            if(!take){std::size_t next=0;nextCodepoint(text,next);take=next;}
        }
        const bool last=line+1==maxLines;
        if(draw) {
            if(last&&take<text.size())drawFitted(canvas,face,size,x,baseline+line*lineHeight,text,maxWidth,color,0,alpha);
            else drawText(canvas,face,size,x,baseline+line*lineHeight,text.substr(0,take),color,0,alpha);
        }
        text.remove_prefix(last?text.size():take);
        ++line;
    }
    return line;
}
}
