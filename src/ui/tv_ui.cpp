// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv_ui.hpp"
#include "font.hpp"
#include "../vendor/qrcodegen.h"
#include "../version.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
#include "artwork_disk.hpp"
#include "motion.hpp"
#include "store_marks.hpp"
#include <utility>

namespace opennow::ui {
namespace {
using ps5::demo::Canvas;
using ps5::demo::Color;
using ps5::demo::Clip;
using std::string_view;

constexpr Color rgb(unsigned hex) {
    return static_cast<Color>(0xff000000U|((hex&0xffU)<<16)|(hex&0xff00U)|((hex>>16)&0xffU));
}
constexpr Color shell=rgb(0x0B0F1A), face=rgb(0xFFFFFF), faceText=rgb(0x111827), accent=rgb(0x7FD4FF);
constexpr Color muted=rgb(0xA7A9AD), dim=rgb(0x6B6F79), black=rgb(0x000000);
constexpr Color mint=rgb(0x6EE7B7), violet=rgb(0xA78BFA), yellow=rgb(0xFFD166), coral=rgb(0xFF8A80);
constexpr Color sheetFill=rgb(0x111522);
constexpr unsigned glassAlpha=184, strongAlpha=240, seamAlpha=36;
constexpr float hintY=984;

Color mix(Color a, Color b, float t) {
    unsigned out=0xff000000U;
    for(unsigned shift:{0U,8U,16U}) {
        const float x=static_cast<float>((static_cast<unsigned>(a)>>shift)&255U), y=static_cast<float>((static_cast<unsigned>(b)>>shift)&255U);
        out|=static_cast<unsigned>(x+(y-x)*t+0.5f)<<shift;
    }
    return static_cast<Color>(out);
}
unsigned alphaOf(float opacity) {return static_cast<unsigned>(std::clamp(opacity,0.0f,1.0f)*255+0.5f);}

float baselineIn(Face face, float size, float top, float lineHeight) {
    return top+(lineHeight-(ascent(face,size)+descent(face,size)))/2+ascent(face,size);
}
float label(Canvas& c, Face face, float size, float x, float centerY, string_view text, Color color, float tracking=0, unsigned alpha=255) {
    return drawText(c,face,size,x,baselineIn(face,size,centerY-size*0.7f,size*1.4f),text,color,tracking,alpha);
}
float fitted(Canvas& c, Face face, float size, float x, float centerY, string_view text, float maxWidth, Color color, float tracking=0) {
    return drawFitted(c,face,size,x,baselineIn(face,size,centerY-size*0.7f,size*1.4f),text,maxWidth,color,tracking);
}
float labelRight(Canvas& c, Face face, float size, float rightX, float centerY, string_view text, Color color, float tracking=0) {
    const float w=textWidth(face,size,text,tracking);
    label(c,face,size,rightX-w,centerY,text,color,tracking);
    return w;
}
float labelCenter(Canvas& c, Face face, float size, float centerX, float centerY, string_view text, Color color, float tracking=0) {
    const float w=textWidth(face,size,text,tracking);
    label(c,face,size,centerX-w/2,centerY,text,color,tracking);
    return w;
}
void upper(char* text) {for(char* p=text;*p;++p)if(*p>='a'&&*p<='z')*p=static_cast<char>(*p-32);}
void eyebrow(Canvas& c, float x, float centerY, string_view text, Color color=muted, float size=14, float maxWidth=1600) {
    char buffer[256];std::snprintf(buffer,sizeof(buffer),"%.*s",static_cast<int>(text.size()),text.data());upper(buffer);
    fitted(c,Face::mono,size,x,centerY,buffer,maxWidth,color,0.12f);
}

void panel(Canvas& c, float x, float y, float w, float h, float radius, bool strong=false) {
    c.roundRect(x,y,w,h,radius,shell,strong?strongAlpha:glassAlpha);
    c.roundRectStroke(x,y,w,h,radius,1,face,seamAlpha);
}
void focusFrame(Canvas& c, float x, float y, float w, float h, float radius, unsigned halo=115) {
    c.roundRectStroke(x-5,y-5,w+10,h+10,radius+5,5,accent,halo);
    c.roundRectStroke(x,y,w,h,radius,3,face);
}
void rule(Canvas& c, float x, float y, float w) {c.roundRect(x,y,w,1,0,face,seamAlpha);}
void dot(Canvas& c, float cx, float cy, Color color) {c.disc(cx,cy,4.5f,color);}

enum Anim : std::uint32_t { focusAnim=1, zoomAnim, coverAnim, scrollAnim, ringAnim, pageAnim, sectionAnim, paneAnim,
    sheetAnim, toggleAnim, segmentAnim, sliderAnim, barAnim, navAnim, backdropAnim, railAnim };
float follow(const Model& m, std::uint32_t key, float target, float omega, float epsilon=0.05f) {
    return m.motion?m.motion->follow(key,target,omega,target,epsilon):target;
}
float reveal(const Model& m, std::uint32_t key, float omega) {
    return m.motion?m.motion->follow(key,1,omega,0,0.002f):1;
}
float loopPhase(const Model& m, float period) {
    if(!m.motion||m.streaming)return 0;
    m.motion->keepAlive();
    return std::fmod(m.motion->seconds(),period)/period;
}
std::uint32_t pageKey(const Model& m) {
    const Screen page=m.screen==Screen::search?Screen::grid:m.screen;
    return motionKey(pageAnim,static_cast<std::uint32_t>(page),static_cast<std::uint32_t>(m.section));
}
void layer(Canvas& c, float opacity, float dx, float dy) {
    c.setOpacity(alphaOf(opacity));
    c.setOffset(dx,dy);
}
struct Layer { float opacity, dx, dy; };
Layer pageLayer(Canvas& c, const Model& m) {
    const float section=static_cast<float>(m.section);
    const float travel=follow(m,motionKey(sectionAnim),section,22,0.001f)-section;
    const float p=reveal(m,pageKey(m),30);
    const Layer l{std::min(1-0.5f*std::min(std::abs(travel),1.0f),0.65f+0.35f*p),-travel*64,10*(1-p)};
    layer(c,l.opacity,l.dx,l.dy);
    return l;
}
void focusRing(Canvas& c, const Model& m, unsigned slot, float x, float y, float w, float h, float radius, unsigned halo=115) {
    const std::uint32_t key=motionKey(focusAnim,pageKey(m),slot);
    constexpr float omega=36;
    focusFrame(c,follow(m,key,x,omega),follow(m,key+1,y,omega),follow(m,key+2,w,omega),follow(m,key+3,h,omega),radius,halo);
}

struct StoreInfo { marks::Store key; const char* name; Color color; };
StoreInfo storeInfo(string_view store) {
    using marks::Store;
    switch(marks::storeKey(store)) {
    case Store::steam: return {Store::steam,"Steam",rgb(0x26364A)};
    case Store::epic: return {Store::epic,"Epic Games",rgb(0x1B1B1F)};
    case Store::ubisoft: return {Store::ubisoft,"Ubisoft Connect",rgb(0x3A5BD9)};
    case Store::battlenet: return {Store::battlenet,"Battle.net",rgb(0x2E7CB8)};
    case Store::xbox: return {Store::xbox,store.find("MICROSOFT")!=string_view::npos?"Microsoft Store":"Xbox",rgb(0x107C41)};
    case Store::gog: return {Store::gog,"GOG",rgb(0x7B3FE4)};
    case Store::ea: return {Store::ea,"EA app",rgb(0x252A35)};
    case Store::nvidia: return {Store::nvidia,"NVIDIA",rgb(0x252A35)};
    case Store::gaijin: return {Store::gaijin,"Gaijin",rgb(0x252A35)};
    case Store::none: break;
    }
    return {Store::none,nullptr,rgb(0x252A35)};
}
const char* storeName(const char* store) {const auto info=storeInfo(store);return info.name?info.name:store;}
void storeMark(Canvas& c, float cx, float cy, float size, string_view store) {
    const auto info=storeInfo(store);
    c.disc(cx,cy,size/2,info.color);
    c.ring(cx,cy,size/2-0.5f,1,face,46);
    const unsigned side=static_cast<unsigned>(std::lround(size*0.56f));
    if(const auto* mask=marks::mask(info.key,side)) {
        c.coverage(static_cast<int>(std::lround(cx-side/2.0f)),static_cast<int>(std::lround(cy-side/2.0f)),side,side,mask->pixels,side,rgb(0xE8F2EA));
        return;
    }
    char letter[2]{};
    if(!store.empty())letter[0]=static_cast<char>(store[0]>='a'&&store[0]<='z'?store[0]-32:store[0]);
    labelCenter(c,Face::black,std::round(size*0.42f),cx,cy,letter,face);
}
float storeChipWidth(const char* store, float markSize=30, float inset=5) {return inset+markSize+10+textWidth(Face::extrabold,16,storeName(store))+16;}
float storeChip(Canvas& c, float x, float cy, const char* store, float markSize=30, float inset=5) {
    const float w=storeChipWidth(store,markSize,inset), h=markSize+inset*2;
    c.roundRect(x,cy-h/2,w,h,h/2,face,18);
    c.roundRectStroke(x,cy-h/2,w,h,h/2,1,face,seamAlpha);
    storeMark(c,x+inset+markSize/2,cy,markSize,store);
    label(c,Face::extrabold,16,x+inset+markSize+10,cy,storeName(store),face);
    return w;
}

enum class Glyph { none, cross, circle, square, triangle, dpad, options, touchpad, stick, l1, r1, l2, r2, r3 };
bool badge(Glyph g) {return g==Glyph::l1||g==Glyph::r1||g==Glyph::l2||g==Glyph::r2||g==Glyph::r3;}
float glyphWidth(Glyph g, float size=30) {return g==Glyph::none?0:g==Glyph::touchpad?size*1.45f:badge(g)?size*1.35f:size;}
float glyph(Canvas& c, Glyph g, float x, float cy, float size=30, Color fill=face, Color ink=faceText) {
    const float w=glyphWidth(g,size), cx=x+w/2, s=size, t=std::max(2.0f,s*0.085f);
    if(g==Glyph::none)return 0;
    if(badge(g)) {
        const float h=s*0.82f;
        c.roundRect(x,cy-h/2,w,h,std::min(7.0f,h/2),fill);
        const char* text=g==Glyph::l1?"L1":g==Glyph::r1?"R1":g==Glyph::l2?"L2":g==Glyph::r2?"R2":"R3";
        labelCenter(c,Face::black,std::round(s*0.43f),cx,cy,text,ink);
        return w;
    }
    if(g==Glyph::touchpad) {
        const float h=s*0.82f;
        c.roundRect(x,cy-h/2,w,h,std::min(7.0f,h/2),fill);
        c.roundRectStroke(x+w*0.2f,cy-h*0.24f,w*0.6f,h*0.48f,3,t*0.8f,ink);
        return w;
    }
    c.disc(cx,cy,s/2,fill);
    switch(g) {
    case Glyph::cross: {const float a=s*0.2f;c.line(cx-a,cy-a,cx+a,cy+a,t,ink);c.line(cx+a,cy-a,cx-a,cy+a,t,ink);break;}
    case Glyph::circle: c.ring(cx,cy,s*0.23f,t,ink);break;
    case Glyph::square: {const float a=s*0.42f;c.roundRectStroke(cx-a/2,cy-a/2,a,a,s*0.05f,t,ink);break;}
    case Glyph::triangle: {
        const float p[]={cx,cy-s*0.22f,cx+s*0.23f,cy+s*0.17f,cx-s*0.23f,cy+s*0.17f};
        for(int i=0;i<3;++i)c.line(p[i*2],p[i*2+1],p[(i+1)%3*2],p[(i+1)%3*2+1],t,ink);
        break;
    }
    case Glyph::options: for(float row:{-0.15f,0.0f,0.15f})c.line(cx-s*0.2f,cy+row*s,cx+s*0.2f,cy+row*s,t*0.9f,ink);break;
    case Glyph::dpad: {
        const float a=s*0.13f, b=s*0.33f;
        c.roundRect(cx-a,cy-b,2*a,2*b,a*0.6f,ink);c.roundRect(cx-b,cy-a,2*b,2*a,a*0.6f,ink);
        c.disc(cx,cy,a*0.55f,fill);
        break;
    }
    case Glyph::stick: c.ring(cx,cy,s*0.27f,t,ink);c.disc(cx,cy,s*0.1f,ink);break;
    default: break;
    }
    return w;
}

struct Hint { Glyph glyph; const char* text; Glyph second=Glyph::none; const char* prefix=nullptr; Color ink=face; const char* join="+"; };
constexpr float hintSize=30, hintLabel=17;
float hintWidth(const Hint& h, float size=hintSize, float text=hintLabel) {
    float w=glyphWidth(h.glyph,size)+10+textWidth(Face::extrabold,text,h.text);
    if(h.prefix)w+=textWidth(Face::extrabold,text,h.prefix)+8;
    if(h.second!=Glyph::none)w+=6+textWidth(Face::black,text,h.join)+6+glyphWidth(h.second,size);
    return w;
}
float drawHint(Canvas& c, const Hint& h, float x, float cy, float size=hintSize, float text=hintLabel) {
    const float start=x;
    if(h.prefix)x+=label(c,Face::extrabold,text,x,cy,h.prefix,muted)+8;
    x+=glyph(c,h.glyph,x,cy,size);
    if(h.second!=Glyph::none){x+=6;x+=label(c,Face::black,text,x,cy,h.join,muted)+6;x+=glyph(c,h.second,x,cy,size);}
    x+=10;
    x+=label(c,Face::extrabold,text,x,cy,h.text,h.ink);
    return x-start;
}
float hintRowWidth(const Hint* items, unsigned count, float gap=22) {
    float w=0;for(unsigned i=0;i<count;++i)w+=hintWidth(items[i])+(i?gap:0);return w;
}
void hintRow(Canvas& c, const Hint* items, unsigned count, float x, float cy=hintY, float gap=22) {
    for(unsigned i=0;i<count;++i)x+=drawHint(c,items[i],x,cy)+gap;
}

enum class NavIcon { library, store, settings };
void navIcon(Canvas& c, NavIcon icon, float cx, float cy, bool onLight) {
    switch(icon) {
    case NavIcon::library: {
        const Color tone=onLight?rgb(0x12638C):accent;
        for(int i=0;i<4;++i)c.roundRect(cx-11+(i%2)*12,cy-11+(i/2)*12,10,10,3,tone);
        break;
    }
    case NavIcon::store: {
        const Color tone=onLight?rgb(0x806017):yellow;
        c.roundRect(cx-13,cy-8,26,15,7,tone);
        c.disc(cx-8,cy+5,5,tone);c.disc(cx+8,cy+5,5,tone);
        const Color ink=onLight?face:shell;
        c.roundRect(cx-10,cy-2,6,2,1,ink);c.roundRect(cx-8,cy-4,2,6,1,ink);
        c.disc(cx+6,cy-2,1.6f,ink);c.disc(cx+9,cy+1,1.6f,ink);
        break;
    }
    case NavIcon::settings: {
        const Color tone=onLight?rgb(0xA9362C):coral;
        for(int i=0;i<8;++i){const float a=static_cast<float>(i)*0.785398f;c.line(cx+std::cos(a)*6,cy+std::sin(a)*6,cx+std::cos(a)*12,cy+std::sin(a)*12,5,tone);}
        c.disc(cx,cy,9,tone);c.disc(cx,cy,3.6f,onLight?face:shell);
        break;
    }
    }
}
void navPill(Canvas& c, const Model& m) {
    const Section section=m.section;
    struct Destination { NavIcon icon; const char* name; Section section; };
    constexpr Destination items[]={{NavIcon::library,"Library",Section::library},{NavIcon::store,"Store",Section::browse},{NavIcon::settings,"Settings",Section::settings}};
    float row=glyphWidth(Glyph::l1)+8+8+glyphWidth(Glyph::r1)+4*4;
    for(const auto& item:items)row+=item.section==section?30+10+textWidth(Face::black,17,item.name)+34:64;
    const float w=row+30, x=960-w/2, y=948, cy=y+36;
    panel(c,x,y,w,72,36,true);
    float at=x+15;
    at+=glyph(c,Glyph::l1,at,cy)+4+8;
    for(float slot=at;const auto& item:items) {
        const float iw=item.section==section?30+10+textWidth(Face::black,17,item.name)+34:64;
        if(item.section==section)c.roundRect(follow(m,motionKey(navAnim),slot,30),cy-26,follow(m,motionKey(navAnim,1),iw,30),52,26,face);
        slot+=iw+4;
    }
    for(const auto& item:items) {
        const bool current=item.section==section;
        const float iw=current?30+10+textWidth(Face::black,17,item.name)+34:64;
        if(current) {
            navIcon(c,item.icon,at+14+15,cy,true);
            label(c,Face::black,17,at+14+30+10,cy,item.name,faceText);
        } else navIcon(c,item.icon,at+32,cy,false);
        at+=iw+4;
    }
    at+=8;
    glyph(c,Glyph::r1,at,cy);
}

void chrome(Canvas& c, const Model& m, string_view title, bool nav, const Hint* left, unsigned leftCount, const Hint* right, unsigned rightCount, bool bottom=true) {
    layer(c,1,0,0);
    const bool signedIn=m.login.state==State::authenticated;
    const char* name=signedIn?"NVIDIA account":"Guest";
    const char* tag=signedIn?nullptr:"SIGNED OUT";
    float rowW=42+12+std::min(200.0f,textWidth(Face::extrabold,19,name));
    if(tag)rowW+=12+textWidth(Face::mono,13,tag,0.08f)+20;
    const float pw=std::min(460.0f,rowW+32);
    panel(c,64,36,pw,60,30,true);
    c.disc(64+10+21,66,21,violet);
    c.ring(64+10+21,66,20,2,face,178);
    labelCenter(c,Face::black,18,64+10+21,66,signedIn?"N":"G",faceText);
    float x=64+10+42+12;
    x+=fitted(c,Face::extrabold,19,x,66,name,200,face)+12;
    if(tag) {
        const float bw=textWidth(Face::mono,13,tag,0.08f)+20;
        c.roundRect(x,54,bw,24,12,violet,46);
        label(c,Face::mono,13,x+10,66,tag,violet,0.08f);
    }
    const char* status=signedIn?(m.login.sessionSaved?"Login saved":"Login not saved"):"Not signed in";
    const char* build="PS5 \xC2\xB7 ALPHA";
    const float statusRow=9+8+textWidth(Face::mono,15,status)+18+1+18+textWidth(Face::mono,15,build);
    const float sw=statusRow+50, sx=1856-sw;
    panel(c,sx,36,sw,60,30,true);
    x=sx+25;
    dot(c,x+4.5f,66,signedIn?(m.login.sessionSaved?mint:yellow):muted);
    x+=9+8;
    x+=label(c,Face::mono,15,x,66,status,face)+18;
    c.roundRect(x,55,1,22,0,face,seamAlpha);
    label(c,Face::mono,15,x+1+18,66,build,face);
    if(!title.empty()) {
        const float gap=64+pw, room=sx-gap-80;
        const float tw=std::min(textWidth(Face::black,22,title,0.01f),room);
        fitted(c,Face::black,22,gap+(sx-gap-tw)/2,66,title,room,face,0.01f);
    }
    if(!bottom)return;
    if(nav)navPill(c,m);
    hintRow(c,left,leftCount,64);
    hintRow(c,right,rightCount,1856-hintRowWidth(right,rightCount));
}

struct Button { Glyph glyph; const char* text; bool primary=false, danger=false; };
float buttonWidth(const Button& b, float labelSize=22, float glyphSize=32) {
    const float content=(b.glyph!=Glyph::none?glyphWidth(b.glyph,glyphSize)+14:0)+textWidth(Face::black,labelSize,b.text);
    return std::max(160.0f,content+(b.glyph!=Glyph::none?18:24)+24+2);
}
float actionButton(Canvas& c, const Button& b, float x, float y, float h=72, float w=0, float labelSize=22, float glyphSize=32, bool enabled=true) {
    if(w<=0)w=buttonWidth(b,labelSize,glyphSize);
    const float r=std::min(26.0f,h/2);
    const Color ink=b.primary?faceText:b.danger?coral:face;
    if(b.primary)c.roundRect(x,y,w,h,r,face,enabled?255:107);
    else if(b.danger){c.roundRect(x,y,w,h,r,coral,enabled?31:13);c.roundRectStroke(x,y,w,h,r,1.5f,coral,153);}
    else {c.roundRect(x,y,w,h,r,face,18);c.roundRectStroke(x,y,w,h,r,1,face,seamAlpha);}
    float at=x+(b.glyph!=Glyph::none?18:24);
    const float cy=y+h/2;
    if(b.glyph!=Glyph::none)at+=glyph(c,b.glyph,at,cy,glyphSize,b.primary?faceText:b.danger?coral:face,b.primary?face:faceText)+14;
    fitted(c,Face::black,labelSize,at,cy,b.text,x+w-24-at,enabled?ink:mix(ink,b.primary?face:shell,0.5f));
    return w;
}

float lerp(float a, float b, float t) {return a+(b-a)*t;}
void screenBase(Canvas& c, Color tint) {
    const Color base=mix(shell,tint,0.12f);
    for(unsigned row=0;row<1080;++row) {
        const Color color=row<432?mix(base,accent,0.07f*(1-row/432.0f)):base;
        c.rectangle(0,row,1920,1,color);
    }
}
bool wantHero(const Model& m, const Game* game) {
    return game&&m.art&&*game->hero&&m.art->want(game->hero,art::Kind::hero,m.frame)==art::State::ready;
}
enum class Backdrop : std::uint32_t { screen, detail, stage };
constexpr unsigned backdropWidth=art::heroWidth, backdropHeight=art::heroHeight;
struct BackdropFrame { std::uint32_t key=0; std::uint32_t pixels[backdropWidth*backdropHeight]; };
BackdropFrame backdrops[2];
unsigned shownBackdrop=0;
unsigned backdropColumn(Backdrop style, int x) {
    const float fx=static_cast<float>(x);
    switch(style) {
    case Backdrop::screen: {
        const float t=fx/1919.0f;
        const float scrim=t<0.48f?lerp(1.0f,0.60f,t/0.48f):lerp(0.60f,0.88f,(t-0.48f)/0.52f);
        return alphaOf(0.58f*0.88f*(1-scrim));
    }
    case Backdrop::detail: {
        if(x<653)return 0;
        const float t=(fx-653)/1267;
        const float scrim=t<0.32f?lerp(1.0f,0.55f,t/0.32f):t<0.6f?lerp(0.55f,0.0f,(t-0.32f)/0.28f):0.0f;
        return alphaOf(1-scrim);
    }
    case Backdrop::stage: {
        if(x<576)return 0;
        const float t=fx/1920.0f;
        const float scrim=t<0.34f?1.0f:t<0.62f?lerp(1.0f,0.66f,(t-0.34f)/0.28f):lerp(0.66f,0.34f,(t-0.62f)/0.38f);
        return alphaOf(0.62f*(1-scrim));
    }
    }
    return 0;
}
float backdropRow(Backdrop style, int y) {
    const float fy=static_cast<float>(y);
    return style==Backdrop::detail?(y<800?1.0f:1-0.9f*(fy-800)/280.0f):style==Backdrop::stage?(y<756?1.0f:1-(fy-756)/324.0f):1.0f;
}
void buildBackdrop(BackdropFrame& frame, Backdrop style, Color tint, const Model& m, const char* hero) {
    static std::uint8_t columns[3][backdropWidth], rows[3][backdropHeight];
    static bool tables=false;
    if(!tables) {
        for(unsigned s=0;s<3;++s) {
            for(unsigned x=0;x<backdropWidth;++x)columns[s][x]=static_cast<std::uint8_t>(backdropColumn(static_cast<Backdrop>(s),static_cast<int>(x*2)));
            for(unsigned y=0;y<backdropHeight;++y)rows[s][y]=static_cast<std::uint8_t>(alphaOf(backdropRow(static_cast<Backdrop>(s),static_cast<int>(y*2))));
        }
        tables=true;
    }
    const unsigned styleIndex=static_cast<unsigned>(style);
    const Color base=style==Backdrop::screen?mix(shell,tint,0.12f):shell;
    const bool art=hero&&m.art->copy(hero,art::Kind::hero,frame.pixels);
    const int origin=style==Backdrop::detail?326:style==Backdrop::stage?288:0;
    static std::uint32_t source[backdropWidth];
    for(unsigned y=0;y<backdropHeight;++y) {
        const int row=static_cast<int>(y*2);
        const auto under=static_cast<std::uint32_t>(style==Backdrop::screen&&row<432?mix(base,accent,0.07f*(1-row/432.0f)):base);
        std::uint32_t* out=frame.pixels+std::size_t(y)*backdropWidth;
        if(art)std::memcpy(source,out,sizeof(source));
        for(unsigned x=0;x<backdropWidth;++x) {
            const int column=static_cast<int>(x*2), sourceX=(column-origin)/2;
            const unsigned a=art&&column>=origin?(columns[styleIndex][x]*rows[styleIndex][y]+127U)/255U:0;
            if(!a){out[x]=under;continue;}
            const std::uint32_t over=source[sourceX];
            std::uint32_t mixed=0xff000000U;
            for(unsigned shift:{0U,8U,16U})
                mixed|=((((over>>shift)&255U)*a+((under>>shift)&255U)*(255-a)+127)/255)<<shift;
            out[x]=mixed;
        }
    }
}
void backdrop(Canvas& c, const Model& m, Backdrop style, const Game* game, Color tint) {
    const char* hero=wantHero(m,game)?game->hero:nullptr;
    std::uint32_t name=2166136261U;
    for(const char* p=hero?hero:"";*p;++p)name=(name^static_cast<unsigned char>(*p))*16777619U;
    const std::uint32_t key=motionKey(backdropAnim+static_cast<std::uint32_t>(style),static_cast<std::uint32_t>(tint),name)|1U;
    if(backdrops[shownBackdrop].key!=key) {
        shownBackdrop^=1;
        if(backdrops[shownBackdrop].key!=key) {
            buildBackdrop(backdrops[shownBackdrop],style,tint,m,hero);
            backdrops[shownBackdrop].key=key;
        }
    }
    const auto& previous=backdrops[shownBackdrop^1];
    const float p=previous.key?reveal(m,key,14):1;
    c.imageDoubled(backdrops[shownBackdrop].pixels,backdropWidth,backdropHeight,alphaOf(p),p<1?previous.pixels:nullptr);
}
void screenBackground(Canvas& c, const Model& m, const Game* game, Color tint) {backdrop(c,m,Backdrop::screen,game,tint);}
void detailBackground(Canvas& c, const Model& m, const Game* game) {backdrop(c,m,Backdrop::detail,game,shell);}
void stageBackground(Canvas& c, const Model& m, const Game* game) {backdrop(c,m,Backdrop::stage,game,shell);}

constexpr float posterW=art::tileWidth, posterH=art::tileHeight, posterRadius=22;
void shimmer(Canvas& c, const Model& m, float x, float y, float w, float h, float radius) {
    const float t=loopPhase(m,1.4f);
    if(!m.motion||m.streaming)return;
    const Clip saved=c.clip();
    const float band=w*0.55f, center=x-band+(w+2*band)*t;
    for(int i=-2;i<=2;++i) {
        c.setClip(saved);
        c.setClip(center+i*band/5-band/10,y,band/5,h);
        c.roundRect(x,y,w,h,radius,face,static_cast<unsigned>(14-4*std::abs(i)));
    }
    c.setClip(saved);
}
void poster(Canvas& c, const Model& m, const Game& game, unsigned index, float x, float y, bool focused) {
    art::State state=art::State::missing;
    if(m.art&&*game.art)state=m.art->want(game.art,art::Kind::tile,m.frame);
    const bool ready=state==art::State::ready;
    const std::uint32_t section=static_cast<std::uint32_t>(m.section);
    const float zoom=follow(m,motionKey(zoomAnim,section,index),focused?1.04f:1.0f,46,0.0005f);
    const float shown=follow(m,motionKey(coverAnim,section,index),ready?1.0f:0.0f,14,0.004f);
    const float w=posterW*zoom, h=posterH*zoom, px=x-(w-posterW)/2, py=y-(h-posterH)/2, radius=posterRadius*zoom;
    if(shown<1) {
        c.roundRect(px,py,w,h,radius,storeInfo(game.store).color);
        if(state==art::State::loading)shimmer(c,m,px,py,w,h,radius);
        const unsigned lines=drawWrapped(c,Face::black,15,0,0,19,game.title,posterW-24,3,face,255,false);
        drawWrapped(c,Face::black,15,px+12,py+h-8-32-8-lines*19.0f,19,game.title,posterW-24,3,face,alphaOf(1-shown));
    }
    if(shown>0)m.art->draw(c,game.art,art::Kind::tile,px,py,w,h,radius,alphaOf(shown));
    c.scrim(px,py,w,h,radius,0.6f,black,alphaOf(0.82f));
    c.roundRect(px+8,py+h-8-32,32,32,16,black,140);
    storeMark(c,px+8+16,py+h-8-16,24,game.store);
    if(!focused)c.roundRectStroke(px,py,w,h,radius,1,face,seamAlpha);
}
void pageTile(Canvas& c, float x, float y, bool loading) {
    c.roundRect(x,y,posterW,posterH,posterRadius,face,5);
    c.roundRectStroke(x,y,posterW,posterH,posterRadius,2,face,97);
    const float cx=x+posterW/2, cy=y+posterH/2-14;
    if(loading){for(int i=0;i<3;++i)c.disc(cx-14+i*14.0f,cy,4,face,i==0?255:90);}
    else {c.roundRect(cx-10,cy-1.5f,20,3,1.5f,face);c.roundRect(cx-1.5f,cy-10,3,20,1.5f,face);}
    labelCenter(c,Face::extrabold,15,cx,cy+30,loading?"Loading\xE2\x80\xA6":"Next page",face);
}
struct GridLayout { float x, y, stepX, stepY, clip; unsigned columns; };
void posterGrid(Canvas& c, const Model& m, const CloudView& v, unsigned focus, const GridLayout& g) {
    const unsigned focusRow=focus/g.columns, top=focusRow>0?focusRow-1:0;
    const bool pageTileShown=v.hasNext||v.state==CloudState::loading;
    const unsigned slots=v.count+(pageTileShown?1:0), rows=(slots+g.columns-1)/g.columns;
    const std::uint32_t section=static_cast<std::uint32_t>(m.section);
    constexpr float rowOmega=22;
    const float scroll=follow(m,motionKey(scrollAnim,section),static_cast<float>(top),rowOmega,0.002f);
    const float ringRow=follow(m,motionKey(ringAnim,section),static_cast<float>(focusRow),rowOmega,0.002f);
    const float ringColumn=follow(m,motionKey(ringAnim,section,1),static_cast<float>(focus%g.columns),36,0.002f);
    const float clipTop=g.y-24;
    const Clip saved=c.clip();
    c.setClip(0,clipTop,1920,g.clip-clipTop);
    bool below=false;
    for(unsigned row=static_cast<unsigned>(std::max(0.0f,std::floor(scroll)-1));row<rows&&!below;++row) {
        const float y=std::round(g.y+(static_cast<float>(row)-scroll)*g.stepY);
        below=y>=g.clip;
        const bool visible=!below&&y+posterH>clipTop;
        for(unsigned col=0;col<g.columns;++col) {
            const unsigned index=row*g.columns+col;
            if(index>=slots)break;
            const float x=g.x+col*g.stepX;
            if(!visible) {
                if(index<v.count&&m.art&&*v.games[index].art)m.art->want(v.games[index].art,art::Kind::tile,m.frame,true);
            } else if(index<v.count)poster(c,m,v.games[index],index,x,y,index==focus);
            else pageTile(c,x,y,v.state==CloudState::loading);
        }
    }
    if(focus<v.count) {
        const float w=posterW*1.04f, h=posterH*1.04f;
        focusFrame(c,g.x+ringColumn*g.stepX-(w-posterW)/2,g.y+(ringRow-scroll)*g.stepY-(h-posterH)/2,w,h,posterRadius*1.04f);
    }
    c.setClip(saved);
}

void searchField(Canvas& c, float x, float y, float w, string_view text, bool query) {
    c.roundRect(x,y,w,60,30,shell,158);
    c.roundRectStroke(x,y,w,60,30,1,face,seamAlpha);
    const float cy=y+30;
    glyph(c,Glyph::triangle,x+15,cy);
    c.ring(x+84,cy-3,7,2.4f,face);c.line(x+89,cy+2,x+95,cy+8,2.6f,face);
    fitted(c,Face::bold,18,x+110,cy,text,w-110-24,query?face:muted);
}
const char* storeCatalogText(const Model& m, char* buffer, std::size_t size) {
    if(*m.searchText)std::snprintf(buffer,size,"\xE2\x80\x9C%.90s\xE2\x80\x9D \xC2\xB7 new search",m.searchText);
    else std::snprintf(buffer,size,"Search all GeForce NOW games");
    return buffer;
}

struct Message { const char* title; const char* body; Button primary; Button secondary; };
void centeredMessage(Canvas& c, float cx, float cy, float width, const Message& msg) {
    const unsigned lines=drawWrapped(c,Face::semibold,18,0,0,26,msg.body?msg.body:"",width,3,muted,255,false);
    const bool buttons=msg.primary.text||msg.secondary.text;
    const float h=42+(lines?14+lines*26.0f:0)+(buttons?28+72:0);
    float y=cy-h/2;
    labelCenter(c,Face::black,30,cx,y+21,msg.title,face);
    y+=42;
    if(lines) {
        y+=14;
        const float bw=std::min(width,textWidth(Face::semibold,18,msg.body));
        drawWrapped(c,Face::semibold,18,lines>1?cx-width/2:cx-bw/2,y,26,msg.body,width,3,muted);
        y+=lines*26.0f;
    }
    if(!buttons)return;
    y+=28;
    const float pw=msg.primary.text?buttonWidth(msg.primary):0, sw=msg.secondary.text?buttonWidth(msg.secondary):0;
    float x=cx-(pw+sw+(pw&&sw?14:0))/2;
    if(msg.primary.text)x+=actionButton(c,msg.primary,x,y)+14;
    if(msg.secondary.text)actionButton(c,msg.secondary,x,y);
}
Message catalogMessage(const Model& m, char* buffer, std::size_t size) {
    const auto& v=catalogFor(m.section,m.session,m.library);
    const bool library=m.section==Section::library;
    if(m.screen==Screen::catalogLoading)
        return {library?"Loading your library\xE2\x80\xA6":"Loading the live catalog\xE2\x80\xA6",
                library?"OpenNOW is asking NVIDIA for the games on your account.":"OpenNOW is asking NVIDIA for the games you can stream.",{},{}};
    if(m.screen==Screen::catalogError)
        return {library?"Your library didn\xE2\x80\x99t load":"Catalog unavailable",*v.message?v.message:"No details reported",{Glyph::cross,"Try again",true},{}};
    if(v.hasNext) {
        std::snprintf(buffer,size,"OpenNOW looked through catalog page %u without finding a game you own. More pages remain, so your library may continue further on.",v.page+1);
        return {"No owned games found yet",buffer,{Glyph::cross,"Keep looking",true},{Glyph::square,"Refresh"}};
    }
    if(library)return {"Nothing in your library yet","Games you add to your GeForce NOW library on your NVIDIA account show up here. The Store has the full catalog.",{Glyph::cross,"Open Store",true},{Glyph::square,"Refresh"}};
    if(*m.searchText)std::snprintf(buffer,size,"Nothing matched \xE2\x80\x9C%.100s\xE2\x80\x9D. Try a shorter title.",m.searchText);
    else std::snprintf(buffer,size,"%s",v.message);
    return {"No games found",buffer,{Glyph::triangle,"Search",true},{Glyph::square,"Refresh"}};
}

void gamePanel(Canvas& c, const Model& m, const CloudView& v, unsigned focus, bool hasGame) {
    const float x=1336, y=228, w=488, h=680, ix=x+37, iw=w-74;
    panel(c,x,y,w,h,40);
    if(!hasGame) {
        eyebrow(c,ix,y+46,m.section==Section::library?"GeForce NOW library":"GeForce NOW catalog");
        label(c,Face::black,40,ix,y+46+38,"Pick a game",face,-0.02f);
        return;
    }
    const Game& game=v.games[focus];
    char text[192];
    std::snprintf(text,sizeof(text),"%s \xC2\xB7 game %u of %u",storeName(game.store),focus+1,v.count);
    eyebrow(c,ix,y+46,text,muted,14,iw);
    const unsigned lines=drawWrapped(c,Face::black,40,ix,y+58,46,game.title,iw,2,face);
    float at=y+58+lines*46.0f;
    const bool owned=m.section==Section::library||game.owned;
    if(owned) {
        at+=16;
        dot(c,ix+4.5f,at+12,mint);
        std::snprintf(text,sizeof(text),"In your %s library",storeName(game.store));
        fitted(c,Face::extrabold,17,ix+17,at+12,text,iw-17,mint);
        at+=24;
    }
    at+=20;
    eyebrow(c,ix,at+9,"Available on",muted,13);
    at+=18+9;
    unsigned siblings[8];
    const unsigned count=storeSiblings(v,focus,siblings,8);
    float cx=ix;
    for(unsigned i=0;i<count;++i) {
        const char* store=v.games[siblings[i]].store;
        const float cw=storeChipWidth(store);
        if(cx>ix&&cx+cw>ix+iw){cx=ix;at+=48;}
        storeChip(c,cx,at+20,store);
        cx+=cw+8;
    }
    const float bottom=y+h-37;
    rule(c,ix,bottom-30-12-7,iw);
    const Hint open{Glyph::cross,"Open details & play"};
    float hx=ix;
    hx+=glyph(c,open.glyph,hx,bottom-15)+12;
    label(c,Face::extrabold,18,hx,bottom-15,open.text,face);
}

const Hint closeAppHint{Glyph::circle,"Close app"};
void catalogChrome(Canvas& c, const Model& m, const char* title, const CloudView& v, bool hasGames, const Message& msg) {
    const Hint left[]={{Glyph::triangle,"Search"},closeAppHint};
    Hint right[2]{};
    unsigned n=0;
    if(m.screen!=Screen::catalogLoading)right[n++]={Glyph::square,"Refresh"};
    if(hasGames)right[n++]={Glyph::cross,v.state==CloudState::failed?"Try again":"Details"};
    else if(msg.primary.glyph==Glyph::cross)right[n++]={Glyph::cross,msg.primary.text};
    chrome(c,m,title,true,left,2,right,n);
}
void libraryScreen(Canvas& c, const Model& m) {
    const auto& v=m.library;
    const bool hasGames=m.screen==Screen::grid&&v.count;
    const unsigned focus=hasGames?std::min(m.focus,v.count-1):0;
    screenBackground(c,m,hasGames?&v.games[focus]:nullptr,rgb(0x354016));
    pageLayer(c,m);
    char text[160],body[256];
    const Message msg=hasGames?Message{}:catalogMessage(m,body,sizeof(body));
    searchField(c,96,126,440,storeCatalogText(m,text,sizeof(text)),false);
    if(hasGames||v.count) {
        if(v.page||v.hasNext)std::snprintf(text,sizeof(text),"%u games in your library \xC2\xB7 page %u",v.count,v.page+1);
        else std::snprintf(text,sizeof(text),"%u games in your library",v.count);
        upper(text);
        labelRight(c,Face::mono,15,1824,156,text,muted,0.09f);
    }
    if(hasGames) {
        posterGrid(c,m,v,focus,{99,232,172,269,916,gridColumns(Section::library)});
        if(v.state==CloudState::failed) {
            char error[256];std::snprintf(error,sizeof(error),"Couldn\xE2\x80\x99t load that page \xC2\xB7 %s",v.message);
            panel(c,96,862,1226,48,24,true);
            fitted(c,Face::semibold,17,124,886,error,1226-56,coral);
        }
    } else centeredMessage(c,701,565,900,msg);
    gamePanel(c,m,v,focus,hasGames);
    catalogChrome(c,m,"Library",v,hasGames,msg);
}
void storeStrip(Canvas& c, const CloudView& v, unsigned focus) {
    const Game& game=v.games[focus];
    const float x=96, y=820, w=1728, h=96, cy=y+h/2;
    panel(c,x,y,w,h,32,true);
    unsigned siblings[4];
    const unsigned count=storeSiblings(v,focus,siblings,4);
    float facts=textWidth(Face::mono,13,"AVAILABLE ON",0.12f)+22;
    for(unsigned i=0;i<count;++i)facts+=storeChipWidth(v.games[siblings[i]].store,32,6)+(i?8:0);
    char text[160];
    std::snprintf(text,sizeof(text),"In your %s library",storeName(game.store));
    if(game.owned)facts+=22+1+22+17+textWidth(Face::extrabold,17,text);
    fitted(c,Face::black,26,x+29,cy-12,game.title,w-facts-80,face);
    char sub[96];std::snprintf(sub,sizeof(sub),"%s \xC2\xB7 game %u of %u \xC2\xB7 page %u",storeName(game.store),focus+1,v.count,v.page+1);
    label(c,Face::bold,16,x+29,cy+18,sub,muted);
    float at=x+w-29-facts;
    at+=label(c,Face::mono,13,at,cy,"AVAILABLE ON",muted,0.12f)+22;
    for(unsigned i=0;i<count;++i)at+=storeChip(c,at,cy,v.games[siblings[i]].store,32,6)+8;
    if(game.owned) {
        at+=14;c.roundRect(at,cy-22,1,44,0,face,seamAlpha);at+=23;
        dot(c,at+4.5f,cy,mint);
        label(c,Face::extrabold,17,at+17,cy,text,mint);
    }
}
void storeScreen(Canvas& c, const Model& m, bool hints=true) {
    const auto& v=m.session;
    const bool hasGames=(m.screen==Screen::grid||m.screen==Screen::search)&&v.count;
    const unsigned focus=hasGames?std::min(m.focus,v.count-1):0;
    screenBackground(c,m,hasGames?&v.games[focus]:nullptr,rgb(0x18230F));
    pageLayer(c,m);
    eyebrow(c,96,130,*m.searchText?"Search results":"GeForce NOW catalog");
    char text[160],body[256];
    const Message msg=hasGames?Message{}:catalogMessage(m,body,sizeof(body));
    if(*m.searchText)std::snprintf(text,sizeof(text),"\xE2\x80\x9C%.100s\xE2\x80\x9D",m.searchText);
    else std::snprintf(text,sizeof(text),"Available games");
    const float tw=fitted(c,Face::black,40,96,166,text,900,face,-0.02f);
    searchField(c,96+tw+28,136,std::min(560.0f,1824-420-(96+tw+28)),*m.searchText?"New search":"Search all games",false);
    if(v.count) {
        if(v.hasNext)std::snprintf(text,sizeof(text),"Page %u \xC2\xB7 %u loaded \xC2\xB7 more available",v.page+1,v.count);
        else std::snprintf(text,sizeof(text),"Page %u \xC2\xB7 %u loaded",v.page+1,v.count);
        upper(text);
        labelRight(c,Face::mono,15,1824,173,text,muted,0.09f);
    }
    if(hasGames) {
        posterGrid(c,m,v,focus,{100,208,174,256,812,gridColumns(Section::browse)});
        if(v.state==CloudState::failed) {
            char error[256];std::snprintf(error,sizeof(error),"Couldn\xE2\x80\x99t load that page \xC2\xB7 %s",v.message);
            const float ew=std::min(textWidth(Face::semibold,17,error)+56,1728.0f);
            panel(c,960-ew/2,760,ew,44,22,true);
            fitted(c,Face::semibold,17,960-ew/2+28,782,error,ew-56,coral);
        }
        storeStrip(c,v,focus);
    } else centeredMessage(c,960,502,900,msg);
    if(hints)catalogChrome(c,m,"Store",v,hasGames,msg);
}

string_view profileName(StreamProfile p, char* buffer, std::size_t size) {
    const auto s=settingsFor(p);
    const char* prefix=p==StreamProfile::quality?"Quality ":p==StreamProfile::smooth?"Smooth ":p==StreamProfile::experimental?"Experimental ":p==StreamProfile::compatibility?"Compatibility ":"";
    if(s.height>=2160)std::snprintf(buffer,size,"%s4K%d %s",prefix,s.fps,s.hdr()?"HDR":"SDR");
    else std::snprintf(buffer,size,"%s%dp%d",prefix,s.height,s.fps);
    return buffer;
}
bool isPreset(const StreamSettings& s) {
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i)if(presetFor(static_cast<StreamProfile>(i),s)==s)return true;
    return false;
}
string_view settingsName(const StreamSettings& s, char* buffer, std::size_t size) {
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i)
        if(presetFor(static_cast<StreamProfile>(i),s)==s)return profileName(static_cast<StreamProfile>(i),buffer,size);
    if(s.height>=2160)std::snprintf(buffer,size,"Custom 4K%d %s",s.fps,s.hdr()?"HDR":"SDR");
    else std::snprintf(buffer,size,"Custom %dp%d%s",s.height,s.fps,s.hdr()?" HDR":"");
    return buffer;
}
const char* codecLabel(const StreamSettings& s) {return s.hdr()?"HEVC \xC2\xB7 10-bit \xC2\xB7 HDR":s.tenBit()?"HEVC \xC2\xB7 10-bit":"H.264 \xC2\xB7 8-bit";}
string_view outputLabel(const Model& m) {
    string_view output(m.output?m.output:"");
    if(output.rfind("OUTPUT ",0)==0)output.remove_prefix(7);
    return output;
}

