// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv_ui.hpp"
#include "font.hpp"
#include "../vendor/qrcodegen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace opennow::ui {
namespace {
using ps5::demo::Canvas;
using ps5::demo::Color;
using std::string_view;

constexpr Color rgb(unsigned hex) {
    return static_cast<Color>(0xff000000U|((hex&0xffU)<<16)|(hex&0xff00U)|((hex>>16)&0xffU));
}
constexpr Color ground=rgb(0x0D1217), surface=rgb(0x151C23), raised=rgb(0x1E272F), hairline=rgb(0x2A343D);
constexpr Color ink=rgb(0xF4F7FF), muted=rgb(0xA9B3BF), dim=rgb(0x76818D);
constexpr Color mint=rgb(0x56E69E), mintInk=rgb(0x062A18), coral=rgb(0xFF8A80), amber=rgb(0xFFD166);
constexpr Color white=rgb(0xFFFFFF);
constexpr float left=96, right=1824, width=1728, hintY=996;

Color mix(Color a, Color b, float t) {
    unsigned out=0xff000000U;
    for(unsigned shift:{0U,8U,16U}) {
        const float x=static_cast<float>((static_cast<unsigned>(a)>>shift)&255U), y=static_cast<float>((static_cast<unsigned>(b)>>shift)&255U);
        out|=static_cast<unsigned>(x+(y-x)*t+0.5f)<<shift;
    }
    return static_cast<Color>(out);
}

float baselineIn(Face face, float size, float top, float lineHeight) {
    return top+(lineHeight-(ascent(face,size)+descent(face,size)))/2+ascent(face,size);
}
float label(Canvas& c, Face face, float size, float x, float centerY, string_view text, Color color, float tracking=0, unsigned alpha=255) {
    return drawText(c,face,size,x,baselineIn(face,size,centerY-size*0.7f,size*1.4f),text,color,tracking,alpha);
}
float labelRight(Canvas& c, Face face, float size, float rightX, float centerY, string_view text, Color color, float tracking=0) {
    const float w=textWidth(face,size,text,tracking);
    label(c,face,size,rightX-w,centerY,text,color,tracking);
    return w;
}

struct Store { Color top, bottom; };
bool contains(string_view haystack, string_view needle) {
    for(std::size_t i=0;i+needle.size()<=haystack.size();++i) {
        bool same=true;
        for(std::size_t j=0;j<needle.size()&&same;++j) {
            char a=haystack[i+j];if(a>='a'&&a<='z')a=static_cast<char>(a-32);
            same=a==needle[j];
        }
        if(same)return true;
    }
    return false;
}
Store storeColors(string_view store) {
    if(contains(store,"STEAM"))return {rgb(0x2E4058),rgb(0x141C27)};
    if(contains(store,"XBOX")||contains(store,"MICROSOFT"))return {rgb(0x12703C),rgb(0x09301C)};
    if(contains(store,"EPIC"))return {rgb(0x36363F),rgb(0x16161B)};
    if(contains(store,"UBISOFT")||contains(store,"UPLAY"))return {rgb(0x3150B5),rgb(0x151E48)};
    if(contains(store,"BATTLE")||contains(store,"BLIZZARD"))return {rgb(0x2A6DA0),rgb(0x112B42)};
    if(contains(store,"GOG"))return {rgb(0x6A36C4),rgb(0x26124A)};
    if(contains(store,"EA")||contains(store,"ORIGIN"))return {rgb(0x8A3A34),rgb(0x341613)};
    return {rgb(0x2E3A44),rgb(0x151C23)};
}
Color storeAccent(string_view store) {
    if(contains(store,"STEAM"))return rgb(0x3B5675);
    if(contains(store,"XBOX")||contains(store,"MICROSOFT"))return rgb(0x107C41);
    if(contains(store,"EPIC"))return rgb(0x55555F);
    if(contains(store,"UBISOFT")||contains(store,"UPLAY"))return rgb(0x3A5BD9);
    if(contains(store,"BATTLE")||contains(store,"BLIZZARD"))return rgb(0x2E7CB8);
    if(contains(store,"GOG"))return rgb(0x7B3FE4);
    if(contains(store,"EA")||contains(store,"ORIGIN"))return rgb(0xB5483F);
    return rgb(0x45525E);
}

enum class Glyph { cross, circle, square, triangle, dpad, options, touch, check };
void glyph(Canvas& c, Glyph kind, float cx, float cy, Color color, float size=26) {
    const float s=size/24, x=cx-12*s, y=cy-12*s, t=2.6f*s;
    switch(kind) {
    case Glyph::cross: c.line(x+6.5f*s,y+6.5f*s,x+17.5f*s,y+17.5f*s,t,color);c.line(x+17.5f*s,y+6.5f*s,x+6.5f*s,y+17.5f*s,t,color);break;
    case Glyph::circle: c.ring(cx,cy,6.6f*s,t,color);break;
    case Glyph::square: c.roundRectStroke(x+6*s-t/2,y+6*s-t/2,12*s+t,12*s+t,1.4f*s+t/2,t,color);break;
    case Glyph::triangle: {
        const float p[]={x+12*s,y+5.2f*s,x+19*s,y+17.6f*s,x+5*s,y+17.6f*s};
        for(int i=0;i<3;++i)c.line(p[i*2],p[i*2+1],p[(i+1)%3*2],p[(i+1)%3*2+1],t,color);
        break;
    }
    case Glyph::dpad: {
        const float d=28.0f/24, ox=cx-12*d, oy=cy-12*d;
        const float p[]={9,3,15,3,15,9,21,9,21,15,15,15,15,21,9,21,9,15,3,15,3,9,9,9};
        for(int i=0;i<12;++i){const int j=(i+1)%12;c.line(ox+p[i*2]*d,oy+p[i*2+1]*d,ox+p[j*2]*d,oy+p[j*2+1]*d,2*d,color);}
        break;
    }
    case Glyph::options: for(float row:{7.0f,12.0f,17.0f})c.line(x+5*s,y+row*s,x+19*s,y+row*s,2.4f*s,color);break;
    case Glyph::touch: c.roundRectStroke(cx-15,cy-8,30,16,5,2.4f,color);break;
    case Glyph::check: c.line(x+5*s,y+12.5f*s,x+9.5f*s,y+17*s,3*s,color);c.line(x+9.5f*s,y+17*s,x+19*s,y+7.5f*s,3*s,color);break;
    }
}
float well(Canvas& c, Glyph kind, float x, float cy, Color fill=raised, Color color=ink) {
    c.disc(x+22,cy,22,fill);glyph(c,kind,x+22,cy,color);return 44;
}
float pillWidth(string_view text) {return textWidth(Face::black,18,text)+24;}
float pill(Canvas& c, string_view text, float x, float cy, Color fill=raised, Color color=ink) {
    const float w=pillWidth(text);
    c.roundRect(x,cy-20,w,40,10,fill);label(c,Face::black,18,x+12,cy,text,color);return w;
}
float optionsPill(Canvas& c, float x, float cy, Color fill=raised, Color color=ink) {
    c.roundRect(x,cy-20,48,40,10,fill);glyph(c,Glyph::options,x+24,cy,color,24);return 48;
}
float touchPill(Canvas& c, float x, float cy) {c.roundRect(x,cy-22,58,44,10,raised);glyph(c,Glyph::touch,x+29,cy,ink);return 58;}

enum class HintKind { glyph, pill, optionsMint, chord, hold };
struct Hint { HintKind kind; Glyph icon; const char* key; const char* text; };
constexpr float hintLabel=24;
float hintWidth(const Hint& h) {
    const float text=textWidth(Face::bold,hintLabel,h.text);
    switch(h.kind) {
    case HintKind::glyph: return 44+12+text;
    case HintKind::pill: return pillWidth(h.key)+12+text;
    case HintKind::optionsMint: return 48+12+text;
    case HintKind::chord: return pillWidth("L1")+8+textWidth(Face::black,20,"+")+8+pillWidth("R1")+12+text;
    case HintKind::hold: return textWidth(Face::bold,22,"Hold")+10+48+10+textWidth(Face::black,20,"+")+10+58+12+text;
    }
    return text;
}
void drawHint(Canvas& c, const Hint& h, float x) {
    switch(h.kind) {
    case HintKind::glyph: x+=well(c,h.icon,x,hintY)+12;break;
    case HintKind::pill: x+=pill(c,h.key,x,hintY)+12;break;
    case HintKind::optionsMint: x+=optionsPill(c,x,hintY,mint,mintInk)+12;break;
    case HintKind::chord:
        x+=pill(c,"L1",x,hintY)+8;x+=label(c,Face::black,20,x,hintY,"+",dim)+8;x+=pill(c,"R1",x,hintY)+12;break;
    case HintKind::hold:
        x+=label(c,Face::bold,22,x,hintY,"Hold",muted)+10;x+=optionsPill(c,x,hintY)+10;
        x+=label(c,Face::black,20,x,hintY,"+",dim)+10;x+=touchPill(c,x,hintY)+12;break;
    }
    label(c,Face::bold,hintLabel,x,hintY,h.text,ink);
}
void hints(Canvas& c, const Hint* leftHints, unsigned leftCount, const Hint* rightHints, unsigned rightCount) {
    float x=left;
    for(unsigned i=0;i<leftCount;++i){drawHint(c,leftHints[i],x);x+=hintWidth(leftHints[i])+40;}
    float total=0;
    for(unsigned i=0;i<rightCount;++i)total+=hintWidth(rightHints[i])+(i?40:0);
    x=right-total;
    for(unsigned i=0;i<rightCount;++i){drawHint(c,rightHints[i],x);x+=hintWidth(rightHints[i])+40;}
}
constexpr Hint closeApp{HintKind::glyph,Glyph::circle,nullptr,"Close app"};
constexpr Hint signOut{HintKind::chord,Glyph::cross,nullptr,"Sign out"};

void logo(Canvas& c, float x, float y) {
    c.ring(x+22,y+22,16,8,mint);
    const float play[]={x+18,y+14.5f,x+29,y+22,x+18,y+29.5f};
    c.polygon(play,3,mint);
    c.line(x+38,y+38,x+38,y+7,8,ink);c.line(x+38,y+7,x+56,y+38,8,ink);c.line(x+56,y+38,x+56,y+7,8,ink);
}
string_view profileName(StreamProfile p, char* buffer, std::size_t size) {
    const auto s=settingsFor(p);
    const char* prefix=p==StreamProfile::quality?"Quality ":p==StreamProfile::smooth?"Smooth ":p==StreamProfile::experimental?"Experimental ":p==StreamProfile::compatibility?"Compatibility ":"";
    if(s.height>=2160)std::snprintf(buffer,size,"%s4K%d %s",prefix,s.fps,s.hdr?"HDR":"SDR");
    else std::snprintf(buffer,size,"%s%dp%d",prefix,s.height,s.fps);
    return buffer;
}
string_view profileSpec(StreamProfile p, char* buffer, std::size_t size) {
    const auto s=settingsFor(p);
    std::snprintf(buffer,size,"%s \xC2\xB7 %s \xC2\xB7 %d Mb/s",s.codec==VideoCodec::hevc?(s.hdr?"HEVC 10-bit":"HEVC"):"H.264",s.hardware?"hardware":"software",s.bitrate_kbps/1000);
    return buffer;
}
void topBar(Canvas& c, const Model& m) {
    logo(c,left,66);
    const float base=baselineIn(Face::black,34,56,64);
    const float w=drawText(c,Face::black,34,176,base,"OpenNOW",ink,-0.01f);
    drawText(c,Face::bold,20,176+w+14,base,"PS5 \xC2\xB7 ALPHA",muted,0.08f);
    const bool signedIn=m.login.state==State::authenticated;
    const char* account=signedIn?(m.login.sessionSaved?"NVIDIA account saved":"Signed in \xC2\xB7 not saved"):"Not signed in";
    const float aw=textWidth(Face::bold,22,account)+22+12+12+22;
    float x=right-aw;
    c.roundRect(x,60,aw,56,28,surface);
    c.disc(x+22+6,88,6,signedIn?(m.login.sessionSaved?mint:amber):dim);
    label(c,Face::bold,22,x+22+12+12,88,account,ink);
    if(!signedIn)return;
    char name[48],spec[64],chip[128];
    profileName(m.profile,name,sizeof(name));profileSpec(m.profile,spec,sizeof(spec));
    std::snprintf(chip,sizeof(chip),"%s \xC2\xB7 %d Mb/s",name,settingsFor(m.profile).bitrate_kbps/1000);
    const float cw=10+52+14+textWidth(Face::mono,20,chip)+22;
    x-=20+cw;
    c.roundRect(x,60,cw,56,28,surface);
    c.roundRect(x+10,70,52,36,10,raised);
    const float lw=textWidth(Face::black,18,"L1");
    label(c,Face::black,18,x+10+26-lw/2,88,"L1",ink);
    label(c,Face::mono,20,x+10+52+14,88,chip,ink);
}
void eyebrow(Canvas& c, float x, float centerY, string_view text, Color color) {label(c,Face::extrabold,22,x,centerY,text,color,0.14f);}
float headline(Canvas& c, float size, float lineHeight, float top, string_view text, float maxWidth, unsigned maxLines) {
    const unsigned lines=drawWrapped(c,Face::black,size,left,top,lineHeight,text,maxWidth,maxLines,ink);
    return top+lines*lineHeight;
}
float body(Canvas& c, float top, string_view text, float maxWidth, unsigned maxLines=3, Color color=muted) {
    const unsigned lines=drawWrapped(c,Face::semibold,28,left,top,40,text,maxWidth,maxLines,color);
    return top+lines*40;
}
float button(Canvas& c, float x, float y, Glyph icon, string_view text, bool primary) {
    const float w=16+48+16+textWidth(Face::black,28,text)+36;
    c.roundRect(x,y,w,80,40,primary?mint:raised);
    c.disc(x+16+24,y+40,24,primary?mintInk:ground);
    glyph(c,icon,x+16+24,y+40,ink);
    label(c,Face::black,28,x+16+48+16,y+40,text,primary?mintInk:ink);
    return w;
}
float holdButton(Canvas& c, float x, float y, string_view text) {
    const float w=16+48+8+20+8+58+16+textWidth(Face::black,28,text)+36;
    c.roundRect(x,y,w,80,40,raised);
    float at=x+16;
    c.roundRect(at,y+20,48,40,10,ground);glyph(c,Glyph::options,at+24,y+40,ink,24);at+=48+8;
    at+=label(c,Face::black,20,at,y+40,"+",dim)+8;
    c.roundRect(at,y+18,58,44,10,ground);glyph(c,Glyph::touch,at+29,y+40,ink);at+=58+16;
    label(c,Face::black,28,at,y+40,text,ink);
    return w;
}
void detailCard(Canvas& c, float top, string_view first, string_view second, Color accent) {
    const float h=second.empty()?72:106;
    c.roundRect(left,top,980,h,18,surface);
    c.roundRect(left,top+8,4,h-16,2,accent);
    drawFitted(c,Face::mono,22,left+28,baselineIn(Face::mono,22,top+20,32),first,924,ink);
    if(!second.empty())drawFitted(c,Face::mono,20,left+28,baselineIn(Face::mono,20,top+56,30),second,924,muted);
}
void warningIcon(Canvas& c, float cx, float cy, Color color) {
    c.ring(cx,cy,15,2.8f,color);c.line(cx,cy-8,cx,cy+3,3.2f,color);c.disc(cx,cy+9,2,color);
}

float fitTitle(string_view title, float maxWidth, float largest, float smallest, float tracking=-0.025f) {
    for(int size=static_cast<int>(largest);size>static_cast<int>(smallest);size-=4)
        if(textWidth(Face::black,static_cast<float>(size),title,tracking)<=maxWidth)return static_cast<float>(size);
    return smallest;
}
void tile(Canvas& c, const Game& game, float x, float y, bool focused, float fade) {
    const auto colors=storeColors(game.store);
    const Color top=fade>0?mix(colors.top,ground,fade):focused?mix(colors.top,white,0.08f):colors.top;
    const Color bottom=fade>0?mix(colors.bottom,ground,fade):colors.bottom;
    if(focused)c.roundRectStroke(x-11,y-11,264+22,352+22,29,5,mint);
    c.verticalGradient(x,y,264,352,18,top,bottom);
    const unsigned alpha=fade>0?static_cast<unsigned>(255*(1-fade)):255;
    drawFitted(c,Face::black,16,x+24,baselineIn(Face::black,16,y+24,22),game.store,216,white,0.12f,alpha*82/100);
    const unsigned lines=drawWrapped(c,Face::black,30,0,0,34,game.title,216,4,ink,255,false);
    drawWrapped(c,Face::black,30,x+24,y+352-24-lines*34.0f,34,game.title,216,4,white,alpha);
}
void bottomFade(Canvas& c, float from) {
    for(int row=static_cast<int>(from);row<1080;++row) {
        const float y=static_cast<float>(row);
        const float t=(y-from)/(1080-from);
        const float a=t<0.45f?t/0.45f*0.92f:t<0.7f?0.92f+(t-0.45f)/0.25f*0.08f:1.0f;
        c.roundRect(0,y,1920,1,0,ground,static_cast<unsigned>(a*255));
    }
}
void storePill(Canvas& c, float x, float cy, string_view store) {
    const float w=std::min(textWidth(Face::black,18,store,0.08f)+28,360.0f);
    c.roundRect(x,cy-18,w,36,8,storeAccent(store));
    drawFitted(c,Face::black,18,x+14,baselineIn(Face::black,18,cy-18,36),store,w-28,white,0.08f);
}
float primaryPill(Canvas& c, float rightX, float y, string_view text) {
    const float w=14+48+18+textWidth(Face::black,28,text)+32;
    const float x=rightX-w;
    c.roundRect(x,y,w,72,36,mint);
    c.disc(x+14+24,y+36,24,mintInk);glyph(c,Glyph::cross,x+14+24,y+36,ink);
    label(c,Face::black,28,x+14+48+18,y+36,text,mintInk);
    return w;
}
void library(Canvas& c, const Model& m) {
    const auto& cv=m.cloud;
    const unsigned focus=std::min(m.focus,cv.count-1);
    const Game& game=cv.games[focus];
    const unsigned rows=(cv.count+gridColumns-1)/gridColumns, row=focus/gridColumns;
    storePill(c,left,210,game.store);
    char meta[96];
    std::snprintf(meta,sizeof(meta),"Entry %u of %u \xC2\xB7 Row %u of %u%s",focus+1,cv.count,row+1,rows,cv.hasNext?" \xC2\xB7 more with R1":"");
    label(c,Face::bold,22,left+std::min(textWidth(Face::black,18,game.store,0.08f)+28,360.0f)+14,210,meta,muted);
    const float pillW=primaryPill(c,right,274,"Select");
    const float room=width-pillW-48;
    const float size=fitTitle(game.title,room,104,64);
    drawFitted(c,Face::black,size,left,baselineIn(Face::black,size,242+(104-size)/2,size),game.title,room,ink,-0.025f);
    for(unsigned r=row;r<row+2&&r<rows;++r)
        for(unsigned col=0;col<gridColumns;++col) {
            const unsigned index=r*gridColumns+col;
            if(index>=cv.count)break;
            tile(c,cv.games[index],left+col*292.8f,386+(r-row)*384.0f,index==focus,r==row?0:0.45f);
        }
    bottomFade(c,780);
    Hint l[6]={{HintKind::glyph,Glyph::dpad,nullptr,"Browse"},{HintKind::glyph,Glyph::cross,nullptr,"Select"},{HintKind::glyph,Glyph::triangle,nullptr,"Search"},{HintKind::glyph,Glyph::square,nullptr,"Refresh"},{HintKind::pill,Glyph::cross,"L1","Profile"},{HintKind::pill,Glyph::cross,"R1","More games"}};
    const Hint r[]={closeApp,signOut};
    hints(c,l,cv.hasNext?6:5,r,2);
}
void catalogStatus(Canvas& c, const Model& m) {
    const bool loading=m.screen==Screen::catalogLoading;
    eyebrow(c,left,210,loading?"LIBRARY":"LIBRARY \xC2\xB7 EMPTY",loading?mint:muted);
    headline(c,104,104,242,loading?"Loading your library":"No games here",width,1);
    drawFitted(c,Face::mono,22,left,baselineIn(Face::mono,22,360,32),*m.cloud.message?m.cloud.message:(loading?"Loading NVIDIA catalog...":""),width,muted);
    for(unsigned col=0;col<gridColumns;++col)
        c.roundRect(left+col*292.8f,430,264,352,18,surface,loading?255:150);
    bottomFade(c,620);
    if(loading){const Hint r[]={closeApp,signOut};hints(c,nullptr,0,r,2);return;}
    const Hint l[]={{HintKind::glyph,Glyph::triangle,nullptr,"Search"},{HintKind::glyph,Glyph::square,nullptr,"Refresh"},{HintKind::pill,Glyph::cross,"L1","Profile"}};
    const Hint r[]={closeApp,signOut};
    hints(c,l,3,r,2);
}

void search(Canvas& c, const Model& m) {
    const auto& s=m.search;
    eyebrow(c,left,205,"SEARCH CATALOG",mint);
    c.roundRect(left,240,width,120,24,mint);
    c.roundRect(left,240,width,116,24,surface);
    char count[32];std::snprintf(count,sizeof(count),"%zu / %zu",std::strlen(s.text),sizeof(s.text)-1);
    const float countW=labelRight(c,Face::bold,22,right-36,300,count,dim);
    const float room=width-72-countW-48;
    string_view text(s.text);
    while(!text.empty()&&textWidth(Face::black,72,text,-0.01f)>room)text.remove_prefix(1);
    const float base=baselineIn(Face::black,72,300-50,100);
    float caret=left+36;
    if(text.empty())drawText(c,Face::black,72,left+36,base,"Type a game title",hairline,-0.01f);
    else caret+=drawText(c,Face::black,72,left+36,base,text,ink,-0.01f)+6;
    if(!text.empty())c.roundRect(caret,264,5,72,2.5f,mint);
    else c.roundRect(left+36,264,5,72,2.5f,mint);
    for(unsigned i=0;i<CatalogSearch::count;++i) {
        const unsigned col=CatalogSearch::column(i), row=CatalogSearch::row(i);
        const float x=col<10?left+col*116.0f:1308+(col-11)*116.0f, y=408+row*100.0f;
        const bool focused=i==s.selected;
        if(focused)c.roundRectStroke(x-9,y-9,122,106,25,4,mint);
        c.roundRect(x,y,104,88,16,focused?mint:surface);
        const Color color=focused?mintInk:ink;
        if(CatalogSearch::keys[i]==' ') {
            c.line(x+34,y+40,x+34,y+52,3,color);c.line(x+34,y+52,x+70,y+52,3,color);c.line(x+70,y+52,x+70,y+40,3,color);
        } else {
            const char key[2]{CatalogSearch::keys[i],0};
            const float kw=textWidth(Face::extrabold,36,key);
            label(c,Face::extrabold,36,x+52-kw/2,y+44,key,color);
        }
    }
    label(c,Face::semibold,24,left,850,"Searches the NVIDIA catalog by title. Results replace the library list.",dim);
    const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move"},{HintKind::glyph,Glyph::cross,nullptr,"Type"},{HintKind::glyph,Glyph::square,nullptr,"Backspace"},{HintKind::glyph,Glyph::triangle,nullptr,"Clear"}};
    const Hint r[]={{HintKind::optionsMint,Glyph::options,nullptr,"Search"},{HintKind::glyph,Glyph::circle,nullptr,"Cancel"}};
    hints(c,l,4,r,2);
}

void detail(Canvas& c, const Model& m) {
    const auto& cv=m.cloud;
    const unsigned focus=std::min(m.focus,cv.count-1);
    const Game& game=cv.games[focus];
    eyebrow(c,left,205,"FROM THE CATALOG",muted);
    const float room=1000;
    const float size=fitTitle(game.title,room,128,72);
    if(textWidth(Face::black,size,game.title,-0.03f)<=room)
        drawText(c,Face::black,size,left,baselineIn(Face::black,size,242+(124-size)/2,size),game.title,ink,-0.03f);
    else drawWrapped(c,Face::black,64,left,236,66,game.title,room,2,ink);
    label(c,Face::extrabold,20,left,423,"STORE",dim,0.14f);
    unsigned siblings[16];
    const unsigned count=storeSiblings(cv,focus,siblings,16);
    float x=left;
    for(unsigned i=0;i<count&&x<left+room;++i) {
        const Game& entry=cv.games[siblings[i]];
        const bool selected=siblings[i]==focus;
        const float w=std::min(22+14+12+textWidth(Face::extrabold,24,entry.store)+22,320.0f);
        if(selected){c.roundRect(x,452,w,56,28,raised);c.roundRectStroke(x,452,w,56,28,2,ink);}
        else c.roundRectStroke(x,452,w,56,28,2,hairline);
        c.roundRect(x+22,473,14,14,4,storeAccent(entry.store));
        drawFitted(c,Face::extrabold,24,x+22+14+12,baselineIn(Face::extrabold,24,452,56),entry.store,w-70,selected?ink:muted);
        x+=w+14;
    }
    StreamProfile profiles[static_cast<unsigned>(StreamProfile::count)];
    const unsigned available=availableProfiles(m.profileMask,profiles,static_cast<unsigned>(StreamProfile::count));
    unsigned selected=0;
    for(unsigned i=0;i<available;++i)if(profiles[i]==m.profile)selected=i;
    label(c,Face::extrabold,20,left,567,"STREAM PROFILE",dim,0.14f);
    char position[32];std::snprintf(position,sizeof(position),"%u of %u",selected+1,available);
    labelRight(c,Face::bold,20,left+room,567,position,dim);
    const unsigned visible=4;
    const unsigned first=available<=visible?0:std::min(selected>0?selected-1:0,available-visible);
    for(unsigned i=first;i<available&&i<first+visible;++i) {
        const float y=593+(i-first)*82.0f;
        const bool on=i==selected;
        if(on){c.roundRect(left,y,room,76,18,raised);c.roundRectStroke(left-4,y-4,room+8,84,22,4,mint);}
        if(on){c.disc(left+28+11,y+38,11,mint);c.disc(left+28+11,y+38,6,raised);c.disc(left+28+11,y+38,5,mint);}
        else c.ring(left+28+11,y+38,10,2,dim);
        char name[48],spec[64];
        label(c,Face::extrabold,30,left+70,y+38,profileName(profiles[i],name,sizeof(name)),ink);
        label(c,Face::mono,22,left+450,y+38,profileSpec(profiles[i],spec,sizeof(spec)),muted);
    }
    if(first>0){const float up[]={left+room-24,600,left+room-14,612,left+room-34,612};c.polygon(up,3,dim);}
    if(first+visible<available){const float down[]={left+room-24,925,left+room-14,913,left+room-34,913};c.polygon(down,3,dim);}
    const float px=1224, py=368, pw=600;
    c.roundRect(px,py,pw,550,28,surface);
    label(c,Face::extrabold,20,px+40,py+50,"THIS PS5",dim,0.14f);
    label(c,Face::bold,26,px+40,py+96,"Video output",muted);
    string_view output(m.output?m.output:"");
    if(output.rfind("OUTPUT ",0)==0)output.remove_prefix(7);
    drawWrapped(c,Face::mono,22,px+40,py+118,32,output,pw-80,2,ink);
    label(c,Face::bold,26,px+40,py+214,"Stream profile",muted);
    char name[48],spec[64];
    label(c,Face::mono,22,px+40,py+252,profileName(m.profile,name,sizeof(name)),ink);
    label(c,Face::mono,22,px+40,py+284,profileSpec(m.profile,spec,sizeof(spec)),muted);
    c.roundRect(px+40,py+322,pw-80,2,1,hairline);
    char play[96];std::snprintf(play,sizeof(play),"Play on %s",game.store);
    c.roundRect(px+40,py+354,pw-80,88,44,mint);
    const float tw=std::min(textWidth(Face::black,32,play),pw-80-110);
    const float startX=px+40+(pw-80-(48+18+tw))/2;
    c.disc(startX+24,py+398,24,mintInk);glyph(c,Glyph::cross,startX+24,py+398,ink);
    drawFitted(c,Face::black,32,startX+48+18,baselineIn(Face::black,32,py+354,88),play,pw-80-110,mintInk);
    drawWrapped(c,Face::semibold,22,px+40,py+464,30,"Your session starts in the cloud. You can wait in the queue or cancel at any time.",pw-80,2,muted);
    const Hint l[]={{HintKind::glyph,Glyph::cross,nullptr,"Play"},{HintKind::glyph,Glyph::dpad,nullptr,count>1?"Store / profile":"Profile"},{HintKind::pill,Glyph::cross,"L1","Next profile"}};
    const Hint r[]={{HintKind::glyph,Glyph::circle,nullptr,"Back"}};
    hints(c,l,3,r,1);
}

void step(Canvas& c, float y, int number, string_view text, int state) {
    const float cx=left+22, cy=y+28;
    if(state==2){c.disc(cx,cy,22,mint);glyph(c,Glyph::check,cx,cy,mintInk,22);}
    else if(state==1)c.ring(cx,cy,20.5f,3,mint);
    else c.ring(cx,cy,21,2,hairline);
    if(state!=2){char n[4];std::snprintf(n,sizeof(n),"%d",number);const float w=textWidth(Face::black,20,n);label(c,Face::black,20,cx-w/2,cy,n,state==1?mint:dim);}
    label(c,state==1?Face::black:Face::bold,30,left+64,cy,text,state==0?dim:ink);
}
void progress(Canvas& c, const Model& m) {
    const auto& cv=m.cloud;
    const bool connecting=m.screen==Screen::connecting;
    const Game* game=cv.selected<cv.count?&cv.games[cv.selected]:nullptr;
    char title[256];
    if(game)std::snprintf(title,sizeof(title),"%s \xC2\xB7 %s ON %s",connecting?"CONNECTING":"STARTING",game->title,game->store);
    else std::snprintf(title,sizeof(title),"%s",connecting?"CONNECTING":"STARTING");
    for(char* p=title;*p;++p)if(*p>='a'&&*p<='z')*p=static_cast<char>(*p-32);
    drawFitted(c,Face::extrabold,22,left,baselineIn(Face::extrabold,22,256,24),title,1060,mint,0.14f);
    const bool queued=cv.state==CloudState::queued;
    const char* head=connecting?"Connecting stream":queued&&cv.queuePosition>=0?"You\xE2\x80\x99re in line":queued&&cv.setupStep>=0?"Preparing your game":queued?"Almost ready":"Starting your session";
    const char* text=connecting?"Your cloud rig is ready. The picture appears as soon as the first video frame arrives.":
        queued&&cv.queuePosition>=0?"NVIDIA is finding a cloud rig for your session. This screen moves on by itself, so you can put the controller down.":
        queued?"NVIDIA is getting the server ready. This screen moves on by itself.":"Asking NVIDIA for a cloud session. This usually takes a few seconds.";
    const bool dial=queued&&(cv.queuePosition>=0||cv.setupStep>=0);
    const float headRoom=dial?1100:width;
    const float headSize=fitTitle(head,headRoom,112,72,0);
    float y=headline(c,headSize,headSize,296+(112-headSize),head,headRoom,1);
    y=body(c,y+20,text,880,2);
    const char* second=queued&&cv.queuePosition<0&&cv.setupStep>=0?"Server preparing game":queued?"Waiting in queue":"Waiting for a server";
    y+=40;
    const int phase=connecting?3:queued?2:1;
    step(c,y,1,"Session requested",phase>1?2:1);
    step(c,y+66,2,second,phase>2?2:phase==2?1:0);
    step(c,y+132,3,"Connecting stream",phase==3?1:0);
    step(c,y+198,4,"Playing",0);
    if(dial) {
        const float cx=1564, cy=540;
        c.ring(cx,cy,258,3,hairline);
        const bool queue=cv.queuePosition>=0;
        const char* heading=queue?"QUEUE POSITION":"SETUP STEP";
        const float hw=textWidth(Face::extrabold,22,heading,0.14f);
        label(c,Face::extrabold,22,cx-hw/2,cy-140,heading,muted,0.14f);
        char number[16];std::snprintf(number,sizeof(number),"%d",queue?cv.queuePosition:cv.setupStep);
        const float size=std::strlen(number)>3?150:240;
        const float nw=textWidth(Face::black,size,number,-0.04f);
        drawText(c,Face::black,size,cx-nw/2,baselineIn(Face::black,size,cy-size/2,size),number,mint,-0.04f);
        const float uw=textWidth(Face::bold,24,"updates live");
        label(c,Face::bold,24,cx-uw/2,cy+138,"updates live",muted);
    }
    drawFitted(c,Face::mono,22,left,baselineIn(Face::mono,22,hintY-16,32),cv.message,980,dim);
    const Hint r[]={{HintKind::hold,Glyph::options,nullptr,m.streaming?"End session":"Cancel session"}};
    hints(c,nullptr,0,r,1);
}

void failure(Canvas& c, const Model& m) {
    const auto& cv=m.cloud;
    const Screen s=m.screen;
    const char* tag=s==Screen::streamEnded?"STREAM ENDED":s==Screen::cleanupFailed?"SESSION STILL RUNNING":"CATALOG UNAVAILABLE";
    const char* head=s==Screen::streamEnded?"The connection to your game dropped":s==Screen::cleanupFailed?"We couldn\xE2\x80\x99t end the cloud session":"Your library didn\xE2\x80\x99t load";
    const char* text=s==Screen::streamEnded?"OpenNOW stopped the cloud session, so nothing keeps running on your account. You can try again right away.":
        s==Screen::cleanupFailed?"It may still be running on your account. End it before starting another game.":
        "NVIDIA didn\xE2\x80\x99t return the catalog. Check the connection and try again.";
    warningIcon(c,left+18,314,coral);
    eyebrow(c,left+52,314,tag,coral);
    float y=headline(c,104,108,354,head,1300,2);
    y=body(c,y+24,text,980,2);
    char second[256]{};
    const Game* game=cv.selected<cv.count?&cv.games[cv.selected]:nullptr;
    if(s!=Screen::catalogError&&game) {
        char name[48];
        std::snprintf(second,sizeof(second),"%s \xC2\xB7 %s \xC2\xB7 %s",game->title,game->store,profileName(m.profile,name,sizeof(name)).data());
    }
    detailCard(c,y+28,*cv.message?cv.message:"No details reported",second,coral);
    float x=left;
    if(s==Screen::streamEnded){x+=button(c,x,944,Glyph::cross,"Try again",true)+20;button(c,x,944,Glyph::square,"Back to library",false);}
    else if(s==Screen::catalogError){x+=button(c,x,944,Glyph::cross,"Try again",true)+20;button(c,x,944,Glyph::triangle,"Search",false);}
    else holdButton(c,x,944,"End session");
    const float closeW=16+48+16+textWidth(Face::black,28,"Close app")+36;
    button(c,right-closeW,944,Glyph::circle,"Close app",false);
}

void signIn(Canvas& c, const Model& m) {
    const auto& v=m.login;
    const bool waiting=v.state==State::waiting, requesting=v.state==State::requesting;
    const bool failed=v.state==State::failed||v.state==State::denied||v.state==State::expired;
    if(waiting) {
        eyebrow(c,left,268,"SIGN IN WITH NVIDIA",mint);
        float y=headline(c,96,100,306,"Scan the code with your phone",980,2);
        string_view url(v.url);
        if(url.rfind("https://",0)==0)url.remove_prefix(8);
        char text[640];
        std::snprintf(text,sizeof(text),"Or visit %.*s on any device and enter this code. Your login stays saved on this PS5.",static_cast<int>(url.size()),url.data());
        y=body(c,y+28,text,820,3);
        string_view code(v.code);
        unsigned cells=0;for(char ch:code)cells+=ch!='-';
        const float cell=cells>10?64:84, gap=12;
        float x=left;y+=40;
        for(std::size_t i=0;i<code.size();++i) {
            if(code[i]=='-'){c.roundRect(x+2,y+51,28,6,3,hairline);x+=32+gap;continue;}
            c.roundRect(x,y,cell,108,14,surface);
            const char ch[2]{code[i],0};
            const float w=textWidth(Face::mono,60,ch);
            label(c,Face::mono,60,x+cell/2-w/2,y+54,ch,ink);
            x+=cell+gap;
        }
        char expiry[64];std::snprintf(expiry,sizeof(expiry),"\xC2\xB7 code expires in %u:%02u",v.expiresIn/60,v.expiresIn%60);
        c.disc(left+6,y+142,6,mint);
        const float w=label(c,Face::bold,24,left+26,y+142,"Waiting for approval",ink);
        label(c,Face::semibold,24,left+26+w+14,y+142,expiry,dim);
        static unsigned char temp[qrcodegen_BUFFER_LEN_MAX],qr[qrcodegen_BUFFER_LEN_MAX];
        if(qrcodegen_encodeText(v.qrUrl,temp,qr,qrcodegen_Ecc_MEDIUM,1,20,qrcodegen_Mask_AUTO,true)) {
            const int size=qrcodegen_getSize(qr);
            const int module=std::max(2,290/size);
            const float qrSize=static_cast<float>(module*size);
            const float pw=qrSize+80, ph=qrSize+80+24+30;
            const float px=right-pw, py=540-ph/2;
            c.roundRect(px,py,pw,ph,28,white);
            for(int qy=0;qy<size;++qy)for(int qx=0;qx<size;++qx)
                if(qrcodegen_getModule(qr,qx,qy))c.rectangle(static_cast<unsigned>(px+40)+qx*module,static_cast<unsigned>(py+40)+qy*module,module,module,ground);
            const float lw=textWidth(Face::extrabold,22,"Scan to sign in");
            label(c,Face::extrabold,22,px+pw/2-lw/2,py+40+qrSize+24+15,"Scan to sign in",ground);
        }
    } else {
        eyebrow(c,left,268,failed?(v.state==State::expired?"CODE EXPIRED":v.state==State::denied?"SIGN-IN DECLINED":"SIGN-IN PROBLEM"):"GEFORCE NOW ON PS5",failed?coral:mint);
        const char* head=requesting?"Contacting NVIDIA":failed?"Sign-in didn\xE2\x80\x99t finish":"Play your cloud library";
        float y=headline(c,96,100,306,head,1200,2);
        if(failed)detailCard(c,y+32,v.message,"",coral);
        else y=body(c,y+28,requesting?v.message:"Sign in with your NVIDIA account using your phone. OpenNOW keeps your login saved on this PS5.",900,3);
        if(!requesting)button(c,left,failed?y+150:y+48,Glyph::cross,failed?"Try again":"Sign in",true);
    }
    label(c,Face::semibold,22,left,hintY,"Unofficial client \xC2\xB7 not affiliated with NVIDIA or Sony",dim);
    const Hint r[]={closeApp};
    hints(c,nullptr,0,r,1);
}
}