void factRow(Canvas& c, float x, float cy, float w, string_view key, string_view value, Color valueColor=face) {
    label(c,Face::bold,19,x,cy,key,muted);
    const float vw=std::min(textWidth(Face::extrabold,19,value),w*0.66f);
    fitted(c,Face::extrabold,19,x+w-vw,cy,value,vw+1,valueColor);
}
void detail(Canvas& c, const Model& m) {
    const auto& cv=catalogFor(m.section,m.session,m.library);
    const unsigned focus=std::min(m.focus,cv.count-1);
    const Game& game=cv.games[focus];
    detailBackground(c,m,&game);
    pageLayer(c,m);
    char text[256];
    std::snprintf(text,sizeof(text),"%s \xC2\xB7 %s",storeName(game.store),m.section==Section::library?"In your library":"From the catalog");
    eyebrow(c,120,209,text,muted,15,860);
    float size=88;
    while(size>56&&drawWrapped(c,Face::black,size,0,0,size*1.02f,game.title,860,3,face,255,false)>2)size-=8;
    const float pitch=std::round(size*1.02f);
    const unsigned lines=drawWrapped(c,Face::black,size,120,222,pitch,game.title,860,2,face);
    const float copyBottom=222+lines*pitch;
    const float chipsTop=std::max(476.0f,copyBottom+40);
    eyebrow(c,120,chipsTop+9,"Play on");
    unsigned siblings[16];
    const unsigned count=storeSiblings(cv,focus,siblings,16);
    float x=120;
    const float chipY=chipsTop+31;
    for(unsigned i=0;i<count&&x<1300;++i) {
        const Game& entry=cv.games[siblings[i]];
        const bool selected=siblings[i]==focus, owned=m.section==Section::library||entry.owned;
        const char* ownership=owned?"Owned":"Not owned";
        const float textW=std::max(textWidth(Face::black,19,storeName(entry.store)),textWidth(Face::bold,15,ownership));
        const float w=14+42+14+textW+(selected?14+22:0)+24;
        if(selected){c.roundRect(x,chipY,w,72,24,face,41);c.roundRectStroke(x,chipY,w,72,24,1.5f,face,120);}
        else {c.roundRect(x,chipY,w,72,24,shell,179);c.roundRectStroke(x,chipY,w,72,24,1,face,seamAlpha);}
        storeMark(c,x+14+21,chipY+36,42,entry.store);
        label(c,Face::black,19,x+14+42+14,chipY+26,storeName(entry.store),face);
        label(c,Face::bold,15,x+14+42+14,chipY+48,ownership,owned?mint:muted);
        if(selected){const float cx=x+14+42+14+textW+14+11;c.line(cx-7,chipY+37,cx-2,chipY+42,3,mint);c.line(cx-2,chipY+42,cx+8,chipY+31,3,mint);}
        x+=w+12;
    }
    const float actionsY=chipY+72+34;
    std::snprintf(text,sizeof(text),"Play on %s",storeName(game.store));
    Button play{Glyph::cross,text,true};
    const float pw=buttonWidth(play,24,34)+12;
    actionButton(c,play,120,actionsY,72,pw,24,34);
    focusRing(c,m,0,120,actionsY,pw,72,36,140);
    if(m.detailAction)actionButton(c,{Glyph::triangle,m.detailAction},120+pw+14,actionsY,72,0,20,30);
    const float cardX=1352, cardY=470, cardW=472, ix=cardX+29, iw=cardW-58;
    const unsigned available=m.choiceCount, selected=std::min(m.choice,available?available-1:0);
    const StreamSettings& chosen=available?m.choices[selected]:m.defaults;
    char name[48],value[96];
    const float cardH=29+18+16+76+16+5*34+6+46+29;
    panel(c,cardX,cardY,cardW,cardH,32,true);
    float y=cardY+29;
    eyebrow(c,ix,y+9,"Next launch requests",muted,13);
    labelRight(c,Face::extrabold,15,ix+iw,y+9,"Settings \xC2\xB7 Streaming",muted);
    y+=18+16;
    c.roundRect(ix,y,iw,76,22,face,18);
    c.roundRectStroke(ix,y,iw,76,22,1,face,seamAlpha);
    fitted(c,Face::black,21,ix+20,y+26,selected==0?string_view("Your default"):settingsName(chosen,name,sizeof(name)),iw-130,face);
    std::snprintf(value,sizeof(value),"%u of %u \xC2\xB7 %s",selected+1,available,selected==0?settingsName(chosen,name,sizeof(name)).data():"this launch only");
    fitted(c,Face::semibold,15,ix+20,y+52,value,iw-110,muted);
    const float ax=ix+iw-36;
    c.disc(ax,y+38,22,face,selected>0?255:60);
    c.line(ax-6,y+33,ax,y+27,2.6f,faceText);c.line(ax,y+27,ax+6,y+33,2.6f,faceText);
    c.line(ax-6,y+43,ax,y+49,2.6f,faceText);c.line(ax,y+49,ax+6,y+43,2.6f,faceText);
    y+=76+16;
    if(chosen.height>=2160)std::snprintf(value,sizeof(value),"4K \xC2\xB7 %d FPS",chosen.fps);
    else std::snprintf(value,sizeof(value),"%dp \xC2\xB7 %d FPS",chosen.height,chosen.fps);
    factRow(c,ix,y+12,iw,"Picture",value);y+=34;
    factRow(c,ix,y+12,iw,"Codec",codecLabel(chosen));y+=34;
    factRow(c,ix,y+12,iw,"Decoding",chosen.hardware()?"Hardware":"Software");y+=34;
    std::snprintf(value,sizeof(value),"%d Mb/s max",chosen.bitrate_kbps/1000);
    factRow(c,ix,y+12,iw,"Bitrate",value);y+=34;
    factRow(c,ix,y+12,iw,"Output",outputLabel(m));y+=34+6;
    drawWrapped(c,Face::semibold,15,ix,y,21,"The rig decides the final stream. These are requests, shown before launch only.",iw,2,muted);
    const Hint left[]={{Glyph::circle,"Back"},{Glyph::dpad,count>1?"Store \xC2\xB7 stream":"Stream"}};
    const Hint right[]={{Glyph::triangle,m.detailAction?m.detailAction:""},{Glyph::cross,play.text}};
    chrome(c,m,"",false,left,2,m.detailAction?right:right+1,m.detailAction?2:1,m.tile.stage==HomeTile::Stage::hidden);
}

void rowFrame(Canvas& c, const Model& m, float x, float y, float w, float h, bool focused) {
    if(!focused) {
        c.roundRect(x+18,y+h-1,w-36,1,0,face,23);
        return;
    }
    const std::uint32_t key=motionKey(focusAnim,pageKey(m),static_cast<std::uint32_t>(m.pane)+8);
    const float fy=follow(m,key,y,36), fh=follow(m,key+1,h,36);
    c.roundRect(x,fy,w,fh,24,face,26);
    c.roundRectStroke(x-5,fy-5,w+10,fh+10,29,5,accent,102);
    c.roundRectStroke(x,fy,w,fh,24,3,face);
}
void rowText(Canvas& c, float x, float y, float h, string_view title, string_view description, Color descColor=muted, bool dirty=false, Color titleColor=face, float maxWidth=700) {
    const bool desc=!description.empty();
    const float top=y+(h-(desc?51:26))/2;
    const float tw=fitted(c,Face::black,21,x+22,top+13,title,maxWidth,titleColor);
    if(dirty)c.disc(x+22+tw+12,top+13,5,yellow);
    if(desc)fitted(c,Face::semibold,16,x+22,top+29+11,description,maxWidth,descColor);
}
struct Option { const char* text; bool on, disabled; };
void unavailableIcon(Canvas& c, float cx, float cy, Color color) {c.ring(cx,cy,7,2.2f,color);c.line(cx-5,cy+5,cx+5,cy-5,2.2f,color);}
float segments(Canvas& c, const Model& m, unsigned id, float rightX, float cy, const Option* options, unsigned count) {
    float widths[4],starts[4],total=6*(count-1.0f);
    for(unsigned i=0;i<count;++i){widths[i]=36+textWidth(Face::black,16,options[i].text)+(options[i].disabled?22:0);total+=widths[i];}
    float x=rightX-total;
    int on=-1;
    for(unsigned i=0;i<count;++i) {
        starts[i]=x;
        if(options[i].on)on=static_cast<int>(i);
        c.roundRectStroke(x,cy-22,widths[i],44,22,1,face,46);
        x+=widths[i]+6;
    }
    float pill=0,pillW=0;
    if(on>=0) {
        const std::uint32_t key=motionKey(segmentAnim,id);
        pill=follow(m,key,starts[on],34);pillW=follow(m,key+1,widths[on],34);
        c.roundRect(pill,cy-22,pillW,44,22,face);
    }
    for(unsigned i=0;i<count;++i) {
        const auto& o=options[i];
        const float overlap=std::clamp((std::min(pill+pillW,starts[i]+widths[i])-std::max(pill,starts[i]))/widths[i],0.0f,1.0f);
        const Color ink=mix(o.disabled?dim:face,faceText,overlap);
        float tx=starts[i]+18;
        if(o.disabled){unavailableIcon(c,tx+7,cy,ink);tx+=22;}
        label(c,Face::black,16,tx,cy,o.text,ink);
    }
    return total;
}
void toggle(Canvas& c, const Model& m, unsigned id, float rightX, float cy, bool on) {
    const float t=follow(m,motionKey(toggleAnim,id),on?1.0f:0.0f,34,0.002f);
    const float x=rightX-64;
    c.roundRect(x,cy-18,64,36,18,face,46);
    if(t>0)c.roundRect(x,cy-18,64,36,18,mint,alphaOf(t));
    c.disc(x+4+28*t+14,cy,14,face);
}
void chevron(Canvas& c, float cx, float cy, int direction, Color color) {
    const float d=4.5f*static_cast<float>(direction);
    c.line(cx-d,cy-8,cx+d,cy,3,color);c.line(cx+d,cy,cx-d,cy+8,3,color);
}
float cycler(Canvas& c, float rightX, float cy, string_view text, bool focused, int index=-1, unsigned count=0) {
    const float w=44+6+190+6+44, x=rightX-w;
    for(int side=0;side<2;++side) {
        const float cx=side?rightX-22:x+22;
        if(focused)c.disc(cx,cy,22,face);
        else c.ring(cx,cy,21.5f,1,face,56);
        chevron(c,cx,cy,side?1:-1,focused?faceText:face);
    }
    const bool dots=count>1&&count<=12;
    labelCenter(c,Face::black,20,x+50+95,dots?cy-7:cy,text,face);
    if(dots) {
        float dw=0;for(unsigned i=0;i<count;++i)dw+=(static_cast<int>(i)==index?18:6)+(i?5:0);
        float dx=x+50+95-dw/2;
        for(unsigned i=0;i<count;++i){const bool on=static_cast<int>(i)==index;c.roundRect(dx,cy+12,on?18:6,6,3,face,on?255:90);dx+=(on?18:6)+5;}
    }
    return w;
}
float valuePill(Canvas& c, float rightX, float cy, string_view text, bool danger, bool enabled=true) {
    const float w=std::min(460.0f,textWidth(Face::black,18,text)+66), x=rightX-w;
    const Color ink=!enabled?dim:danger?coral:face;
    if(danger){c.roundRect(x,cy-24,w,48,24,coral,26);c.roundRectStroke(x,cy-24,w,48,24,1.5f,coral,153);}
    else {c.roundRect(x,cy-24,w,48,24,face,26);c.roundRectStroke(x,cy-24,w,48,24,1,face,46);}
    fitted(c,Face::black,18,x+20,cy,text,w-66,ink);
    chevron(c,rightX-25,cy,1,ink);
    return w;
}
float infoValue(Canvas& c, float rightX, float cy, string_view text, Color color=muted, float maxWidth=560) {
    const float w=std::min(textWidth(Face::black,18,text),maxWidth);
    fitted(c,Face::black,18,rightX-w,cy,text,maxWidth,color);
    return w;
}