Screen screenFor(const Inputs& in) noexcept {
    if(in.streaming)return Screen::connecting;
    if(in.login.state!=State::authenticated)return Screen::signIn;
    const auto state=in.cloud.state;
    if(in.sessionOwned||state==CloudState::starting||state==CloudState::queued||state==CloudState::ready) {
        if(in.sessionOwned&&state==CloudState::failed)return Screen::cleanupFailed;
        return state==CloudState::ready?Screen::connecting:Screen::launching;
    }
    if(in.searchOpen&&(state==CloudState::catalog||state==CloudState::failed))return Screen::search;
    if(state==CloudState::failed)return Screen::catalogError;
    if(state!=CloudState::catalog)return Screen::catalogLoading;
    if(in.streamFailed)return Screen::streamEnded;
    if(!in.cloud.count)return Screen::catalogEmpty;
    return in.detailOpen?Screen::detail:Screen::library;
}

unsigned availableProfiles(unsigned mask, StreamProfile* out, unsigned capacity) noexcept {
    unsigned count=0;
    StreamProfile p=StreamProfile::native_hdr120;
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i,p=nextProfile(p))
        if((mask&profileBit(p))&&count<capacity)out[count++]=p;
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
    c.clear(ground);
    topBar(c,m);
    switch(m.screen) {
    case Screen::signIn: signIn(c,m);break;
    case Screen::catalogLoading: case Screen::catalogEmpty: catalogStatus(c,m);break;
    case Screen::catalogError: case Screen::cleanupFailed: case Screen::streamEnded: failure(c,m);break;
    case Screen::library: library(c,m);break;
    case Screen::search: search(c,m);break;
    case Screen::detail: detail(c,m);break;
    case Screen::launching: case Screen::connecting: progress(c,m);break;
    }
}
}