constexpr float rowsX=515, rowsW=1274, rowsTop=213;
struct DraftStatus { const char* title; const char* body; Color tone; char buffer[96]; };
DraftStatus draftStatus(const Model& m) {
    DraftStatus s{};
    const unsigned dirty=changedRows(m.draft,m.defaults);
    const char* problem=settingsProblem(m.draft,m.draftAvailable);
    if(dirty&&problem){s.title="Can\xE2\x80\x99t save yet";s.body=problem;s.tone=coral;}
    else if(dirty&&m.adjusted){std::snprintf(s.buffer,sizeof(s.buffer),"%u setting%s adjusted",m.adjusted,m.adjusted==1?"":"s");s.title=s.buffer;s.body="Changed to fit Software decoding. Kept until you Save or Revert.";s.tone=yellow;}
    else if(dirty){std::snprintf(s.buffer,sizeof(s.buffer),"%u unsaved change%s",rowCount(dirty),rowCount(dirty)==1?"":"s");s.title=s.buffer;s.body="Kept until you Save or Revert, even if you leave.";s.tone=yellow;}
    else if(m.settings.saveError){std::snprintf(s.buffer,sizeof(s.buffer),"Couldn\xE2\x80\x99t save on this PS5 \xC2\xB7 error %d",m.settings.saveError);s.title="Not saved";s.body=s.buffer;s.tone=coral;}
    else if(m.settings.savedUnavailable){s.title="Saved choice not qualified";s.body="It doesn\xE2\x80\x99t work on this PS5 or output now. Using the best available.";s.tone=yellow;}
    else if(m.settings.loadCorrupt||m.settings.loadUnreadable){s.title="Saved settings ignored";s.body="They couldn\xE2\x80\x99t be read. Using the best available.";s.tone=yellow;}
    else {s.title=m.settings.saved?"Saved on this PS5":"Best available";s.body=m.settings.saved?"Used when you press Play.":"Used when you press Play. Not saved yet.";s.tone=mint;}
    return s;
}
void draftCard(Canvas& c, const Model& m) {
    const StreamSettings& d=m.draft;
    const float x=119, y=508, w=314, h=392, ix=x+22, iw=w-44;
    c.roundRect(x,y,w,h,26,face,10);
    c.roundRectStroke(x,y,w,h,26,1,face,seamAlpha);
    char text[96];
    float at=y+30;
    const bool input=m.pane==SettingsPane::controller;
    eyebrow(c,ix,at,input?"Next stream":"Requested",accent,13);at+=30;
    if(input) {
        std::snprintf(text,sizeof(text),"Dead zones L %d%% \xC2\xB7 R %d%%",d.controller_left_deadzone,d.controller_right_deadzone);
        fitted(c,Face::mono,15,ix,at,text,iw,face);at+=26;
        std::snprintf(text,sizeof(text),"Vibration %d%%",d.vibration_intensity);
        fitted(c,Face::mono,15,ix,at,text,iw,face);at+=26;
        std::snprintf(text,sizeof(text),"Mouse speed %d%%",d.mouse_sensitivity);
        fitted(c,Face::mono,15,ix,at,text,iw,face);at+=26;
        fitted(c,Face::mono,15,ix,at,d.persist_game_settings?"Keep game settings":"Fresh game settings",iw,face);at+=22;
        rule(c,ix,at,iw);at+=26;
        eyebrow(c,ix,at,"Applies to",muted,13);at+=14;
        at+=drawWrapped(c,Face::semibold,14,ix,at,22,"Every controller on this PS5, from the next stream",iw,2,muted)*22.0f+12;
    } else {
        std::snprintf(text,sizeof(text),"%d\xC3\x97%d \xC2\xB7 %d FPS",d.width,d.height,d.fps);
        fitted(c,Face::mono,15,ix,at,text,iw,face);at+=26;
        std::snprintf(text,sizeof(text),"%s \xC2\xB7 %s \xC2\xB7 %d Mb/s",d.tenBit()?"HEVC":"H.264",d.hdr()?"HDR":"SDR",d.bitrate_kbps/1000);
        fitted(c,Face::mono,15,ix,at,text,iw,face);at+=26;
        fitted(c,Face::mono,15,ix,at,d.hardware()?"Hardware decoding":"Software decoding",iw,face);at+=26;
        std::snprintf(text,sizeof(text),"%s audio",d.audio_mode==audio::Mode::automatic?"Auto":d.audio_mode==audio::Mode::stereo?"Stereo":audio::modeLabel(d.audio_mode));
        fitted(c,Face::mono,15,ix,at,text,iw,face);at+=22;
        rule(c,ix,at,iw);at+=26;
        eyebrow(c,ix,at,"This PS5 output",muted,13);at+=14;
        at+=drawWrapped(c,Face::mono,14,ix,at,22,outputLabel(m),iw,2,muted)*22.0f+12;
    }
    const auto s=draftStatus(m);
    c.roundRect(ix-8,at,iw+16,y+h-14-at,18,s.tone,20);
    c.disc(ix+8,at+24,5,s.tone);
    fitted(c,Face::black,18,ix+22,at+24,s.title,iw-26,face);
    drawWrapped(c,Face::semibold,15,ix+4,at+42,20,s.body,iw-8,3,muted);
}
void streamPane(Canvas& c, const Model& m) {
    const StreamSettings& d=m.draft;
    const unsigned dirty=changedRows(d,m.defaults);
    const char* problem=settingsProblem(d,m.draftAvailable);
    const bool anyHardware=m.caps.h264Hardware||m.caps.hevcSdr||m.caps.hevcHdr;
    char text[96],name[48];
    for(unsigned i=0;i<static_cast<unsigned>(StreamRow::count);++i) {
        const auto row=static_cast<StreamRow>(i);
        const float h=80, y=rowsTop+i*(h+4), cy=y+h/2, rx=rowsX+rowsW-22;
        const bool focused=m.settingsContent&&m.settingsRow==i;
        rowFrame(c,m,rowsX,y,rowsW,h,focused);
        const char* head="";const char* sub="";Color subColor=muted;
        switch(row) {
        case StreamRow::preset: {
            head="Preset";
            int index=-1;
            for(unsigned p=1;p<m.choiceCount;++p){char a[48],b[48];if(settingsName(m.choices[p],a,sizeof(a))==settingsName(d,b,sizeof(b)))index=static_cast<int>(p-1);}
            std::snprintf(text,sizeof(text),"%u qualified presets \xC2\xB7 edits make it Custom",m.choiceCount?m.choiceCount-1:0);
            sub=text;
            cycler(c,rx,cy,isPreset(d)?settingsName(d,name,sizeof(name)):string_view("Custom"),focused,index,m.choiceCount?m.choiceCount-1:0);
            break;
        }
        case StreamRow::decoding: {
            head="Decoding";
            sub=!anyHardware?"Hardware decoding isn\xE2\x80\x99t qualified on this PS5":d.hardware()?"PS5 video decoder":"CPU decoding, H.264 only";
            if(!anyHardware)subColor=yellow;
            const Option o[]={{"Hardware accelerated",d.hardware(),!anyHardware},{"Software",!d.hardware(),false}};
            segments(c,m,0,rx,cy,o,2);
            break;
        }
        case StreamRow::codec: {
            head="Codec";
            const bool hevc=m.caps.hevcSdr||m.caps.hevcHdr;
            sub=!d.hardware()?"Software decodes H.264 only":!hevc?"HEVC isn\xE2\x80\x99t qualified on this PS5":"AV1 is unavailable in this port";
            if(!d.hardware()||!hevc)subColor=yellow;
            const Option o[]={{"H.264",!d.tenBit(),d.hardware()&&!m.caps.h264Hardware},{"HEVC",d.tenBit(),!d.hardware()||!hevc},{"AV1",false,true}};
            segments(c,m,1,rx,cy,o,3);
            break;
        }
        case StreamRow::resolution:
            head="Resolution";
            sub=d.hardware()?"Even sizes, 320\xC3\x97" "180 to 3840\xC3\x97" "2160 \xC2\xB7 Cross types an exact size":"Software: up to 1920\xC3\x97" "1080 \xC2\xB7 Cross types an exact size";
            if(!d.hardware())subColor=yellow;
            if(validateSettings(d)==SettingsError::dimensions){sub=problem;subColor=coral;}
            std::snprintf(text,sizeof(text),"%d \xC3\x97 %d",d.width,d.height);
            cycler(c,rx,cy,text,focused);
            break;
        case StreamRow::fps:
            head="Frame rate";
            sub=d.hardware()?"Any whole number, 30 to 120":"Software: 30 to 60";
            if(!d.hardware())subColor=yellow;
            if(validateSettings(d)==SettingsError::fps){sub=problem;subColor=coral;}
            std::snprintf(text,sizeof(text),"%d FPS",d.fps);
            cycler(c,rx,cy,text,focused);
            break;
        case StreamRow::bitrate:
            head="Bitrate limit";
            sub="4 to 100 Mb/s \xC2\xB7 the cloud may send less";
            std::snprintf(text,sizeof(text),"%d Mb/s",d.bitrate_kbps/1000);
            cycler(c,rx,cy,text,focused);
            break;
        case StreamRow::hdr: {
            head="HDR";
            sub=!d.tenBit()?"Needs HEVC and hardware decoding":!m.caps.hevcHdr?"HDR isn\xE2\x80\x99t qualified on this PS5":"HEVC Main10 with HDR10";
            if(d.tenBit()&&!m.caps.hevcHdr)subColor=yellow;
            const Option o[]={{"Off",!d.hdr(),d.tenBit()&&!m.caps.hevcSdr},{"On",d.hdr(),!d.tenBit()||!m.caps.hevcHdr}};
            segments(c,m,2,rx,cy,o,2);
            break;
        }
        case StreamRow::reset:
            head="Reset to best available";
            sub="Forgets the saved default. Your NVIDIA login stays.";
            valuePill(c,rx,cy,"Reset",true);
            break;
        case StreamRow::count: break;
        }
        rowText(c,rowsX,y,h,head,sub,subColor,dirty&rowBit(row));
    }
}
void displayPane(Canvas& c, const Model& m) {
    float y=rowsTop;
    const auto info=[&](const char* title,const char* desc,string_view value,float h=84){
        rowFrame(c,m,rowsX,y,rowsW,h,false);
        rowText(c,rowsX,y,h,title,desc,muted,false,face,560);
        infoValue(c,rowsX+rowsW-22,y+h/2,value,face,600);
        y+=h+4;
    };
    info("Video output","VideoOut mode chosen at startup",outputLabel(m));
    char codecs[96]{};
    for(const auto& [on,text]:{std::pair{m.caps.hevcHdr,"HEVC Main10 HDR"},std::pair{m.caps.hevcSdr,"HEVC Main10"},std::pair{m.caps.h264Hardware,"H.264"}})
        if(on)std::snprintf(codecs+std::strlen(codecs),sizeof(codecs)-std::strlen(codecs),"%s%s",*codecs?" \xC2\xB7 ":"",text);
    info("Hardware video","Modes that passed the startup test streams",*codecs?codecs:"Software decoding only");
    info("Audio channels","Surround is turned off for now","Stereo");
    const auto& a=m.audio;
    const float h=150;
    rowFrame(c,m,rowsX,y,rowsW,h,false);
    const float ix=rowsX+22;
    label(c,Face::black,21,ix,y+30,"Last session audio",face);
    if(!a.requested) {
        drawWrapped(c,Face::semibold,16,ix,y+52,24,"No stream yet since OpenNOW started. After you play, this shows the layout requested and the layout negotiated for that session.",900,2,muted);
        return;
    }
    const auto layout=[](unsigned channels){return channels==8?"7.1":channels==6?"5.1":"Stereo";};
    char line[96],why[128];
    std::snprintf(line,sizeof(line),"%s \xC2\xB7 %u channels",layout(a.requested),a.requested);
    eyebrow(c,ix,y+66,"Requested",muted,13);
    const float rw=label(c,Face::black,19,ix,y+94,line,face);
    const float nx=ix+std::max(rw+60,300.0f);
    eyebrow(c,nx,y+66,"Negotiated",muted,13);
    if(a.negotiated)std::snprintf(line,sizeof(line),"%s \xC2\xB7 Opus %u channels",layout(a.negotiated),a.negotiated);
    else std::snprintf(line,sizeof(line),"Not negotiated");
    label(c,Face::black,19,nx,y+94,line,a.negotiated&&a.negotiated>=a.requested?face:yellow);
    if(a.fallback)std::snprintf(why,sizeof(why),"The cloud described surround but sent stereo, so OpenNOW fell back to stereo.");
    else if(!a.negotiated)std::snprintf(why,sizeof(why),"Audio wasn\xE2\x80\x99t set up in that session.");
    else if(a.negotiated<a.requested)std::snprintf(why,sizeof(why),"This session negotiated %s instead of the requested layout.",layout(a.negotiated));
    else std::snprintf(why,sizeof(why),"This session negotiated the requested layout.");
    label(c,Face::semibold,16,ix,y+124,why,muted);
}
void cachePane(Canvas& c, const Model& m) {
    const auto& d=m.disk;
    const double megabytes=double(d.bytes)/(1024.0*1024.0);
    const bool known=d.available;
    char big[32],line[160];
    float y=rowsTop;
    const float usageH=120;
    rowFrame(c,m,rowsX,y,rowsW,usageH,false);
    const char* state=d.error?"Cover art storage reported an error on this PS5":d.busy?"Updating\xE2\x80\xA6":!d.enabled?"Saving is off":"oldest removed first when full";
    if(known)std::snprintf(line,sizeof(line),"%zu of %u images \xC2\xB7 %s",d.count,art::DiskCache::slots,state);
    else std::snprintf(line,sizeof(line),"%s \xC2\xB7 saved covers aren\xE2\x80\x99t counted until saving is on",state);
    rowText(c,rowsX,y,usageH,"Cover art on this PS5",line,d.error?coral:muted,false,face,640);
    if(known)std::snprintf(big,sizeof(big),megabytes<10?"%.1f MB":"%.0f MB",megabytes);
    else std::snprintf(big,sizeof(big),"Unknown");
    std::snprintf(line,sizeof(line),"of %zu MB",art::DiskCache::budgetBytes>>20);
    const float rx=rowsX+rowsW-22;
    const float lw=labelRight(c,Face::bold,16,rx,y+44,line,muted);
    labelRight(c,Face::black,28,rx-lw-10,y+44,big,known?face:muted);
    const float fill=std::min(1.0f,float(double(d.bytes)/double(art::DiskCache::budgetBytes)));
    c.roundRect(rx-320,y+76,320,10,5,face,41);
    const float shownFill=follow(m,motionKey(barAnim,1),known?fill:0,18,0.0005f);
    if(shownFill>0)c.roundRect(rx-320,y+76,std::max(10.0f,320*shownFill),10,5,accent);
    y+=usageH+4;
    const bool toggleFocus=m.settingsContent&&m.settingsRow==0, clearFocus=m.settingsContent&&m.settingsRow==1;
    rowFrame(c,m,rowsX,y,rowsW,84,toggleFocus);
    rowText(c,rowsX,y,84,"Save cover art on this PS5","Off keeps covers in memory only. They download again each launch.");
    toggle(c,m,0,rx,y+42,m.artworkWanted);
    y+=88;
    rowFrame(c,m,rowsX,y,rowsW,84,clearFocus);
    const bool canClear=m.art&&!d.busy&&(!known||d.count>0);
    rowText(c,rowsX,y,84,"Clear cover art cache",canClear?(known?"Removes saved images only. You stay signed in and keep your settings.":"Removes any saved images, even while saving is off. You stay signed in."):
        d.busy?"Busy right now. Try again in a moment.":"Nothing saved to clear.");
    valuePill(c,rx,y+42,"Clear cache",true,canClear);
    y+=88+20;
    if(m.settings.cacheSaveError){std::snprintf(line,sizeof(line),"Couldn\xE2\x80\x99t save this choice on this PS5 \xC2\xB7 error %d",m.settings.cacheSaveError);dot(c,rowsX+30,y+12,coral);label(c,Face::bold,16,rowsX+46,y+12,line,coral);}
    else {dot(c,rowsX+30,y+12,mint);label(c,Face::bold,16,rowsX+46,y+12,"Saved on this PS5 \xC2\xB7 changes apply right away",muted);}
}
void accountPane(Canvas& c, const Model& m) {
    float y=rowsTop;
    const float cardH=120;
    rowFrame(c,m,rowsX,y,rowsW,cardH,false);
    const float ax=rowsX+22+38, cy=y+cardH/2;
    c.disc(ax,cy,38,violet);c.ring(ax,cy,37,3,face);
    labelCenter(c,Face::black,32,ax,cy,"N",faceText);
    const float tx=rowsX+22+76+20;
    const float nw=label(c,Face::black,28,tx,cy-24,"NVIDIA account",face);
    const char* tier=m.login.sessionSaved?"LOGIN SAVED":"NOT SAVED";
    const float bw=textWidth(Face::mono,13,tier,0.08f)+20;
    c.roundRect(tx+nw+12,cy-36,bw,24,10,m.login.sessionSaved?violet:yellow,41);
    label(c,Face::mono,13,tx+nw+22,cy-24,tier,m.login.sessionSaved?violet:yellow,0.08f);
    label(c,Face::bold,17,tx,cy+6,"Signed in to GeForce NOW on this PS5",muted);
    label(c,Face::semibold,14,tx,cy+30,m.login.sessionSaved?"Login saved on this PS5 and restored when OpenNOW starts.":"Signed in for now \xC2\xB7 the login couldn\xE2\x80\x99t be saved on this PS5.",m.login.sessionSaved?muted:yellow);
    y+=cardH+4;
    const bool focused=m.settingsContent;
    rowFrame(c,m,rowsX,y,rowsW,84,focused);
    rowText(c,rowsX,y,84,"Sign out","Removes the saved login. Signing in again needs a new code.");
    valuePill(c,rowsX+rowsW-22,y+42,"Sign out of NVIDIA",true);
    y+=88;
    rowFrame(c,m,rowsX,y,rowsW,84,false);
    rowText(c,rowsX,y,84,"Your password stays with NVIDIA","OpenNOW never asks for it. Sign-in happens on NVIDIA\xE2\x80\x99s site with a one-time code from this PS5.",muted,false,face,1200);
}
void aboutPane(Canvas& c, const Model& m) {
    float y=rowsTop;
    rowFrame(c,m,rowsX,y,rowsW,84,false);
    rowText(c,rowsX,y,84,"Version","OpenNOW for PS5 \xC2\xB7 alpha build");
    infoValue(c,rowsX+rowsW-22,y+42,OPENNOW_VERSION,face);
    y+=88;
    const float h=136;
    rowFrame(c,m,rowsX,y,rowsW,h,false);
    label(c,Face::black,21,rowsX+22,y+32,"Credits & licenses",face);
    drawWrapped(c,Face::semibold,16,rowsX+22,y+52,24,"Built on OpenNOW and OpenNOW-Switch by Open Cloud Gaming. Fonts: Nunito and IBM Plex Mono (SIL OFL 1.1). stb_truetype and stb_image (public domain / MIT). Full notices ship with the app in THIRD_PARTY_NOTICES.",rowsW-44,3,muted);
    y+=h+4;
    rowFrame(c,m,rowsX,y,rowsW,84,false);
    rowText(c,rowsX,y,84,"Unofficial client","Not affiliated with NVIDIA or Sony. GeForce NOW is a trademark of NVIDIA.");
}

void paneIcon(Canvas& c, SettingsPane pane, float x, float cy) {
    constexpr float s=32;
    const float cx=x+s/2;
    const Color tone=pane==SettingsPane::stream?accent:pane==SettingsPane::display?yellow:pane==SettingsPane::controller?mint:
                     pane==SettingsPane::cache?coral:pane==SettingsPane::account?violet:face;
    c.roundRect(x,cy-s/2,s,s,9,tone);
    switch(pane) {
    case SettingsPane::stream: {const float p[]={cx-4,cy-7,cx+7,cy,cx-4,cy+7};c.polygon(p,3,faceText);break;}
    case SettingsPane::display: c.roundRect(cx-9,cy-8,18,12,3,faceText);c.roundRect(cx-5,cy+6,10,2.5f,1,faceText);break;
    case SettingsPane::controller:
        c.roundRect(cx-11,cy-6,22,12,6,faceText);
        c.roundRect(cx-7.5f,cy-1,6,2,1,tone);c.roundRect(cx-5.5f,cy-3,2,6,1,tone);
        c.disc(cx+5,cy-1.5f,1.6f,tone);c.disc(cx+7.5f,cy+1.5f,1.6f,tone);
        break;
    case SettingsPane::cache: {
        c.roundRectStroke(cx-9,cy-8,18,16,3,2,faceText);
        const float p[]={cx-6,cy+5,cx-1,cy-1,cx+3,cy+5};c.polygon(p,3,faceText);
        c.disc(cx+4,cy-3,2,faceText);
        break;
    }
    case SettingsPane::account: c.disc(cx,cy-3.5f,4,faceText);c.roundRect(cx-7,cy+2,14,7,3.5f,faceText);break;
    case SettingsPane::about: labelCenter(c,Face::black,18,cx,cy,"i",faceText);break;
    }
}
const char* paneName(SettingsPane pane) {
    constexpr const char* names[]={"Streaming","Display & audio","Input & controllers","Cover art & cache","Account","About"};
    return names[static_cast<unsigned>(pane)];
}
bool settingsDialog(const Model& m) {return m.edit.open()||m.confirmClear||m.confirmFixed||m.confirmSignOut;}
void settingsSidebar(Canvas& c, const Model& m) {
    panel(c,96,124,360,792,40);
    const float activeY=follow(m,motionKey(paneAnim,1),168+static_cast<float>(m.pane)*58.0f,36);
    c.roundRect(119,activeY-27,314,54,27,face);
    if(!m.settingsContent&&!settingsDialog(m))focusFrame(c,119,activeY-27,314,54,27);
    for(unsigned i=0;i<settingsPanes;++i) {
        const auto pane=static_cast<SettingsPane>(i);
        const float cy=168+i*58.0f;
        paneIcon(c,pane,135,cy);
        fitted(c,Face::black,19,181,cy,paneName(pane),236,mix(face,faceText,std::clamp(1-std::abs(activeY-cy)/54,0.0f,1.0f)));
    }
}

void slider(Canvas& c, const Model& m, unsigned id, float rightX, float cy, int value, const ControllerRange& range, bool focused) {
    char text[16];std::snprintf(text,sizeof(text),"%d%%",value);
    const float tw=280, tx=rightX-120-tw;
    const float t=std::clamp(static_cast<float>(value-range.min)/static_cast<float>(range.max-range.min),0.0f,1.0f);
    const float kx=tx+tw*follow(m,motionKey(sliderAnim,id),t,34,0.0005f);
    c.roundRect(tx,cy-4,tw,8,4,face,41);
    if(kx>tx)c.roundRect(tx,cy-4,std::max(8.0f,kx-tx),8,4,accent);
    if(focused)c.disc(kx,cy,20,accent,90);
    c.disc(kx,cy,14,face);
    label(c,Face::black,20,rightX-88,cy,text,face);
}
void controllerPane(Canvas& c, const Model& m) {
    StreamSettings draft=m.draft, saved=m.defaults;
    constexpr const char* titles[]={"Left stick dead zone","Right stick dead zone","Vibration intensity","Mouse sensitivity"};
    constexpr const char* descriptions[]={"Ignore small movements from stick drift. Default: 5%.","Ignore small movements from stick drift. Default: 5%.",
        "How strongly game rumble plays on the controller. 0% turns it off. Default: 100%.","Pointer speed when the controller moves the mouse. Default: 100%."};
    float y=rowsTop;
    const float h=84, rx=rowsX+rowsW-22;
    for(unsigned i=0;i<4;++i,y+=h+4) {
        const auto row=static_cast<ControllerRow>(i);
        const int value=*controllerValue(draft,row);
        rowFrame(c,m,rowsX,y,rowsW,h,m.settingsContent&&m.settingsRow==i);
        rowText(c,rowsX,y,h,titles[i],descriptions[i],muted,value!=*controllerValue(saved,row),face,760);
        slider(c,m,i,rx,y+h/2,value,controllerRanges[i],m.settingsContent&&m.settingsRow==i);
    }
    const unsigned row=static_cast<unsigned>(ControllerRow::persistGameSettings);
    rowFrame(c,m,rowsX,y,rowsW,h,m.settingsContent&&m.settingsRow==row);
    rowText(c,rowsX,y,h,"Persistent in-game settings","Keep in-game graphics settings between sessions. Only affects games that support it; applies to new sessions.",
        muted,draft.persist_game_settings!=saved.persist_game_settings,face,1000);
    toggle(c,m,1,rx,y+h/2,draft.persist_game_settings);
}

enum class Sheet : std::uint32_t { signOut=1, clear, fixed, editor, tile, search };
void sheetBackdrop(Canvas& c, const Model& m, Sheet kind, bool left) {
    const float p=reveal(m,motionKey(sheetAnim,static_cast<std::uint32_t>(kind)),26);
    layer(c,1,0,0);
    c.roundRect(0,0,1920,1080,0,black,static_cast<unsigned>(166*p+0.5f));
    layer(c,p,left?-48*(1-p):0,left?0:24*(1-p));
}
void sheet(Canvas& c, float x, float y, float w, float h, float radius) {
    c.roundRect(x,y,w,h,radius,sheetFill);
    c.roundRectStroke(x,y,w,h,radius,1,face,seamAlpha);
}
float confirmSheet(Canvas& c, const Model& m, Sheet kind, Color tone, const char* tag, const char* title, const char* body) {
    sheetBackdrop(c,m,kind,true);
    sheet(c,48,48,620,984,40);
    c.disc(113,113,32,tone);
    c.roundRect(110,96,6,22,3,faceText);c.disc(113,128,3.5f,faceText);
    eyebrow(c,81,178,tag,tone,15,553);
    const unsigned lines=drawWrapped(c,Face::black,40,81,196,48,title,553,3,face);
    const float y=196+lines*48.0f+20;
    return y+drawWrapped(c,Face::semibold,20,81,y,30,body,553,5,muted)*30.0f;
}
void sheetActions(Canvas& c, const Model& m, const Button& confirm, const Button& cancel) {
    actionButton(c,confirm,81,784,80,553,22,34);
    focusRing(c,m,1,81,784,553,80,26);
    actionButton(c,cancel,81,876,72,553,20,30);
    const Hint h[]={{Glyph::cross,confirm.text},{Glyph::circle,cancel.text}};
    hintRow(c,h,2,81,984);
}
void signOutConfirm(Canvas& c, const Model& m) {
    confirmSheet(c,m,Sheet::signOut,coral,"Sign out","Sign out of NVIDIA on this PS5?",
        "The saved login is deleted from this PS5. Signing out doesn\xE2\x80\x99t remove games from your GeForce NOW library or change purchases on Steam, Epic or other stores.");
    sheetActions(c,m,{Glyph::cross,"Sign out",true},{Glyph::circle,"Cancel"});
}
void clearConfirm(Canvas& c, const Model& m) {
    char head[64],removed[48];
    const bool known=m.disk.available;
    const double megabytes=double(m.disk.bytes)/(1024.0*1024.0);
    if(known)std::snprintf(head,sizeof(head),megabytes<10?"Clear %.1f MB of cover art?":"Clear %.0f MB of cover art?",megabytes);
    else std::snprintf(head,sizeof(head),"Clear saved cover art?");
    float y=confirmSheet(c,m,Sheet::clear,coral,"Cover art cache",head,"Covers download again the next time you open Library or Store.")+36;
    rule(c,81,y,553);y+=34;
    eyebrow(c,81,y,"Removed",coral,13);
    eyebrow(c,360,y,"Kept",mint,13);
    y+=34;
    if(known)std::snprintf(removed,sizeof(removed),"%zu saved cover image%s",m.disk.count,m.disk.count==1?"":"s");
    else std::snprintf(removed,sizeof(removed),"All saved cover images");
    drawWrapped(c,Face::bold,18,81,y-13,26,removed,250,2,face);
    for(const char* kept:{"NVIDIA sign-in","Stream settings","Your Library list"}){label(c,Face::bold,18,360,y,kept,face);y+=32;}
    sheetActions(c,m,{Glyph::cross,"Clear cache",true},{Glyph::circle,"Keep"});
}
void fixedConfirm(Canvas& c, const Model& m) {
    float y=confirmSheet(c,m,Sheet::fixed,yellow,"Hardware H.264","Use fixed-resolution streaming with hardware decoding?",
        "Hardware H.264 can\xE2\x80\x99t follow resolution changes during a stream yet, so the cloud must keep one fixed resolution. This only changes the draft; it still needs Save.")+36;
    rule(c,81,y,553);y+=36;
    const auto change=[&](const char* key,const char* from,const char* to) {
        label(c,Face::bold,17,81,y,key,muted);
        y+=30;
        const float fw=label(c,Face::mono,17,81,y,from,muted);
        const float ax=81+fw+14;
        c.line(ax,y,ax+18,y,2.4f,muted);c.line(ax+12,y-6,ax+18,y,2.4f,muted);c.line(ax+12,y+6,ax+18,y,2.4f,muted);
        label(c,Face::mono,17,ax+32,y,to,face);
        y+=42;
    };
    change("Decoding",m.draft.hardware()?(m.draft.tenBit()?"Hardware HEVC":"Hardware H.264"):"Software","Hardware H.264");
    change("Resolution policy","Adaptive","Fixed");
    sheetActions(c,m,{Glyph::cross,"Use fixed resolution",true},{Glyph::circle,"Keep current draft"});
}
void numberEditor(Canvas& c, const Model& m) {
    const auto& e=m.edit;
    const StreamSettings candidate=applyEdit(e,m.draft);
    const auto error=validateSettings(candidate);
    sheetBackdrop(c,m,Sheet::editor,false);
    const float x=440, y=168, w=1040, h=716;
    sheet(c,x,y,w,h,40);
    const bool resolution=e.row==StreamRow::resolution;
    eyebrow(c,x+48,y+52,"Streaming \xC2\xB7 exact value",accent,14);
    label(c,Face::black,36,x+48,y+96,resolution?"Custom resolution":e.row==StreamRow::fps?"Frame rate":"Bitrate limit",face,-0.01f);
    char was[48];
    if(resolution)std::snprintf(was,sizeof(was),"was %d \xC3\x97 %d",m.draft.width,m.draft.height);
    else if(e.row==StreamRow::fps)std::snprintf(was,sizeof(was),"was %d FPS",m.draft.fps);
    else std::snprintf(was,sizeof(was),"was %d Mb/s",m.draft.bitrate_kbps/1000);
    labelRight(c,Face::mono,16,x+w-48,y+96,was,muted);
    const auto bad=[&](unsigned from) {
        if(!resolution)return error!=SettingsError::none;
        const int v=editNumber(e,from,4);
        return from==0?(v<320||v>maxWidth(candidate)||(v&1)):(v<180||v>maxHeight(candidate)||(v&1));
    };
    const float cellsY=y+200;
    const auto group=[&](float gx,const char* name,unsigned from,unsigned count) {
        const bool wrong=bad(from);
        eyebrow(c,gx,y+160,name,wrong?coral:muted,13);
        const float gw=count*92.0f-8;
        if(wrong)c.roundRectStroke(gx-8,cellsY-8,gw+16,132,26,1.5f,coral,153);
        for(unsigned i=0;i<count;++i) {
            const unsigned index=from+i;
            const float cx=gx+i*92.0f;
            const bool active=index==e.cursor;
            if(active) {
                c.roundRect(cx,cellsY,84,116,20,face,26);
                focusRing(c,m,1,cx,cellsY,84,116,20);
                c.line(cx+32,cellsY-14,cx+42,cellsY-24,3,accent);c.line(cx+42,cellsY-24,cx+52,cellsY-14,3,accent);
                c.line(cx+32,cellsY+130,cx+42,cellsY+140,3,accent);c.line(cx+42,cellsY+140,cx+52,cellsY+130,3,accent);
            } else {
                c.roundRect(cx,cellsY,84,116,20,face,18);
                c.roundRectStroke(cx,cellsY,84,116,20,1,face,seamAlpha);
            }
            const char digit[2]{e.digits[index],0};
            labelCenter(c,Face::black,64,cx+42,cellsY+58,digit,face);
        }
        return gw;
    };
    float gx=x+48;
    if(resolution) {
        gx+=group(gx,"Width",0,4)+28;
        label(c,Face::black,48,gx,cellsY+58,"\xC3\x97",muted);
        group(gx+56,"Height",4,4);
    } else group(gx,e.row==StreamRow::fps?"Frames per second":"Megabits per second",0,3);
    const char* problem=settingsProblem(candidate,true);
    const float sy=y+392;
    if(problem){dot(c,x+56,sy,coral);label(c,Face::black,20,x+72,sy,problem,coral);}
    else if(!m.editAvailable){dot(c,x+56,sy,yellow);label(c,Face::black,20,x+72,sy,"Not qualified for this PS5 and output \xC2\xB7 Save stays off",yellow);}
    else {dot(c,x+56,sy,mint);label(c,Face::black,20,x+72,sy,"Valid",mint);}
    const char* range=resolution?(candidate.hardware()?"Width 320 to 3840, height 180 to 2160, both even.":"Software decoding: width 320 to 1920, height 180 to 1080, both even."):
        e.row==StreamRow::fps?(candidate.hardware()?"Any whole number from 30 to 120.":"Software decoding: 30 to 60."):"Any whole number from 4 to 100.";
    label(c,Face::semibold,17,x+72,sy+32,range,muted);
    rule(c,x+48,y+472,w-96);
    eyebrow(c,x+48,y+510,resolution?"Common sizes":"Common values",muted,13);
    float cx=x+48;
    char chip[32];
    const auto drawChip=[&](const char* t) {
        const float cw=textWidth(Face::mono,16,t)+36;
        if(cx+cw>x+w-48)return;
        c.roundRect(cx,y+536,cw,44,22,face,18);
        c.roundRectStroke(cx,y+536,cw,44,22,1,face,seamAlpha);
        label(c,Face::mono,16,cx+18,y+558,t,face);
        cx+=cw+10;
    };
    if(resolution){for(const auto& size:commonSizes)if(size.width<=maxWidth(candidate)&&size.height<=maxHeight(candidate)){std::snprintf(chip,sizeof(chip),"%d \xC3\x97 %d",size.width,size.height);drawChip(chip);}}
    else if(e.row==StreamRow::fps){for(int v:commonFps)if(v<=maxFps(candidate)){std::snprintf(chip,sizeof(chip),"%d",v);drawChip(chip);}}
    else for(int v:commonBitrates){std::snprintf(chip,sizeof(chip),"%d Mb/s",v);drawChip(chip);}
    const Hint l[]={{Glyph::dpad,"Pick digit / change"},{Glyph::l1,resolution?"Common sizes":"Common values",Glyph::r1,nullptr,face,"/"}};
    const Hint r[]={{Glyph::circle,"Cancel"},{Glyph::cross,problem?"Done (fix first)":"Done",Glyph::none,nullptr,problem?muted:face}};
    hintRow(c,l,2,x+48,y+h-50);
    hintRow(c,r,2,x+w-48-hintRowWidth(r,2),y+h-50);
}

void homeTileSheet(Canvas& c, const Model& m) {
    const auto& t=m.tile;
    const bool working=t.stage==HomeTile::Stage::working;
    const Color tone=working?accent:t.ok?mint:coral;
    sheetBackdrop(c,m,Sheet::tile,true);
    sheet(c,48,48,620,984,40);
    c.disc(113,113,32,tone);
    if(working){c.roundRectStroke(99,99,28,28,7,3,faceText);c.roundRect(107,107,12,12,3,faceText);}
    else if(t.ok){c.line(100,114,109,123,4.5f,faceText);c.line(109,123,127,103,4.5f,faceText);}
    else {c.roundRect(110,96,6,22,3,faceText);c.disc(113,128,3.5f,faceText);}
    eyebrow(c,81,178,working?"Add to Home Screen":t.ok?"Tile ready":"Tile not created",tone,15,553);
    const unsigned lines=drawWrapped(c,Face::black,40,81,196,48,t.game.title,553,3,face);
    float y=196+lines*48.0f+6;
    label(c,Face::bold,18,81,y+12,storeName(t.game.store),muted);
    y+=56;
    if(working) {
        label(c,Face::black,20,81,y,t.cancelling?"Cancelling\xE2\x80\xA6":t.step,face);
        char percent[8];std::snprintf(percent,sizeof(percent),"%u%%",std::min(t.percent,100U));
        labelRight(c,Face::mono,16,634,y,percent,muted);
        y+=28;
        c.roundRect(81,y,553,8,4,face,41);
        c.roundRect(81,y,std::max(8.0f,553.0f*follow(m,motionKey(barAnim,2),static_cast<float>(std::min(t.percent,100U))/100.0f,10,0.0005f)),8,4,accent);
        y+=42;
        drawWrapped(c,Face::semibold,18,81,y,27,"OpenNOW downloads the cover and hero art and writes a launcher tile to this PS5. Keep OpenNOW open until it finishes.",553,4,muted);
        actionButton(c,{Glyph::circle,t.cancelling?"Cancelling\xE2\x80\xA6":"Cancel"},81,876,72,553,20,30,!t.cancelling);
        if(!t.cancelling)focusRing(c,m,1,81,876,553,72,26);
        if(!t.cancelling){const Hint h[]={{Glyph::circle,"Cancel"}};hintRow(c,h,1,81,984);}
        return;
    }
    y+=drawWrapped(c,Face::semibold,20,81,y,30,t.message,553,4,t.ok?face:coral)*30.0f+24;
    if(t.ok&&*t.titleId) {
        rule(c,81,y,553);y+=32;
        eyebrow(c,81,y,"Title ID",muted,13);
        labelRight(c,Face::mono,18,634,y,t.titleId,face,0.06f);
        y+=36;
    } else if(!t.ok&&t.error) {
        char code[32];std::snprintf(code,sizeof(code),"Error %d",t.error);
        rule(c,81,y,553);y+=32;
        eyebrow(c,81,y,"Details",muted,13);
        labelRight(c,Face::mono,18,634,y,code,face);
        y+=36;
    }
    if(t.ok) {
        y+=12;
        c.roundRect(81,y,553,96,22,mint,26);
        c.roundRectStroke(81,y,553,96,22,1,mint,110);
        eyebrow(c,105,y+28,"Next step",mint,13);
        drawWrapped(c,Face::black,18,105,y+44,26,"Rescan homebrew in ShadowMountPlus to show the tile",505,2,face);
    }
    actionButton(c,{Glyph::cross,"Done",true},81,876,72,553,20,30);
    focusRing(c,m,1,81,876,553,72,26);
    const Hint h[]={{Glyph::cross,"Done"},{Glyph::circle,"Close"}};
    hintRow(c,h,2,81,984);
}

void settings(Canvas& c, const Model& m) {
    screenBase(c,shell);
    const Layer page=pageLayer(c,m);
    settingsSidebar(c,m);
    panel(c,480,124,1344,792,40);
    const float pane=reveal(m,motionKey(paneAnim,2,static_cast<std::uint32_t>(m.pane)),30);
    layer(c,page.opacity*(0.4f+0.6f*pane),page.dx,page.dy+14*(1-pane));
    if(m.pane==SettingsPane::stream||m.pane==SettingsPane::controller)draftCard(c,m);
    label(c,Face::black,34,537,175,paneName(m.pane),face,-0.01f);
    switch(m.pane) {
    case SettingsPane::stream: streamPane(c,m);break;
    case SettingsPane::display: displayPane(c,m);break;
    case SettingsPane::controller: controllerPane(c,m);break;
    case SettingsPane::cache: cachePane(c,m);break;
    case SettingsPane::account: accountPane(c,m);break;
    case SettingsPane::about: aboutPane(c,m);break;
    }
    const bool dialog=settingsDialog(m);
    const unsigned rows=settingsRows(m.pane);
    Hint left[2]{},right[2]{};
    unsigned l=0,r=0;
    if(!m.settingsContent) {
        left[l++]={Glyph::circle,"Close app"};
        if(rows)right[r++]={Glyph::cross,"Select"};
    } else if(m.pane==SettingsPane::stream||m.pane==SettingsPane::controller) {
        const bool canSave=changedRows(m.draft,m.defaults)&&!settingsProblem(m.draft,m.draftAvailable);
        left[l++]={Glyph::circle,"Sections"};
        left[l++]={Glyph::square,"Revert"};
        right[r++]={Glyph::options,"Save",Glyph::none,nullptr,canSave?mint:muted};
        if(m.pane==SettingsPane::controller)
            right[r++]=m.settingsRow<static_cast<unsigned>(ControllerRow::persistGameSettings)?Hint{Glyph::dpad,"Adjust"}:Hint{Glyph::cross,"Toggle"};
        else {
            const auto row=static_cast<StreamRow>(m.settingsRow);
            if(row==StreamRow::resolution||row==StreamRow::fps||row==StreamRow::bitrate||row==StreamRow::reset)
                right[r++]={Glyph::cross,row==StreamRow::reset?"Reset":"Edit"};
        }
    } else {
        left[l++]={Glyph::circle,"Sections"};
        right[r++]={Glyph::cross,"Select"};
    }
    chrome(c,m,"Settings",true,left,l,right,r,!dialog);
    if(m.edit.open())numberEditor(c,m);
    else if(m.confirmClear)clearConfirm(c,m);
    else if(m.confirmFixed)fixedConfirm(c,m);
    else if(m.confirmSignOut)signOutConfirm(c,m);
}

void signIn(Canvas& c, const Model& m) {
    const auto& v=m.login;
    const bool waiting=v.state==State::waiting, requesting=v.state==State::requesting;
    const bool failed=v.state==State::failed||v.state==State::denied||v.state==State::expired;
    screenBase(c,shell);
    pageLayer(c,m);
    const char* tag=waiting?"Sign in with NVIDIA":!failed?"Sign in":v.state==State::expired?"Code expired":v.state==State::denied?"Sign-in declined":"Sign-in problem";
    eyebrow(c,120,217,tag,failed?coral:accent,16);
    const char* head=waiting?"Scan the code with your phone":requesting?"Contacting NVIDIA\xE2\x80\xA6":failed?"Sign-in didn\xE2\x80\x99t finish":"Bring your games to the big screen.";
    const unsigned lines=drawWrapped(c,Face::black,64,120,250,90,head,880,2,face);
    float y=250+lines*90.0f+20;
    char text[640];
    if(waiting) {
        string_view url(v.url);
        if(url.rfind("https://",0)==0)url.remove_prefix(8);
        std::snprintf(text,sizeof(text),"Or visit %.*s on any device and enter this code. Your login stays saved on this PS5.",static_cast<int>(url.size()),url.data());
    } else if(requesting||failed)std::snprintf(text,sizeof(text),"%s",v.message);
    else std::snprintf(text,sizeof(text),"OpenNOW connects to your GeForce NOW account without storing your NVIDIA password. Sign in from your phone, then come straight back to the controller.");
    y+=drawWrapped(c,Face::semibold,22,120,y,37,text,800,3,failed?coral:muted)*37.0f;
    if(waiting) {
        string_view code(v.code);
        unsigned cells=0;for(char ch:code)cells+=ch!='-';
        const float cell=cells>10?56:72;
        float x=120;y+=36;
        for(char ch:code) {
            if(ch=='-'){c.roundRect(x+4,y+45,20,5,2.5f,face,90);x+=28+10;continue;}
            c.roundRect(x,y,cell,96,20,face,18);
            c.roundRectStroke(x,y,cell,96,20,1,face,seamAlpha);
            const char letter[2]{ch,0};
            labelCenter(c,Face::mono,48,x+cell/2,y+48,letter,face);
            x+=cell+10;
        }
    } else {
        const Button start{Glyph::cross,failed?"Try again":"Sign in with NVIDIA",true};
        actionButton(c,start,120,668,72,640,22,34,!requesting);
        if(!requesting)focusRing(c,m,0,120,668,640,72,26);
    }
    char status[96];
    Color tone=muted;
    if(waiting){std::snprintf(status,sizeof(status),"Waiting for approval \xC2\xB7 code expires in %u:%02u",v.expiresIn/60,v.expiresIn%60);tone=mint;}
    else if(requesting){std::snprintf(status,sizeof(status),"Talking to NVIDIA\xE2\x80\xA6");tone=accent;}
    else std::snprintf(status,sizeof(status),"Unofficial client \xC2\xB7 not affiliated with NVIDIA or Sony");
    dot(c,125,859,tone);
    label(c,Face::semibold,18,142,859,status,muted);
    panel(c,1281,275,478,530,40,true);
    c.roundRect(1340,314,360,360,26,face);
    static unsigned char temp[qrcodegen_BUFFER_LEN_MAX],qr[qrcodegen_BUFFER_LEN_MAX];
    if(waiting&&qrcodegen_encodeText(v.qrUrl,temp,qr,qrcodegen_Ecc_MEDIUM,1,20,qrcodegen_Mask_AUTO,true)) {
        const int size=qrcodegen_getSize(qr);
        const int module=std::max(2,296/size);
        const int origin=static_cast<int>(1520-module*size/2.0f), top=static_cast<int>(494-module*size/2.0f);
        for(int qy=0;qy<size;++qy)for(int qx=0;qx<size;++qx)
            if(qrcodegen_getModule(qr,qx,qy))c.rectangle(static_cast<unsigned>(origin+qx*module),static_cast<unsigned>(top+qy*module),module,module,black);
    } else {
        c.ring(1520,481,32,5,faceText);c.ring(1520,481,13,5,faceText);
        labelCenter(c,Face::black,19,1520,558,requesting?"Getting a code\xE2\x80\xA6":"Ready when you are",faceText);
    }
    labelCenter(c,Face::black,22,1520,708,"Scan with your phone",face);
    labelCenter(c,Face::semibold,16,1520,754,waiting?"Opens NVIDIA sign-in with this code filled in":"A QR code appears after sign-in starts",muted);
    const Hint left[]={closeAppHint};
    const Hint right[]={{Glyph::cross,failed?"Try again":"Sign in"}};
    chrome(c,m,"Welcome to OpenNOW",false,left,1,right,waiting||requesting?0:1);
}

void searchScreen(Canvas& c, const Model& m) {
    storeScreen(c,m,false);
    sheetBackdrop(c,m,Sheet::search,false);
    const auto& s=m.search;
    const float x=96, y=300, w=1728, h=616;
    sheet(c,x,y,w,h,40);
    eyebrow(c,x+40,y+44,"Search the GeForce NOW catalog",accent,15);
    char count[32];std::snprintf(count,sizeof(count),"%zu / %zu",std::strlen(s.text),sizeof(s.text)-1);
    labelRight(c,Face::mono,15,x+w-40,y+44,count,muted);
    const float fx=x+28, fy=y+72, fw=w-56, fh=84, cy=fy+fh/2;
    c.roundRect(fx,fy,fw,fh,28,face,18);
    c.roundRectStroke(fx,fy,fw,fh,28,1,face,seamAlpha);
    c.ring(fx+42,cy-4,11,3,face);c.line(fx+50,cy+4,fx+58,cy+12,3.4f,face);
    const float room=fw-110-40;
    string_view text(s.text);
    while(!text.empty()&&textWidth(Face::black,36,text)>room)text.remove_prefix(1);
    float caret=fx+84;
    if(text.empty())label(c,Face::black,36,fx+84,cy,"Type a game title",muted);
    else caret+=label(c,Face::black,36,fx+84,cy,text,face)+4;
    c.roundRect(caret,cy-22,4,44,2,accent);
    const float x0=x+(w-1228)/2, y0=y+186;
    for(unsigned i=0;i<CatalogSearch::count;++i) {
        const unsigned col=CatalogSearch::column(i), row=CatalogSearch::row(i);
        const float kx=x0+col*124.0f, ky=y0+row*72.0f;
        const bool focused=i==s.selected;
        if(focused){c.roundRect(kx,ky,112,60,22,face);focusRing(c,m,1,kx,ky,112,60,22);}
        else {c.roundRect(kx,ky,112,60,22,face,18);c.roundRectStroke(kx,ky,112,60,22,1,face,seamAlpha);}
        const Color ink=focused?faceText:face;
        if(CatalogSearch::keys[i]==' ') {
            c.line(kx+38,ky+26,kx+38,ky+38,3,ink);c.line(kx+38,ky+38,kx+74,ky+38,3,ink);c.line(kx+74,ky+38,kx+74,ky+26,3,ink);
        } else {
            const char key[2]{CatalogSearch::keys[i],0};
            labelCenter(c,Face::black,30,kx+56,ky+30,key,ink);
        }
    }
    label(c,Face::semibold,17,x+40,y+h-36,"Searches the full NVIDIA catalog by title. Results open in Store.",muted);
    const Hint left[]={{Glyph::circle,"Cancel"},{Glyph::square,"Backspace"},{Glyph::triangle,"Clear"}};
    const Hint right[]={{Glyph::cross,"Type"},{Glyph::options,"Search",Glyph::none,nullptr,*s.text?mint:face}};
    chrome(c,m,"Search",false,left,3,right,2);
}

void gameHeader(Canvas& c, const Game& game) {
    c.roundRect(120,72,42,42,10,storeInfo(game.store).color);
    c.roundRectStroke(120,72,42,42,10,1,face,seamAlpha);
    storeMark(c,141,93,28,game.store);
    fitted(c,Face::black,19,178,84,game.title,900,face);
    label(c,Face::bold,15,178,105,storeName(game.store),muted);
}
void statusPill(Canvas& c, float rightX, float cy, const char* text, Color tone) {
    const float w=22+9+10+textWidth(Face::mono,15,text,0.12f)+22, x=rightX-w;
    c.roundRect(x,cy-22,w,44,22,tone,26);
    c.roundRectStroke(x,cy-22,w,44,22,1.2f,tone,140);
    dot(c,x+22+4.5f,cy,tone);
    label(c,Face::mono,15,x+22+9+10,cy,text,tone,0.12f);
}
void stageLine(Canvas& c, const Model& m, float cy, int number, const char* text, const char* sub, int state, Color tone) {
    const float cx=140;
    if(state==2){c.disc(cx,cy,20,mint);c.line(cx-8,cy,cx-2.5f,cy+6,3,faceText);c.line(cx-2.5f,cy+6,cx+8,cy-6,3,faceText);}
    else if(state==1&&m.streaming){c.ring(cx,cy,19,2.5f,tone);c.disc(cx,cy,6,tone);}
    else if(state==1) {
        c.ring(cx,cy,19,2.5f,tone,60);
        c.arc(cx,cy,19,2.5f,loopPhase(m,1.2f)*6.2831853f,1.9f,tone);
        c.disc(cx,cy,6,tone);
    }
    else {
        c.ring(cx,cy,19.5f,1.2f,face,56);
        char n[4];std::snprintf(n,sizeof(n),"%d",number);
        labelCenter(c,Face::mono,15,cx,cy,n,muted);
    }
    const bool two=state==1&&sub;
    label(c,Face::black,21,178,two?cy-10:cy,text,state==0?muted:face);
    if(two)label(c,Face::semibold,15,178,cy+15,sub,muted);
}
void progress(Canvas& c, const Model& m) {
    const auto& cv=m.session;
    const bool connecting=m.screen==Screen::connecting, queued=cv.state==CloudState::queued;
    const bool queue=queued&&cv.queuePosition>=0, setup=queued&&!queue&&cv.setupStep>=0;
    const Game* game=*cv.current.title?&cv.current:nullptr;
    stageBackground(c,m,game);
    pageLayer(c,m);
    if(game)gameHeader(c,*game);
    const Color tone=queue?yellow:accent;
    statusPill(c,1800,93,connecting?"CONNECTING":queue?"IN QUEUE":queued?"PREPARING":"STARTING",tone);
    eyebrow(c,120,223,connecting?"Connecting your stream":"Starting your session",tone,16);
    const char* head=connecting?"Connecting stream":queue?"You\xE2\x80\x99re in line":setup?"Preparing your game":queued?"Almost ready":"Starting your session";
    const char* text=connecting?"Your cloud rig is ready. The picture appears as soon as the first video frame arrives.":
        queue?"NVIDIA is finding a cloud rig for your session. This screen moves on by itself, so you can put the controller down.":
        queued?"NVIDIA is getting the server ready. This screen moves on by itself.":"Asking NVIDIA for a cloud session. This usually takes a few seconds.";
    const bool dial=queue||setup;
    fitted(c,Face::black,80,120,293,head,dial?1060:1680,face,-0.02f);
    drawWrapped(c,Face::semibold,22,120,356,33,text,860,3,muted);
    const int active=connecting?3:queue?1:queued?2:0;
    const float rail=follow(m,motionKey(railAnim),static_cast<float>(active),12,0.002f);
    for(int i=0;i<3;++i) {
        const float top=663+i*66.0f+22, length=66-44;
        c.roundRect(139,top,2,length,1,face,46);
        const float filled=std::clamp(rail-static_cast<float>(i),0.0f,1.0f)*length;
        if(filled>0)c.roundRect(139,top,2,filled,1,mint);
    }
    stageLine(c,m,663,1,"Session requested",nullptr,active>0?2:1,tone);
    stageLine(c,m,729,2,"Waiting for a rig","Position comes from GeForce NOW",active>1?2:active==1?1:0,tone);
    stageLine(c,m,795,3,"Setting up the rig",nullptr,active>2?2:active==2?1:0,tone);
    stageLine(c,m,861,4,"Connecting the stream",nullptr,active==3?1:0,tone);
    if(dial) {
        constexpr float cx=1500, cy=510;
        c.ring(cx,cy,258,1.5f,face,56);
        if(!m.streaming)c.arc(cx,cy,258,3,loopPhase(m,4)*6.2831853f,0.8f,tone,200);
        labelCenter(c,Face::mono,16,cx,cy-121,queue?"QUEUE POSITION":"SETUP STEP",face,0.12f);
        char number[16];std::snprintf(number,sizeof(number),"%d",queue?cv.queuePosition:cv.setupStep);
        const float size=std::strlen(number)>3?130:200;
        labelCenter(c,Face::black,size,cx,cy-7,number,tone,-0.03f);
        labelCenter(c,Face::bold,19,cx,cy+118,"Updates when GeForce NOW reports it",muted);
    }
    const Hint end{Glyph::options,m.streaming?"End session":queue?"Leave queue":"Cancel session",Glyph::touchpad,"Hold"};
    hintRow(c,&end,1,1800-hintRowWidth(&end,1));
    if(m.streaming) {
        const Hint left[]={{Glyph::touchpad,"Mouse"},{Glyph::options,"Keyboard",Glyph::triangle},{Glyph::options,"Back",Glyph::square}};
        hintRow(c,left,3,120);
        fitted(c,Face::mono,15,120,930,cv.message,1200,muted);
    } else fitted(c,Face::mono,15,120,hintY,cv.message,1200,muted);
}

void failure(Canvas& c, const Model& m) {
    const auto& cv=m.session;
    const Screen s=m.screen;
    const Game* game=*cv.current.title?&cv.current:nullptr;
    stageBackground(c,m,game);
    pageLayer(c,m);
    if(game)gameHeader(c,*game);
    statusPill(c,1800,93,s==Screen::streamEnded?"STREAM ENDED":s==Screen::cleanupFailed?"SESSION RUNNING":"NOT STARTED",coral);
    eyebrow(c,120,223,s==Screen::streamEnded?"Stream ended":s==Screen::cleanupFailed?"Session still running":"Couldn\xE2\x80\x99t start",coral,16);
    const char* head=s==Screen::streamEnded?"The connection to your game dropped":s==Screen::cleanupFailed?"We couldn\xE2\x80\x99t end the cloud session":"The game didn\xE2\x80\x99t start";
    const char* text=s==Screen::streamEnded?"OpenNOW stopped the cloud session, so nothing keeps running on your account. You can try again right away.":
        s==Screen::cleanupFailed?"It may still be running on your account. End it before starting another game.":
        "The cloud session could not be started or confirmed. You can try again or go back.";
    const unsigned lines=drawWrapped(c,Face::black,72,120,250,80,head,1200,2,face);
    float y=250+lines*80.0f+16;
    y+=drawWrapped(c,Face::semibold,22,120,y,33,text,900,2,muted)*33.0f+32;
    char second[256]{};
    if(game) {
        char name[48];
        std::snprintf(second,sizeof(second),"%s \xC2\xB7 %s \xC2\xB7 %s",cv.current.title,storeName(cv.current.store),settingsName(m.launch,name,sizeof(name)).data());
    }
    const float ch=*second?104:72;
    panel(c,120,y,980,ch,26,true);
    c.roundRect(120+18,y+18,4,ch-36,2,coral);
    fitted(c,Face::mono,17,148,y+36,*cv.message?cv.message:"No details reported",930,face);
    if(*second)fitted(c,Face::semibold,16,148,y+70,second,930,muted);
    y+=ch+40;
    if(s==Screen::cleanupFailed) {
        const Hint end{Glyph::options,"End session",Glyph::touchpad,"Hold"};
        const float w=hintWidth(end)+48;
        c.roundRect(120,y,w,72,26,face);
        focusRing(c,m,0,120,y,w,72,26);
        drawHint(c,end,144,y+36,hintSize,hintLabel);
    } else {
        const float pw=buttonWidth({Glyph::cross,"Try again",true});
        actionButton(c,{Glyph::cross,"Try again",true},120,y);
        focusRing(c,m,0,120,y,pw,72,26);
        actionButton(c,s==Screen::streamEnded?Button{Glyph::square,"Back to games"}:Button{Glyph::circle,"Back"},120+pw+14,y);
    }
    const Hint left[]={s==Screen::launchFailed?Hint{Glyph::circle,"Back"}:closeAppHint};
    hintRow(c,left,1,120);
    if(s==Screen::cleanupFailed) {
        const Hint end{Glyph::options,"End session",Glyph::touchpad,"Hold"};
        hintRow(c,&end,1,1800-hintRowWidth(&end,1));
    } else {
        const Hint right[]={{Glyph::square,"Back to games"},{Glyph::cross,"Try again"}};
        const unsigned n=s==Screen::streamEnded?2:1;
        hintRow(c,right+2-n,n,1800-hintRowWidth(right+2-n,n));
    }
}

void keyGrid(Canvas& c, const StreamKeyboard& k, bool ready, float x0, float y0, float span, float keyH, float rowPitch) {
    const float pitch=span/StreamKeyboard::units, big=keyH*0.40f, small=keyH*0.28f;
    for(unsigned i=0;i<StreamKeyboard::count;++i) {
        const auto& key=StreamKeyboard::keys[i];
        const float x=x0+StreamKeyboard::column(i)*pitch, y=y0+StreamKeyboard::row(i)*rowPitch, w=key.width*pitch-12;
        const bool focused=i==k.selected, latched=k.latched(i);
        if(focused){c.roundRect(x,y,w,keyH,18,face);focusFrame(c,x,y,w,keyH,18);}
        else if(latched){c.roundRect(x,y,w,keyH,18,mint,46);c.roundRectStroke(x,y,w,keyH,18,1.5f,mint,170);}
        else {c.roundRect(x,y,w,keyH,18,face,18);c.roundRectStroke(x,y,w,keyH,18,1,face,seamAlpha);}
        const Color color=focused?faceText:latched?mint:ready?face:muted;
        const float cy=y+keyH/2;
        if(key.vk>=0x25&&key.vk<=0x28) {
            const float cx=x+w/2, a=keyH*0.15f, dx=key.vk==0x25?-1.0f:key.vk==0x27?1.0f:0.0f, dy=key.vk==0x26?-1.0f:key.vk==0x28?1.0f:0.0f;
            c.line(cx-dx*a,cy-dy*a,cx+dx*a,cy+dy*a,3.4f,color);
            c.line(cx+dx*a,cy+dy*a,cx+dx*a*0.25f-dy*a*0.75f,cy+dy*a*0.25f+dx*a*0.75f,3.4f,color);
            c.line(cx+dx*a,cy+dy*a,cx+dx*a*0.25f+dy*a*0.75f,cy+dy*a*0.25f-dx*a*0.75f,3.4f,color);
            continue;
        }
        const char* name=k.label(i);
        const bool word=name[1]!=0;
        labelCenter(c,word?Face::extrabold:Face::black,word?small:big,x+w/2,cy,name,color);
    }
}
const char* modifierText(const StreamKeyboard& k, char* buffer, std::size_t size) {
    std::size_t at=0;buffer[0]=0;
    for(const auto& [on,name]:{std::pair{k.shift,"SHIFT"},std::pair{k.caps,"CAPS LOCK"},std::pair{k.ctrl,"CTRL"},std::pair{k.alt,"ALT"}})
        if(on)at+=std::snprintf(buffer+at,size-at,"%s%s",at?" + ":"",name);
    return buffer;
}
void streamKeyboard(Canvas& c, const Model& m) {
    const auto& k=m.keyboard;
    const Game* game=*m.session.current.title?&m.session.current:nullptr;
    stageBackground(c,m,game);
    if(game)gameHeader(c,*game);
    statusPill(c,1800,93,m.inputReady?"INPUT READY":"WAITING FOR INPUT",m.inputReady?mint:yellow);
    eyebrow(c,120,223,game?"Keyboard \xC2\xB7 typing into your game":"Keyboard",accent,16);
    fitted(c,Face::black,56,120,276,m.inputReady?"Keys go straight to your game":"Waiting for game input",1100,m.inputReady?face:yellow,-0.02f);
    char state[48];
    modifierText(k,state,sizeof(state));
    labelRight(c,Face::mono,15,1800,276,*state?state:"NO MODIFIERS",*state?mint:muted,0.12f);
    const unsigned rows=StreamKeyboard::row(StreamKeyboard::count-1)+1;
    panel(c,96,328,1728,rows*96.0f+44,36,true);
    keyGrid(c,k,m.inputReady,124,350,1684,84,96);
    label(c,Face::semibold,17,120,930,m.inputReady?"The picture returns when you close the keyboard. Your game and audio keep running.":
        "Key presses are ignored until the game\xE2\x80\x99s input channel opens. The picture returns when you close the keyboard.",muted);
    const Hint left[]={{Glyph::circle,"Back to game"}};
    const Hint right[]={{Glyph::l1,"Shift"},{Glyph::square,"Backspace"},{Glyph::triangle,"Space"},{Glyph::cross,"Press key"}};
    hintRow(c,left,1,120);
    hintRow(c,right,4,1800-hintRowWidth(right,4));
}
}

void renderOverlay(Canvas& c, const Overlay& o) noexcept {
    loadFonts();
    c.resetClip();layer(c,1,0,0);
    const float x=64, y=40, w=1792, h=72, cy=y+h/2;
    c.roundRect(x,y,w,h,36,shell);
    c.roundRectStroke(x,y,w,h,36,1,face,seamAlpha);
    const auto& k=o.keyboard;
    const char* mode=!o.inputReady?"WAITING FOR GAME INPUT":k.open?(o.launcher?"KEYBOARD \xC2\xB7 MOUSE PAUSED":"KEYBOARD"):"LAUNCHER MOUSE";
    const Color tone=o.inputReady?accent:yellow;
    dot(c,x+30,cy,tone);
    label(c,Face::mono,15,x+46,cy,mode,tone,0.12f);
    static const char* speeds[]={"Move \xC2\xB7 Slow","Move \xC2\xB7 Normal","Move \xC2\xB7 Fast"};
    Hint items[8]{};unsigned count=0;
    if(!k.open) {
        items[count++]={Glyph::stick,speeds[std::min(o.speed,2U)]};
        items[count++]={Glyph::r2,"Left click"};
        items[count++]={Glyph::l2,"Right click"};
        items[count++]={Glyph::dpad,"Scroll"};
        items[count++]={Glyph::square,"Speed"};
        items[count++]={Glyph::triangle,"Keyboard"};
    } else items[count++]={Glyph::circle,o.launcher?"Back to mouse":"Close"};
    items[count++]=o.launcher?Hint{Glyph::options,"Exit",Glyph::r3}:Hint{Glyph::options,"Close",Glyph::triangle};
    hintRow(c,items,count,x+w-28-hintRowWidth(items,count),cy);
    if(!k.open)return;
    const float ph=500, py=1080-40-ph;
    c.roundRect(x,py,w,ph,36,shell);
    c.roundRectStroke(x,py,w,ph,36,1,face,seamAlpha);
    eyebrow(c,x+32,py+38,o.inputReady?"Keyboard \xC2\xB7 US QWERTY \xC2\xB7 typed keys are not shown":"Key presses are ignored until the game\xE2\x80\x99s input channel opens",tone,14,1200);
    char state[48];
    modifierText(k,state,sizeof(state));
    if(*state)labelRight(c,Face::mono,14,x+w-32,py+38,state,mint,0.12f);
    keyGrid(c,k,o.inputReady,x+28,py+70,w-44,62,72);
    const Hint l[]={{Glyph::cross,"Press key"},{Glyph::square,"Backspace"},{Glyph::triangle,"Space"},{Glyph::l1,"Shift"},{Glyph::circle,o.launcher?"Back to mouse":"Close"}};
    hintRow(c,l,5,x+32,py+ph-36);
}

Screen screenFor(const Inputs& in) noexcept {
    if(in.streaming)return Screen::connecting;
    if(in.login.state!=State::authenticated)return Screen::signIn;
    const auto state=in.session.state;
    if(in.sessionOwned||state==CloudState::starting||state==CloudState::queued||state==CloudState::ready) {
        if(in.sessionOwned&&state==CloudState::failed)return Screen::cleanupFailed;
        return state==CloudState::ready?Screen::connecting:Screen::launching;
    }
    if(in.streamFailed)return Screen::streamEnded;
    if(in.session.launchError)return Screen::launchFailed;
    if(in.searchOpen)return Screen::search;
    if(in.section==Section::settings)return Screen::settings;
    const auto& view=catalogFor(in.section,in.session,in.library);
    if(view.count) {
        if(in.detailOpen&&view.state==CloudState::catalog)return Screen::detail;
        return Screen::grid;
    }
    if(view.state==CloudState::failed)return Screen::catalogError;
    if(view.state==CloudState::catalog)return Screen::catalogEmpty;
    return Screen::catalogLoading;
}

const CloudView& catalogFor(Section section,const CloudView& session,const CloudView& library) noexcept {
    return section==Section::library?library:session;
}

unsigned settingsRows(SettingsPane pane) noexcept {
    switch(pane) {
    case SettingsPane::stream: return static_cast<unsigned>(StreamRow::count);
    case SettingsPane::controller: return static_cast<unsigned>(ControllerRow::count);
    case SettingsPane::cache: return 2;
    case SettingsPane::account: return 1;
    case SettingsPane::display: case SettingsPane::about: return 0;
    }
    return 0;
}

unsigned qualifiedPresets(StreamProfile* out, unsigned capacity, bool (*available)(const StreamSettings&)) noexcept {
    unsigned count=0;
    StreamProfile p=StreamProfile::native_hdr120;
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i,p=nextProfile(p))
        if(available(settingsFor(p))&&count<capacity)out[count++]=p;
    return count;
}

unsigned storeSiblings(const CloudView& cloud, unsigned index, unsigned* out, unsigned capacity) noexcept {
    if(index>=cloud.count||!capacity)return 0;
    unsigned count=0;
    for(unsigned i=0;i<cloud.count&&count<capacity;++i)
        if(i==index||std::strcmp(cloud.games[i].title,cloud.games[index].title)==0)out[count++]=i;
    return count;
}

void render(Canvas& c, const Model& m) noexcept {
    loadFonts();
    c.resetClip();layer(c,1,0,0);
    switch(m.screen) {
    case Screen::signIn: signIn(c,m);break;
    case Screen::catalogLoading: case Screen::catalogEmpty: case Screen::catalogError: case Screen::grid:
        if(m.section==Section::library)libraryScreen(c,m);
        else storeScreen(c,m);
        break;
    case Screen::search: searchScreen(c,m);break;
    case Screen::detail:
        detail(c,m);
        if(m.tile.stage!=HomeTile::Stage::hidden)homeTileSheet(c,m);
        break;
    case Screen::settings: settings(c,m);break;
    case Screen::launching: case Screen::connecting: progress(c,m);break;
    case Screen::cleanupFailed: case Screen::streamEnded: case Screen::launchFailed: failure(c,m);break;
    case Screen::keyboard: streamKeyboard(c,m);break;
    }
    c.resetClip();layer(c,1,0,0);
}
}
