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
#include <utility>

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

enum class Glyph { cross, circle, square, triangle, dpad, options, touch, check, stick };
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
    case Glyph::stick: c.ring(cx,cy,8.5f*s,t,color);c.disc(cx,cy,3*s,color);break;
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

enum class HintKind { glyph, pill, optionsMint, chord, hold, combo };
struct Hint { HintKind kind; Glyph icon; const char* key; const char* text; };
constexpr float hintLabel=24;
float hintWidth(const Hint& h) {
    const float text=textWidth(Face::bold,hintLabel,h.text);
    switch(h.kind) {
    case HintKind::glyph: return 44+12+text;
    case HintKind::pill: return pillWidth(h.key)+12+text;
    case HintKind::optionsMint: return 48+12+text;
    case HintKind::chord: return pillWidth("L1")+8+pillWidth("R1")+12+text;
    case HintKind::hold: return textWidth(Face::bold,22,"Hold")+10+48+10+textWidth(Face::black,20,"+")+10+58+12+text;
    case HintKind::combo: return 48+10+textWidth(Face::black,20,"+")+10+44+12+text;
    }
    return text;
}
void drawHint(Canvas& c, const Hint& h, float x, float cy=hintY) {
    switch(h.kind) {
    case HintKind::glyph: x+=well(c,h.icon,x,cy)+12;break;
    case HintKind::pill: x+=pill(c,h.key,x,cy)+12;break;
    case HintKind::optionsMint: x+=optionsPill(c,x,cy,mint,mintInk)+12;break;
    case HintKind::chord:
        x+=pill(c,"L1",x,cy)+8;x+=pill(c,"R1",x,cy)+12;break;
    case HintKind::hold:
        x+=label(c,Face::bold,22,x,cy,"Hold",muted)+10;x+=optionsPill(c,x,cy)+10;
        x+=label(c,Face::black,20,x,cy,"+",dim)+10;x+=touchPill(c,x,cy)+12;break;
    case HintKind::combo:
        x+=optionsPill(c,x,cy)+10;x+=label(c,Face::black,20,x,cy,"+",dim)+10;x+=well(c,h.icon,x,cy)+12;break;
    }
    label(c,Face::bold,hintLabel,x,cy,h.text,ink);
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
constexpr Hint sections{HintKind::chord,Glyph::cross,nullptr,"Sections"};
constexpr Hint back{HintKind::glyph,Glyph::circle,nullptr,"Back"};

void logo(Canvas& c, float x, float y) {
    c.ring(x+22,y+22,16,8,mint);
    const float play[]={x+18,y+14.5f,x+29,y+22,x+18,y+29.5f};
    c.polygon(play,3,mint);
    c.line(x+38,y+38,x+38,y+7,8,ink);c.line(x+38,y+7,x+56,y+38,8,ink);c.line(x+56,y+38,x+56,y+7,8,ink);
}
string_view profileName(StreamProfile p, char* buffer, std::size_t size) {
    const auto s=settingsFor(p);
    const char* prefix=p==StreamProfile::quality?"Quality ":p==StreamProfile::smooth?"Smooth ":p==StreamProfile::experimental?"Experimental ":p==StreamProfile::compatibility?"Compatibility ":"";
    if(s.height>=2160)std::snprintf(buffer,size,"%s4K%d %s",prefix,s.fps,s.hdr()?"HDR":"SDR");
    else std::snprintf(buffer,size,"%s%dp%d",prefix,s.height,s.fps);
    return buffer;
}
string_view settingsName(const StreamSettings& s, char* buffer, std::size_t size) {
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i)
        if(presetFor(static_cast<StreamProfile>(i),s)==s)return profileName(static_cast<StreamProfile>(i),buffer,size);
    if(s.height>=2160)std::snprintf(buffer,size,"Custom 4K%d %s",s.fps,s.hdr()?"HDR":"SDR");
    else std::snprintf(buffer,size,"Custom %dp%d%s",s.height,s.fps,s.hdr()?" HDR":"");
    return buffer;
}
const char* codecLabel(const StreamSettings& s) {return s.hdr()?"HEVC HDR":s.tenBit()?"HEVC 10-bit":"H.264";}
string_view settingsSpec(const StreamSettings& s, char* buffer, std::size_t size) {
    std::snprintf(buffer,size,"%d\xC3\x97%d \xC2\xB7 %s \xC2\xB7 %s \xC2\xB7 %d Mb/s",s.width,s.height,codecLabel(s),s.hardware()?"hardware":"software",s.bitrate_kbps/1000);
    return buffer;
}
void topBar(Canvas& c, const Model& m, bool tabs) {
    logo(c,left,66);
    const float base=baselineIn(Face::black,34,56,64);
    const float w=drawText(c,Face::black,34,176,base,"OpenNOW",ink,-0.01f);
    drawText(c,Face::bold,20,176+w+14,base,"PS5 \xC2\xB7 ALPHA",muted,0.08f);
    const bool signedIn=m.login.state==State::authenticated;
    const char* account=signedIn?(m.login.sessionSaved?"NVIDIA account saved":"Signed in \xC2\xB7 not saved"):"Not signed in";
    const float aw=textWidth(Face::bold,22,account)+22+12+12+22;
    const float x=right-aw;
    c.roundRect(x,60,aw,56,28,surface);
    c.disc(x+22+6,88,6,signedIn?(m.login.sessionSaved?mint:amber):dim);
    label(c,Face::bold,22,x+22+12+12,88,account,ink);
    if(!tabs)return;
    const char* names[]={"Library","Browse","Settings"};
    float total=pillWidth("L1")+pillWidth("R1")+40*4;
    for(unsigned i=0;i<3;++i)total+=textWidth(i==static_cast<unsigned>(m.section)?Face::black:Face::bold,28,names[i]);
    float at=960-total/2;
    at+=pill(c,"L1",at,88)+40;
    for(unsigned i=0;i<3;++i) {
        const bool active=i==static_cast<unsigned>(m.section);
        const float tw=textWidth(active?Face::black:Face::bold,28,names[i]);
        label(c,active?Face::black:Face::bold,28,at,88,names[i],active?ink:muted);
        if(active)c.roundRect(at,112,tw,4,2,mint);
        at+=tw+40;
    }
    pill(c,"R1",at,88);
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
constexpr float tileW=art::tileWidth, tileH=art::tileHeight, tileStep=296, gridTop=436, peekTop=834;
void fallbackTile(Canvas& c, const Game& game, float x, float y, float fade, bool missingArt) {
    const auto colors=storeColors(game.store);
    c.verticalGradient(x,y,tileW,tileH,18,mix(colors.top,ground,fade),mix(colors.bottom,ground,fade));
    const unsigned alpha=static_cast<unsigned>(255*(1-fade));
    drawFitted(c,Face::black,14,x+22,baselineIn(Face::black,14,y+22,20),game.store,missingArt?170:204,white,0.12f,alpha*82/100);
    if(missingArt) {
        const float ix=x+tileW-22-22, iy=y+22;
        c.roundRectStroke(ix+3,iy+4,17,14,2.5f,2,white,alpha*55/100);
        c.line(ix+2,iy+21,ix+20,iy+3,2,white,alpha*55/100);
    }
    const unsigned lines=drawWrapped(c,Face::black,28,0,0,32,game.title,204,5,ink,255,false);
    drawWrapped(c,Face::black,28,x+22,y+tileH-22-lines*32.0f,32,game.title,204,5,white,alpha);
}
void loadingTile(Canvas& c, const Game& game, float x, float y, float fade) {
    c.verticalGradient(x,y,tileW,tileH,18,mix(raised,ground,fade),mix(surface,ground,fade));
    const unsigned alpha=static_cast<unsigned>(255*(1-fade));
    c.disc(x+27,y+27,5,mint,alpha);c.disc(x+45,y+27,5,hairline,alpha);c.disc(x+63,y+27,5,hairline,alpha);
    char tag[96];std::snprintf(tag,sizeof(tag),"%s \xC2\xB7 LOADING ART",game.store);
    const unsigned lines=drawWrapped(c,Face::black,28,0,0,32,game.title,204,4,ink,255,false);
    const float titleTop=y+tileH-22-lines*32.0f;
    drawFitted(c,Face::extrabold,14,x+22,baselineIn(Face::extrabold,14,titleTop-28,20),tag,204,dim,0.12f,alpha);
    drawWrapped(c,Face::black,28,x+22,titleTop,32,game.title,204,4,muted,alpha);
}
void coverTile(Canvas& c, const Model& m, const Game& game, float x, float y, bool focused, float fade, bool ownedBadge) {
    if(focused)c.roundRectStroke(x-11,y-11,tileW+22,tileH+22,29,5,mint);
    art::State state=art::State::missing;
    if(m.art&&*game.art)state=m.art->want(game.art,art::Kind::tile,m.frame);
    const unsigned alpha=static_cast<unsigned>(255*(1-fade));
    if(state==art::State::ready&&m.art->draw(c,game.art,art::Kind::tile,x,y,18,alpha)) {
        const float bw=std::min(textWidth(Face::black,14,game.store,0.1f)+20,tileW-28);
        c.roundRect(x+14,y+tileH-14-30,bw,30,8,ground,alpha*82/100);
        drawFitted(c,Face::black,14,x+24,baselineIn(Face::black,14,y+tileH-44,30),game.store,bw-20,ink,0.1f,alpha);
        if(ownedBadge&&game.owned) {
            const float ow=6+18+6+textWidth(Face::black,13,"IN LIBRARY",0.1f)+10;
            c.roundRect(x+tileW-14-ow,y+14,ow,30,15,mint,alpha);
            glyph(c,Glyph::check,x+tileW-14-ow+15,y+29,mintInk,18);
            label(c,Face::black,13,x+tileW-14-ow+30,y+29,"IN LIBRARY",mintInk,0.1f,alpha);
        }
        return;
    }
    if(state==art::State::loading)loadingTile(c,game,x,y,fade);
    else fallbackTile(c,game,x,y,fade,*game.art!=0);
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
void heroBackdrop(Canvas& c, const Model& m, const Game& game) {
    if(!m.art||!*game.hero||m.art->want(game.hero,art::Kind::hero,m.frame)!=art::State::ready)return;
    static std::uint8_t columns[1280], rows[720];
    static bool ready=false;
    if(!ready) {
        for(int i=0;i<1280;++i) {
            const float t=static_cast<float>(i)/1279.0f;
            const float scrim=t<0.45f?1.0f-(t/0.45f)*0.45f:0.55f-((t-0.45f)/0.55f)*0.40f;
            columns[i]=static_cast<std::uint8_t>(std::clamp((1.0f-scrim)*0.55f,0.0f,1.0f)*255);
        }
        for(int i=0;i<720;++i) {
            const float t=static_cast<float>(i)/719.0f;
            const float scrim=t<0.30f?0.35f*(1-t/0.30f):t<0.62f?0.6f*(t-0.30f)/0.32f:t<0.88f?0.6f+0.4f*(t-0.62f)/0.26f:1.0f;
            rows[i]=static_cast<std::uint8_t>(std::clamp(1.0f-scrim,0.0f,1.0f)*255);
        }
        ready=true;
    }
    m.art->drawHero(c,game.hero,640,0,1280,720,columns,rows);
}
const char* sectionEyebrow(const Model& m) {
    if(m.section==Section::library)return "YOUR LIBRARY";
    return *m.searchText?"SEARCH RESULTS":"BROWSE CATALOG";
}
void sectionHints(Canvas& c, const Hint* l, unsigned n) {
    const Hint r[]={sections,closeApp};
    hints(c,l,n,r,2);
}
void grid(Canvas& c, const Model& m) {
    const auto& v=catalogFor(m.section,m.session,m.library);
    const unsigned focus=std::min(m.focus,v.count-1);
    const Game& game=v.games[focus];
    const bool browse=m.section==Section::browse;
    heroBackdrop(c,m,game);
    const float eyebrowW=label(c,Face::extrabold,22,left,230,sectionEyebrow(m),mint,0.14f);
    if(browse) {
        char text[160];
        if(*m.searchText)std::snprintf(text,sizeof(text),"\xE2\x80\x9C%.100s\xE2\x80\x9D \xC2\xB7 new search",m.searchText);
        else std::snprintf(text,sizeof(text),"Search all games");
        const float pw=8+28+10+std::min(textWidth(Face::bold,20,text),520.0f)+16;
        const float px=left+eyebrowW+20;
        c.roundRect(px,210,pw,40,20,surface,220);
        c.disc(px+8+14,230,14,raised);glyph(c,Glyph::triangle,px+8+14,230,ink,18);
        drawFitted(c,Face::bold,20,px+8+28+10,baselineIn(Face::bold,20,210,40),text,520,muted);
    }
    const float pillW=primaryPill(c,right,324,"Select");
    const float room=width-pillW-48;
    const float size=fitTitle(game.title,room,88,60);
    drawFitted(c,Face::black,size,left,baselineIn(Face::black,size,262+(88-size)/2,size),game.title,room,ink,-0.025f);
    storePill(c,left,378,game.store);
    char meta[160];
    const char* owned=browse?(game.owned?"In your library":"Not in your library"):"In your library";
    if(v.page||v.hasNext)std::snprintf(meta,sizeof(meta),"%s \xC2\xB7 Game %u of %u \xC2\xB7 Page %u",owned,focus+1,v.count,v.page+1);
    else std::snprintf(meta,sizeof(meta),"%s \xC2\xB7 Game %u of %u",owned,focus+1,v.count);
    label(c,Face::bold,22,left+std::min(textWidth(Face::black,18,game.store,0.08f)+28,360.0f)+14,378,meta,muted);
    const unsigned rows=(v.count+gridColumns-1)/gridColumns, row=focus/gridColumns;
    for(unsigned r=row;r<row+2&&r<rows+1;++r) {
        const float y=r==row?gridTop:peekTop;
        const float fade=r==row?0:0.45f;
        for(unsigned col=0;col<gridColumns;++col) {
            const unsigned index=r*gridColumns+col;
            const float x=left+col*tileStep;
            if(index<v.count){coverTile(c,m,v.games[index],x,y,index==focus,fade,browse);continue;}
            if(index==v.count&&(v.hasNext||v.state==CloudState::loading)) {
                c.roundRectStroke(x,y,tileW,tileH,18,2,hairline);
                const bool loading=v.state==CloudState::loading;
                c.disc(x+tileW/2-22,y+tileH/2-40,6,mint);c.disc(x+tileW/2,y+tileH/2-40,6,hairline);c.disc(x+tileW/2+22,y+tileH/2-40,6,hairline);
                const char* head=loading?"Loading more":"More games";
                label(c,Face::extrabold,22,x+tileW/2-textWidth(Face::extrabold,22,head)/2,y+tileH/2,head,muted);
                drawWrapped(c,Face::semibold,18,x+34,y+tileH/2+22,24,"Keep moving past the last game for the next page",180,3,dim);
            }
            break;
        }
    }
    bottomFade(c,800);
    if(v.state==CloudState::failed) {
        char text[256];std::snprintf(text,sizeof(text),"Couldn\xE2\x80\x99t load that page \xC2\xB7 %s",v.message);
        const float bw=std::min(textWidth(Face::bold,22,text)+48+44+12,width);
        c.roundRect(left,888,bw,52,26,surface);
        c.roundRect(left,888,4,52,2,coral);
        well(c,Glyph::cross,left+14,914);
        drawFitted(c,Face::bold,22,left+14+44+12,baselineIn(Face::bold,22,888,52),text,bw-80,coral);
    }
    const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move"},{HintKind::glyph,Glyph::cross,nullptr,v.state==CloudState::failed?"Try again":"Select"},
        {HintKind::glyph,Glyph::triangle,nullptr,browse?"Search":"Search catalog"},{HintKind::glyph,Glyph::square,nullptr,"Refresh"}};
    sectionHints(c,l,4);
}
void catalogStatus(Canvas& c, const Model& m) {
    const auto& v=catalogFor(m.section,m.session,m.library);
    const bool library=m.section==Section::library;
    if(m.screen==Screen::catalogLoading) {
        eyebrow(c,left,230,sectionEyebrow(m),mint);
        headline(c,88,92,262,library?"Loading your library":"Loading the catalog",width,1);
        for(unsigned col=0;col<gridColumns;++col)c.roundRect(left+col*tileStep,gridTop,tileW,tileH,18,surface);
        bottomFade(c,700);
        sectionHints(c,nullptr,0);
        return;
    }
    if(m.screen==Screen::catalogError) {
        warningIcon(c,left+18,254,coral);
        eyebrow(c,left+52,254,library?"LIBRARY UNAVAILABLE":"CATALOG UNAVAILABLE",coral);
        float y=headline(c,104,108,294,library?"Your library didn\xE2\x80\x99t load":"The catalog didn\xE2\x80\x99t load",1300,1);
        y=body(c,y+20,library?"The NVIDIA library request failed. Your games are still on your account.":"NVIDIA didn\xE2\x80\x99t return the catalog. Check the connection and try again.",1000,2);
        detailCard(c,y+28,*v.message?v.message:"No details reported","",coral);
        button(c,left,y+28+72+48,Glyph::cross,"Try again",true);
        sectionHints(c,nullptr,0);
        return;
    }
    const bool more=v.hasNext;
    eyebrow(c,left,254,more?(library?"YOUR LIBRARY \xC2\xB7 STILL LOOKING":"BROWSE \xC2\xB7 STILL LOOKING"):library?"YOUR LIBRARY \xC2\xB7 EMPTY":"BROWSE \xC2\xB7 NO RESULTS",muted);
    float y=headline(c,104,108,294,more?"No owned games found yet":library?"Nothing in your library yet":"No games found",1400,1);
    char text[256];
    if(more)std::snprintf(text,sizeof(text),"OpenNOW looked through catalog page %u without finding a game you own. More pages remain, so your library may continue further on.",v.page+1);
    else if(library)std::snprintf(text,sizeof(text),"Games you add to your GeForce NOW library on your NVIDIA account show up here. Browse the full catalog to find something to play.");
    else if(*m.searchText)std::snprintf(text,sizeof(text),"Nothing matched \xE2\x80\x9C%.100s\xE2\x80\x9D. Try a shorter title.",m.searchText);
    else std::snprintf(text,sizeof(text),"%s",v.message);
    y=body(c,y+24,text,1000,2);
    float x=left;
    if(more)x+=button(c,x,y+40,Glyph::cross,"Keep looking",true)+20;
    else if(library)x+=button(c,x,y+40,Glyph::cross,"Browse catalog",true)+20;
    else x+=button(c,x,y+40,Glyph::triangle,"Search",true)+20;
    button(c,x,y+40,Glyph::square,"Refresh",false);
    sectionHints(c,nullptr,0);
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
    label(c,Face::semibold,24,left,850,"Searches the full NVIDIA catalog by title. Results open in Browse.",dim);
    const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move"},{HintKind::glyph,Glyph::cross,nullptr,"Type"},{HintKind::glyph,Glyph::square,nullptr,"Backspace"},{HintKind::glyph,Glyph::triangle,nullptr,"Clear"}};
    const Hint r[]={{HintKind::optionsMint,Glyph::options,nullptr,"Search"},{HintKind::glyph,Glyph::circle,nullptr,"Cancel"}};
    hints(c,l,4,r,2);
}

void detail(Canvas& c, const Model& m) {
    const auto& cv=catalogFor(m.section,m.session,m.library);
    const unsigned focus=std::min(m.focus,cv.count-1);
    const Game& game=cv.games[focus];
    eyebrow(c,left,205,m.section==Section::library?"FROM YOUR LIBRARY":"FROM THE CATALOG",muted);
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
    const unsigned available=m.choiceCount, selected=std::min(m.choice,available?available-1:0);
    label(c,Face::extrabold,20,left,567,"STREAM SETTINGS",dim,0.14f);
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
        char name[48],spec[96];
        drawFitted(c,Face::extrabold,30,left+70,baselineIn(Face::extrabold,30,y+20,36),i==0?"Your default":settingsName(m.choices[i],name,sizeof(name)),360,ink);
        drawFitted(c,Face::mono,20,left+450,baselineIn(Face::mono,20,y+24,28),settingsSpec(m.choices[i],spec,sizeof(spec)),room-480,muted);
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
    label(c,Face::bold,26,px+40,py+214,"Requested stream",muted);
    char name[48],spec[96];
    const StreamSettings& chosen=available?m.choices[selected]:m.defaults;
    label(c,Face::mono,22,px+40,py+252,settingsName(chosen,name,sizeof(name)),ink);
    drawFitted(c,Face::mono,20,px+40,baselineIn(Face::mono,20,py+270,28),settingsSpec(chosen,spec,sizeof(spec)),pw-80,muted);
    c.roundRect(px+40,py+322,pw-80,2,1,hairline);
    char play[96];std::snprintf(play,sizeof(play),"Play on %s",game.store);
    c.roundRect(px+40,py+354,pw-80,88,44,mint);
    const float tw=std::min(textWidth(Face::black,32,play),pw-80-110);
    const float startX=px+40+(pw-80-(48+18+tw))/2;
    c.disc(startX+24,py+398,24,mintInk);glyph(c,Glyph::cross,startX+24,py+398,ink);
    drawFitted(c,Face::black,32,startX+48+18,baselineIn(Face::black,32,py+354,88),play,pw-80-110,mintInk);
    drawWrapped(c,Face::semibold,22,px+40,py+464,30,"Your session starts in the cloud. You can wait in the queue or cancel at any time.",pw-80,2,muted);
    const Hint l[]={{HintKind::glyph,Glyph::cross,nullptr,"Play"},{HintKind::glyph,Glyph::dpad,nullptr,count>1?"Store / settings":"Settings"},{HintKind::pill,Glyph::cross,"L1","Next"}};
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
    const auto& cv=m.session;
    const bool connecting=m.screen==Screen::connecting;
    const Game* game=*cv.current.title?&cv.current:nullptr;
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
    drawFitted(c,Face::mono,22,left,baselineIn(Face::mono,22,m.streaming?hintY-86:hintY-16,32),cv.message,980,dim);
    const Hint l[]={{HintKind::glyph,Glyph::touch,nullptr,"Mouse"},{HintKind::combo,Glyph::triangle,nullptr,"Keyboard"},{HintKind::combo,Glyph::square,nullptr,"Back"}};
    const Hint r[]={{HintKind::hold,Glyph::options,nullptr,m.streaming?"End session":"Cancel session"}};
    hints(c,l,m.streaming?3:0,r,1);
}

void keyGrid(Canvas& c, const StreamKeyboard& k, bool ready, float x0, float y0, float span, float keyH, float rowPitch, Color keyColor) {
    const float pitch=span/StreamKeyboard::units, big=keyH*0.43f, small=keyH*0.31f;
    for(unsigned i=0;i<StreamKeyboard::count;++i) {
        const auto& key=StreamKeyboard::keys[i];
        const float x=x0+StreamKeyboard::column(i)*pitch, y=y0+StreamKeyboard::row(i)*rowPitch, w=key.width*pitch-12;
        const bool focused=i==k.selected, latched=k.latched(i);
        if(focused)c.roundRectStroke(x-9,y-9,w+18,keyH+18,25,4,mint);
        c.roundRect(x,y,w,keyH,16,focused?mint:latched?mix(keyColor,mint,0.24f):keyColor);
        const Color color=focused?mintInk:latched?mint:ready?ink:dim;
        const float cy=y+keyH/2;
        if(key.vk>=0x25&&key.vk<=0x28) {
            const float cx=x+w/2, a=keyH*0.15f, dx=key.vk==0x25?-1.0f:key.vk==0x27?1.0f:0.0f, dy=key.vk==0x26?-1.0f:key.vk==0x28?1.0f:0.0f;
            c.line(cx-dx*a,cy-dy*a,cx+dx*a,cy+dy*a,4,color);
            c.line(cx+dx*a,cy+dy*a,cx+dx*a*0.25f-dy*a*0.75f,cy+dy*a*0.25f+dx*a*0.75f,4,color);
            c.line(cx+dx*a,cy+dy*a,cx+dx*a*0.25f+dy*a*0.75f,cy+dy*a*0.25f-dx*a*0.75f,4,color);
            continue;
        }
        const char* name=k.label(i);
        const bool word=name[1]!=0;
        const Face face=word?Face::bold:Face::extrabold;
        const float size=word?small:big, kw=textWidth(face,size,name);
        label(c,face,size,x+w/2-kw/2,cy,name,color);
    }
}
void streamKeyboard(Canvas& c, const Model& m) {
    const auto& k=m.keyboard;
    char tag[192];
    if(*m.session.current.title)std::snprintf(tag,sizeof(tag),"KEYBOARD \xC2\xB7 TYPING INTO %s",m.session.current.title);
    else std::snprintf(tag,sizeof(tag),"KEYBOARD \xC2\xB7 TYPING INTO YOUR GAME");
    for(char* p=tag;*p;++p)if(*p>='a'&&*p<='z')*p=static_cast<char>(*p-32);
    drawFitted(c,Face::extrabold,22,left,baselineIn(Face::extrabold,22,193,24),tag,width,mint,0.14f);
    c.roundRect(left,240,width,120,24,mint);
    c.roundRect(left,240,width,116,24,surface);
    char state[48]="NO MODIFIERS";
    std::size_t at=0;
    for(const auto& [on,name]:{std::pair{k.shift,"SHIFT"},std::pair{k.caps,"CAPS LOCK"},std::pair{k.ctrl,"CTRL"},std::pair{k.alt,"ALT"}})
        if(on)at+=std::snprintf(state+at,sizeof(state)-at,"%s%s",at?" + ":"",name);
    const float stateW=labelRight(c,Face::extrabold,22,right-36,300,state,at?mint:dim,0.14f);
    drawFitted(c,Face::black,56,left+36,baselineIn(Face::black,56,300-40,80),m.inputReady?"Keys go straight to your game":"Waiting for game input",
        width-72-stateW-48,m.inputReady?muted:amber,-0.01f);
    keyGrid(c,k,m.inputReady,left,396,width,84,96,surface);
    label(c,Face::semibold,24,left,912,m.inputReady?"The picture returns when you close the keyboard. Your game and audio keep running.":
        "Key presses are ignored until the game\xE2\x80\x99s input channel opens. The picture returns when you close the keyboard.",dim);
    const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move"},{HintKind::glyph,Glyph::cross,nullptr,"Press key"},
        {HintKind::glyph,Glyph::square,nullptr,"Backspace"},{HintKind::glyph,Glyph::triangle,nullptr,"Space"},{HintKind::pill,Glyph::cross,"L1","Shift"}};
    const Hint r[]={{HintKind::glyph,Glyph::circle,nullptr,"Back to game"}};
    hints(c,l,5,r,1);
}

void overlayHints(Canvas& c, float x, float cy, const Hint* items, unsigned count) {
    for(unsigned i=0;i<count;++i){drawHint(c,items[i],x,cy);x+=hintWidth(items[i])+32;}
}
float hudItem(Canvas& c, float x, float cy, Glyph icon, const char* key, const char* text) {
    if(key)x+=pill(c,key,x,cy)+10;else x+=well(c,icon,x,cy)+10;
    return x+label(c,Face::bold,22,x,cy,text,ink)+28;
}
float hudWidth(Glyph, const char* key, const char* text) {return (key?pillWidth(key):44)+10+textWidth(Face::bold,22,text)+28;}
}
void renderOverlay(Canvas& c, const Overlay& o) noexcept {
    loadFonts();
    const float y=48, h=72, cy=y+h/2;
    c.roundRect(left,y,width,h,24,surface);
    c.roundRectStroke(left,y,width,h,24,2,hairline);
    const auto& k=o.keyboard;
    const char* mode=!o.inputReady?"WAITING FOR GAME INPUT":k.open?(o.launcher?"KEYBOARD \xC2\xB7 MOUSE PAUSED":"KEYBOARD"):"LAUNCHER MOUSE";
    const Color accent=o.inputReady?mint:amber;
    c.disc(left+28,cy,6,accent);
    label(c,Face::extrabold,20,left+46,cy,mode,accent,0.14f);
    struct Item { Glyph icon; const char* key; const char* text; };
    static const char* speeds[]={"Move \xC2\xB7 Slow","Move \xC2\xB7 Normal","Move \xC2\xB7 Fast"};
    Item items[8];unsigned count=0;
    if(!k.open) {
        items[count++]={Glyph::stick,nullptr,speeds[std::min(o.speed,2U)]};
        items[count++]={Glyph::cross,"R2","Left click"};
        items[count++]={Glyph::cross,"L2","Right click"};
        items[count++]={Glyph::dpad,nullptr,"Scroll"};
        items[count++]={Glyph::square,nullptr,"Speed"};
        items[count++]={Glyph::triangle,nullptr,"Keyboard"};
    } else items[count++]={Glyph::circle,nullptr,o.launcher?"Back to mouse":"Close"};
    float total=0;for(unsigned i=0;i<count;++i)total+=hudWidth(items[i].icon,items[i].key,items[i].text);
    const char* exit=o.launcher?"R3":nullptr;
    const float exitW=48+10+textWidth(Face::black,18,"+")+10+(exit?pillWidth(exit):44)+10+textWidth(Face::bold,22,o.launcher?"Exit":"Close");
    float x=right-24-exitW-total;
    for(unsigned i=0;i<count;++i)x=hudItem(c,x,cy,items[i].icon,items[i].key,items[i].text);
    x+=optionsPill(c,x,cy)+10;x+=label(c,Face::black,18,x,cy,"+",dim)+10;
    x+=(exit?pill(c,exit,x,cy):well(c,Glyph::triangle,x,cy))+10;
    label(c,Face::bold,22,x,cy,o.launcher?"Exit":"Close",ink);
    if(!k.open)return;
    const float ph=500, py=1080-40-ph;
    c.roundRect(left,py,width,ph,28,surface);
    c.roundRectStroke(left,py,width,ph,28,2,hairline);
    label(c,Face::extrabold,20,left+28,py+38,o.inputReady?"KEYBOARD \xC2\xB7 US QWERTY \xC2\xB7 TYPED KEYS ARE NOT SHOWN":"KEY PRESSES ARE IGNORED UNTIL THE GAME\xE2\x80\x99S INPUT CHANNEL OPENS",accent,0.14f);
    char state[48]="";std::size_t at=0;
    for(const auto& [on,name]:{std::pair{k.shift,"SHIFT"},std::pair{k.caps,"CAPS LOCK"},std::pair{k.ctrl,"CTRL"},std::pair{k.alt,"ALT"}})
        if(on)at+=std::snprintf(state+at,sizeof(state)-at,"%s%s",at?" + ":"",name);
    if(at)labelRight(c,Face::extrabold,18,right-28,py+38,state,mint,0.14f);
    keyGrid(c,k,o.inputReady,left+16,py+70,width-20,62,72,raised);
    const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move"},{HintKind::glyph,Glyph::cross,nullptr,"Press key"},
        {HintKind::glyph,Glyph::square,nullptr,"Backspace"},{HintKind::glyph,Glyph::triangle,nullptr,"Space"},
        {HintKind::pill,Glyph::cross,"L1","Shift"},{HintKind::glyph,Glyph::circle,nullptr,o.launcher?"Back to mouse":"Close"}};
    overlayHints(c,left+28,py+ph-36,l,6);
}
namespace {
void failure(Canvas& c, const Model& m) {
    const auto& cv=m.session;
    const Screen s=m.screen;
    const char* tag=s==Screen::streamEnded?"STREAM ENDED":s==Screen::cleanupFailed?"SESSION STILL RUNNING":"COULDN\xE2\x80\x99T START";
    const char* head=s==Screen::streamEnded?"The connection to your game dropped":s==Screen::cleanupFailed?"We couldn\xE2\x80\x99t end the cloud session":"The game didn\xE2\x80\x99t start";
    const char* text=s==Screen::streamEnded?"OpenNOW stopped the cloud session, so nothing keeps running on your account. You can try again right away.":
        s==Screen::cleanupFailed?"It may still be running on your account. End it before starting another game.":
        "The cloud session could not be started or confirmed. You can try again or go back.";
    warningIcon(c,left+18,314,coral);
    eyebrow(c,left+52,314,tag,coral);
    float y=headline(c,104,108,354,head,1300,2);
    y=body(c,y+24,text,980,2);
    char second[256]{};
    if(*cv.current.title) {
        char name[48];
        std::snprintf(second,sizeof(second),"%s \xC2\xB7 %s \xC2\xB7 %s",cv.current.title,cv.current.store,settingsName(m.launch,name,sizeof(name)).data());
    }
    detailCard(c,y+28,*cv.message?cv.message:"No details reported",second,coral);
    float x=left;
    if(s==Screen::streamEnded){x+=button(c,x,944,Glyph::cross,"Try again",true)+20;button(c,x,944,Glyph::square,"Back to games",false);}
    else if(s==Screen::launchFailed){x+=button(c,x,944,Glyph::cross,"Try again",true)+20;button(c,x,944,Glyph::circle,"Back",false);return;}
    else holdButton(c,x,944,"End session");
    const float closeW=16+48+16+textWidth(Face::black,28,"Close app")+36;
    button(c,right-closeW,944,Glyph::circle,"Close app",false);
}

void settingsRow(Canvas& c, float x, float y, float w, float h, bool focused) {
    c.roundRect(x,y,w,h,22,surface);
    if(focused)c.roundRectStroke(x-4,y-4,w+8,h+8,26,4,mint);
}
void infoRow(Canvas& c, float x, float y, float w, string_view key, string_view value, string_view sub, float h=116) {
    c.roundRect(x,y,w,h,22,surface);
    label(c,Face::extrabold,28,x+32,y+h/2-14,key,ink);
    label(c,Face::semibold,20,x+32,y+h/2+22,sub,dim);
    const float vw=std::min(textWidth(Face::mono,24,value),w-420);
    drawFitted(c,Face::mono,24,x+w-32-vw,baselineIn(Face::mono,24,y+h/2-18,36),value,w-420,ink);
}
struct Option { const char* text; bool on, disabled; };
void unavailableIcon(Canvas& c, float cx, float cy, Color color) {c.ring(cx,cy,7,2.2f,color);c.line(cx-5,cy+5,cx+5,cy-5,2.2f,color);}
float segmented(Canvas& c, float rightX, float cy, const Option* options, unsigned count) {
    float widths[4],total=8+4*(count-1.0f);
    for(unsigned i=0;i<count;++i){widths[i]=36+textWidth(Face::extrabold,20,options[i].text)+(options[i].disabled?24:0);total+=widths[i];}
    float x=rightX-total;
    c.roundRect(x,cy-24,total,48,24,raised);
    x+=4;
    for(unsigned i=0;i<count;++i) {
        const auto& o=options[i];
        if(o.on)c.roundRect(x,cy-20,widths[i],40,20,ink);
        float tx=x+18;
        if(o.disabled){unavailableIcon(c,tx+8,cy,dim);tx+=24;}
        label(c,Face::extrabold,20,tx,cy,o.text,o.on?ground:o.disabled?dim:muted);
        x+=widths[i]+4;
    }
    return total;
}
float stepper(Canvas& c, float rightX, float cy, string_view text) {
    const float tw=std::max(textWidth(Face::black,22,text),100.0f), w=8+36+8+tw+8+36+8, x=rightX-w;
    c.roundRect(x,cy-24,w,48,24,raised);
    for(int side=0;side<2;++side) {
        const float cx=side?x+w-8-18:x+8+18;
        c.disc(cx,cy,18,hairline);
        const float d=side?3.5f:-3.5f;
        c.line(cx-d,cy-7,cx+d,cy,3,ink);c.line(cx+d,cy,cx-d,cy+7,3,ink);
    }
    const float vw=textWidth(Face::black,22,text);
    label(c,Face::black,22,x+w/2-vw/2,cy,text,ink);
    return w;
}
float editValue(Canvas& c, float rightX, float cy, string_view text) {
    const float pw=8+30+8+textWidth(Face::extrabold,18,"Edit")+14, px=rightX-pw;
    c.roundRect(px,cy-20,pw,40,20,raised);
    c.disc(px+8+15,cy,15,hairline);glyph(c,Glyph::cross,px+8+15,cy,ink,18);
    label(c,Face::extrabold,18,px+8+30+8,cy,"Edit",ink);
    const float tw=textWidth(Face::mono,24,text);
    label(c,Face::mono,24,px-14-tw,cy,text,ink);
    return pw+14+tw;
}
void streamPane(Canvas& c, const Model& m, float x) {
    const StreamSettings& d=m.draft;
    const unsigned dirty=changedRows(d,m.defaults);
    const char* problem=settingsProblem(d,m.draftAvailable);
    const bool anyHardware=m.caps.h264Hardware||m.caps.hevcSdr||m.caps.hevcHdr;
    const float rw=800;
    char text[96],name[48];
    for(unsigned i=0;i<static_cast<unsigned>(StreamRow::count);++i) {
        const auto row=static_cast<StreamRow>(i);
        const float y=360+i*72.0f, cy=y+32, rx=x+rw-12;
        settingsRow(c,x,y,rw,64,m.settingsContent&&m.settingsRow==i);
        const char* head="";const char* sub="";Color subColor=dim;
        switch(row) {
        case StreamRow::preset: {
            head="Preset";
            bool preset=false;
            for(unsigned p=0;p<static_cast<unsigned>(StreamProfile::count);++p)preset=preset||presetFor(static_cast<StreamProfile>(p),d)==d;
            std::snprintf(text,sizeof(text),"%u qualified presets \xC2\xB7 edits make it Custom",m.choiceCount?m.choiceCount-1:0);
            sub=text;
            stepper(c,rx,cy,preset?settingsName(d,name,sizeof(name)):string_view("Custom"));
            break;
        }
        case StreamRow::decoding: {
            head="Decoding";
            sub=!anyHardware?"Hardware decoding isn\xE2\x80\x99t qualified on this PS5":d.hardware()?"PS5 video decoder":"CPU decoding, H.264 only";
            if(!anyHardware)subColor=amber;
            const Option o[]={{"Hardware accelerated",d.hardware(),!anyHardware},{"Software",!d.hardware(),false}};
            segmented(c,rx,cy,o,2);
            break;
        }
        case StreamRow::codec: {
            head="Codec";
            const bool hevc=m.caps.hevcSdr||m.caps.hevcHdr;
            sub=!d.hardware()?"Software decodes H.264 only":!hevc?"HEVC isn\xE2\x80\x99t qualified on this PS5":"AV1 is unavailable in this port";
            if(!d.hardware()||!hevc)subColor=amber;
            const Option o[]={{"H.264",!d.tenBit(),d.hardware()&&!m.caps.h264Hardware},{"HEVC",d.tenBit(),!d.hardware()||!hevc},{"AV1",false,true}};
            segmented(c,rx,cy,o,3);
            break;
        }
        case StreamRow::resolution:
            head="Resolution";
            sub=d.hardware()?"Even sizes, 320\xC3\x97" "180 to 3840\xC3\x97" "2160":"Software: up to 1920\xC3\x97" "1080";
            if(!d.hardware())subColor=amber;
            if(validateSettings(d)==SettingsError::dimensions){sub=problem;subColor=coral;}
            std::snprintf(text,sizeof(text),"%d \xC3\x97 %d",d.width,d.height);
            editValue(c,rx,cy,text);
            break;
        case StreamRow::fps:
            head="Frame rate";
            sub=d.hardware()?"Any whole number, 30 to 120":"Software: 30 to 60";
            if(!d.hardware())subColor=amber;
            if(validateSettings(d)==SettingsError::fps){sub=problem;subColor=coral;}
            std::snprintf(text,sizeof(text),"%d FPS",d.fps);
            stepper(c,rx,cy,text);
            break;
        case StreamRow::bitrate:
            head="Bitrate limit";
            sub="4 to 100 Mb/s \xC2\xB7 the cloud may send less";
            std::snprintf(text,sizeof(text),"%d Mb/s",d.bitrate_kbps/1000);
            stepper(c,rx,cy,text);
            break;
        case StreamRow::hdr: {
            head="HDR";
            sub=!d.tenBit()?"Needs HEVC and hardware decoding":!m.caps.hevcHdr?"HDR isn\xE2\x80\x99t qualified on this PS5":"HEVC Main10 with HDR10";
            if(d.tenBit()&&!m.caps.hevcHdr)subColor=amber;
            const Option o[]={{"Off",!d.hdr(),d.tenBit()&&!m.caps.hevcSdr},{"On",d.hdr(),!d.tenBit()||!m.caps.hevcHdr}};
            segmented(c,rx,cy,o,2);
            break;
        }
        case StreamRow::reset: {
            head="Reset to best available";
            sub="Forgets the saved default. Your NVIDIA login stays.";
            const float bw=44+12+textWidth(Face::bold,24,"Reset");
            well(c,Glyph::cross,rx-bw,cy);
            label(c,Face::bold,24,rx-bw+56,cy,"Reset",ink);
            break;
        }
        case StreamRow::count: break;
        }
        const float hw=label(c,Face::extrabold,24,x+28,y+20,head,ink);
        if(dirty&rowBit(row))c.disc(x+28+hw+12,y+20,5,amber);
        drawFitted(c,Face::semibold,18,x+28,baselineIn(Face::semibold,18,y+32,24),sub,430,subColor);
    }
    const float px=x+rw+32, pw=right-px, py=360, ph=568;
    c.roundRectStroke(px,py,pw,ph,22,2,hairline);
    label(c,Face::extrabold,18,px+24,py+36,"REQUESTED",mint,0.14f);
    std::snprintf(text,sizeof(text),"%d \xC3\x97 %d \xC2\xB7 %d FPS",d.width,d.height,d.fps);
    label(c,Face::mono,22,px+24,py+72,text,ink);
    std::snprintf(text,sizeof(text),"%s \xC2\xB7 %s \xC2\xB7 %d Mb/s max",d.tenBit()?"HEVC":"H.264",d.hdr()?"HDR":"SDR",d.bitrate_kbps/1000);
    label(c,Face::mono,22,px+24,py+106,text,ink);
    label(c,Face::mono,22,px+24,py+140,d.hardware()?"Hardware decoding":"Software decoding",ink);
    std::snprintf(text,sizeof(text),"%s audio",d.audio_mode==audio::Mode::automatic?"Auto":d.audio_mode==audio::Mode::stereo?"Stereo":audio::modeLabel(d.audio_mode));
    label(c,Face::mono,22,px+24,py+174,text,ink);
    c.roundRect(px+24,py+204,pw-48,2,1,hairline);
    label(c,Face::extrabold,18,px+24,py+238,"THIS PS5 OUTPUT",dim,0.14f);
    string_view output(m.output?m.output:"");
    if(output.rfind("OUTPUT ",0)==0)output.remove_prefix(7);
    drawWrapped(c,Face::mono,20,px+24,py+256,30,output,pw-48,2,muted);
    drawWrapped(c,Face::semibold,19,px+24,py+320,26,"The actual stream can differ. The game, your plan and the network decide.",pw-48,2,dim);
    const char* title;const char* body;Color accent;char status[96];
    if(dirty&&problem){title="Can\xE2\x80\x99t save yet";body=problem;accent=coral;}
    else if(dirty&&m.adjusted){std::snprintf(status,sizeof(status),"%u setting%s adjusted",m.adjusted,m.adjusted==1?"":"s");title=status;body="Changed to fit Software decoding. Kept until you Save or Revert.";accent=amber;}
    else if(dirty){std::snprintf(status,sizeof(status),"%u unsaved change%s",rowCount(dirty),rowCount(dirty)==1?"":"s");title=status;body="Kept until you Save or Revert, even if you leave.";accent=amber;}
    else if(m.settings.saveError){std::snprintf(status,sizeof(status),"Couldn\xE2\x80\x99t save on this PS5 \xC2\xB7 error %d",m.settings.saveError);title="Not saved";body=status;accent=coral;}
    else if(m.settings.savedUnavailable){title="Saved choice not qualified";body="It doesn\xE2\x80\x99t work on this PS5 or output now. Using the best available.";accent=amber;}
    else if(m.settings.loadCorrupt||m.settings.loadUnreadable){title="Saved settings ignored";body="They couldn\xE2\x80\x99t be read. Using the best available.";accent=amber;}
    else {title=m.settings.saved?"Saved on this PS5":"Best available";body=m.settings.saved?"Used when you press Play.":"Used when you press Play. Not saved yet.";accent=mint;}
    const float sy=py+ph-24-156;
    c.roundRect(px+24,sy,pw-48,156,18,raised);
    c.disc(px+24+24,sy+36,6,accent);
    drawFitted(c,Face::extrabold,24,px+24+42,baselineIn(Face::extrabold,24,sy+20,32),title,pw-48-60,ink);
    drawWrapped(c,Face::semibold,20,px+24+20,sy+64,26,body,pw-48-40,3,muted);
}
void numberEditor(Canvas& c, const Model& m) {
    const auto& e=m.edit;
    const StreamSettings candidate=applyEdit(e,m.draft);
    const auto error=validateSettings(candidate);
    c.roundRect(0,0,1920,1080,0,rgb(0x06090C),214);
    const float x=592, y=232, w=1232, h=560;
    c.roundRect(x,y,w,h,28,surface);
    c.roundRectStroke(x,y,w,h,28,2,hairline);
    const bool resolution=e.row==StreamRow::resolution;
    const char* title=resolution?"Custom resolution":e.row==StreamRow::fps?"Frame rate":"Bitrate limit";
    label(c,Face::black,40,x+48,y+66,title,ink);
    char was[48];
    if(resolution)std::snprintf(was,sizeof(was),"was %d \xC3\x97 %d",m.draft.width,m.draft.height);
    else if(e.row==StreamRow::fps)std::snprintf(was,sizeof(was),"was %d FPS",m.draft.fps);
    else std::snprintf(was,sizeof(was),"was %d Mb/s",m.draft.bitrate_kbps/1000);
    labelRight(c,Face::mono,20,x+w-48,y+66,was,dim);
    const auto bad=[&](unsigned from) {
        if(!resolution)return error!=SettingsError::none;
        const int v=editNumber(e,from,4);
        return from==0?(v<320||v>maxWidth(candidate)||(v&1)):(v<180||v>maxHeight(candidate)||(v&1));
    };
    const auto group=[&](float gx,const char* name,unsigned from,unsigned count) {
        const bool wrong=bad(from);
        label(c,Face::extrabold,18,gx,y+150,name,wrong?coral:dim,0.14f);
        const float gw=count*92.0f+4;
        if(wrong)c.roundRectStroke(gx-6,y+172,gw+8,128,22,2,coral);
        for(unsigned i=0;i<count;++i) {
            const unsigned index=from+i;
            const float cx=gx+i*92.0f;
            const bool active=index==e.cursor;
            c.roundRect(cx,y+178,84,116,16,raised);
            if(active) {
                c.roundRectStroke(cx-2,y+176,88,120,18,4,mint);
                c.line(cx+32,y+164,cx+42,y+154,3.2f,mint);c.line(cx+42,y+154,cx+52,y+164,3.2f,mint);
                c.line(cx+32,y+308,cx+42,y+318,3.2f,mint);c.line(cx+42,y+318,cx+52,y+308,3.2f,mint);
            }
            const char digit[2]{e.digits[index],0};
            const float dw=textWidth(Face::black,72,digit);
            drawText(c,Face::black,72,cx+42-dw/2,baselineIn(Face::black,72,y+178,116),digit,ink);
        }
        return gw;
    };
    float gx=x+48;
    if(resolution) {
        gx+=group(gx,"WIDTH",0,4)+28;
        label(c,Face::black,56,gx,y+236,"\xC3\x97",dim);
        group(gx+60,"HEIGHT",4,4);
    } else group(gx,e.row==StreamRow::fps?"FRAMES PER SECOND":"MEGABITS PER SECOND",0,3);
    const char* problem=settingsProblem(candidate,true);
    if(problem){warningIcon(c,x+64,y+374,coral);label(c,Face::extrabold,26,x+92,y+374,problem,coral);}
    else if(!m.editAvailable){warningIcon(c,x+64,y+374,amber);label(c,Face::extrabold,26,x+92,y+374,"Not qualified for this PS5 and output \xC2\xB7 Save stays off",amber);}
    else {c.disc(x+64,y+374,8,mint);label(c,Face::extrabold,26,x+92,y+374,"Valid",mint);}
    const char* range=resolution?(candidate.hardware()?"Width 320 to 3840, height 180 to 2160, both even.":"Software decoding: width 320 to 1920, height 180 to 1080, both even."):
        e.row==StreamRow::fps?(candidate.hardware()?"Any whole number from 30 to 120.":"Software decoding: 30 to 60."):"Any whole number from 4 to 100.";
    label(c,Face::semibold,20,x+92,y+410,range,dim);
    label(c,Face::extrabold,18,x+48,y+464,resolution?"COMMON SIZES \xC2\xB7 L1 / R1":"COMMON VALUES \xC2\xB7 L1 / R1",dim,0.14f);
    float cx=x+48;
    char chip[32];
    const auto drawChip=[&](const char* t){const float cw=textWidth(Face::mono,20,t)+36;c.roundRectStroke(cx,y+486,cw,44,22,2,hairline);label(c,Face::mono,20,cx+18,y+508,t,muted);cx+=cw+10;};
    if(resolution){for(const auto& size:commonSizes)if(size.width<=maxWidth(candidate)&&size.height<=maxHeight(candidate)){std::snprintf(chip,sizeof(chip),"%d \xC3\x97 %d",size.width,size.height);drawChip(chip);}}
    else if(e.row==StreamRow::fps){for(int v:commonFps)if(v<=maxFps(candidate)){std::snprintf(chip,sizeof(chip),"%d",v);drawChip(chip);}}
    else for(int v:commonBitrates){std::snprintf(chip,sizeof(chip),"%d Mb/s",v);drawChip(chip);}
    const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Pick digit / change"},{HintKind::chord,Glyph::cross,nullptr,resolution?"Common sizes":"Common values"}};
    const Hint r[]={{HintKind::glyph,Glyph::cross,nullptr,problem?"Done (fix first)":"Done"},{HintKind::glyph,Glyph::circle,nullptr,"Cancel"}};
    hints(c,l,2,r,2);
}
void cachePane(Canvas& c, const Model& m, float x, float w) {
    const auto& d=m.disk;
    const double megabytes=double(d.bytes)/(1024.0*1024.0);
    const bool known=d.available;
    char big[32],line[128];
    if(known)std::snprintf(big,sizeof(big),megabytes<10?"%.1f MB":"%.0f MB",megabytes);
    else std::snprintf(big,sizeof(big),"Unknown");
    const float bw=drawText(c,Face::black,112,x,baselineIn(Face::black,112,372,112),big,known?ink:muted,-0.03f);
    std::snprintf(line,sizeof(line),known?"of %zu MB limit":"usage \xC2\xB7 %zu MB limit",art::DiskCache::budgetBytes>>20);
    label(c,Face::bold,30,x+bw+18,452,line,dim);
    c.roundRect(x,500,w,12,6,raised);
    const float fill=std::min(1.0f,float(double(d.bytes)/double(art::DiskCache::budgetBytes)));
    if(known&&fill>0)c.roundRect(x,500,std::max(12.0f,w*fill),12,6,mint);
    const char* state=d.error?"Cover art storage reported an error on this PS5":d.busy?"Updating\xE2\x80\xA6":!d.enabled?"Saving is off":"oldest removed first when full";
    if(known)std::snprintf(line,sizeof(line),"%zu of %u images \xC2\xB7 %s",d.count,art::DiskCache::slots,state);
    else std::snprintf(line,sizeof(line),"%s \xC2\xB7 saved covers aren\xE2\x80\x99t counted until saving is on",state);
    label(c,Face::mono,20,x,544,line,d.error?coral:muted);
    const bool toggleFocus=m.settingsContent&&m.settingsRow==0, clearFocus=m.settingsContent&&m.settingsRow==1;
    settingsRow(c,x,598,w,92,toggleFocus);
    label(c,Face::extrabold,26,x+28,630,"Save cover art on this PS5",ink);
    label(c,Face::semibold,20,x+28,662,"Off keeps covers in memory only. They download again each launch.",dim);
    const Option o[]={{"Off",!m.artworkWanted,false},{"On",m.artworkWanted,false}};
    segmented(c,x+w-16,644,o,2);
    settingsRow(c,x,706,w,92,clearFocus);
    const bool canClear=m.art&&!d.busy&&(!known||d.count>0);
    label(c,Face::extrabold,26,x+28,738,"Clear cover art cache",ink);
    label(c,Face::semibold,20,x+28,770,canClear?(known?"Removes saved images only. You stay signed in and keep your settings.":"Removes any saved images, even while saving is off. You stay signed in."):
        d.busy?"Busy right now. Try again in a moment.":"Nothing saved to clear.",dim);
    const float cw=8+36+10+textWidth(Face::black,22,"Clear")+22, cx=x+w-16-cw;
    c.roundRect(cx,726,cw,52,26,canClear&&clearFocus?mint:raised);
    c.disc(cx+8+18,752,18,canClear&&clearFocus?rgb(0x0E4A2C):hairline);glyph(c,Glyph::cross,cx+8+18,752,canClear?ink:dim,20);
    label(c,Face::black,22,cx+8+36+10,752,"Clear",canClear&&clearFocus?mintInk:canClear?ink:dim);
    if(m.settings.cacheSaveError){std::snprintf(line,sizeof(line),"Couldn\xE2\x80\x99t save this choice on this PS5 \xC2\xB7 error %d",m.settings.cacheSaveError);c.disc(x+34,836,5,coral);label(c,Face::bold,20,x+48,836,line,coral);}
    else {c.disc(x+34,836,5,mint);label(c,Face::bold,20,x+48,836,"Saved on this PS5 \xC2\xB7 changes apply right away",muted);}
}
void clearConfirm(Canvas& c, const Model& m) {
    c.roundRect(0,0,1920,1080,0,rgb(0x06090C),214);
    const float x=460, y=250, w=1000, h=530;
    c.roundRect(x,y,w,h,28,surface);
    c.roundRectStroke(x,y,w,h,28,2,hairline);
    char head[64];
    const bool known=m.disk.available;
    const double megabytes=double(m.disk.bytes)/(1024.0*1024.0);
    if(known)std::snprintf(head,sizeof(head),megabytes<10?"Clear %.1f MB of cover art?":"Clear %.0f MB of cover art?",megabytes);
    else std::snprintf(head,sizeof(head),"Clear saved cover art?");
    drawFitted(c,Face::black,56,x+52,baselineIn(Face::black,56,y+44,62),head,w-104,ink,-0.02f);
    label(c,Face::semibold,26,x+52,y+150,"Covers download again the next time you open Library or Browse.",muted);
    c.roundRect(x+52,y+194,w-104,2,1,hairline);
    label(c,Face::extrabold,18,x+52,y+232,"REMOVED",coral,0.14f);
    char removed[48];
    if(known)std::snprintf(removed,sizeof(removed),"%zu saved cover image%s",m.disk.count,m.disk.count==1?"":"s");
    else std::snprintf(removed,sizeof(removed),"All saved cover images");
    label(c,Face::bold,24,x+52,y+272,removed,ink);
    label(c,Face::extrabold,18,x+520,y+232,"KEPT",mint,0.14f);
    label(c,Face::bold,24,x+520,y+272,"NVIDIA sign-in",ink);
    label(c,Face::bold,24,x+520,y+314,"Stream settings",ink);
    label(c,Face::bold,24,x+520,y+356,"Your Library list",ink);
    c.roundRect(x+52,y+394,w-104,2,1,hairline);
    const float bw=button(c,x+52,y+422,Glyph::cross,"Clear cache",true);
    c.roundRectStroke(x+48,y+418,bw+8,88,44,4,mint);
    button(c,x+52+bw+20,y+422,Glyph::circle,"Keep",false);
    const Hint r[]={{HintKind::glyph,Glyph::cross,nullptr,"Clear cache"},{HintKind::glyph,Glyph::circle,nullptr,"Keep"}};
    hints(c,nullptr,0,r,2);
}
void fixedConfirm(Canvas& c, const Model& m) {
    c.roundRect(0,0,1920,1080,0,rgb(0x06090C),214);
    const float x=440, y=220, w=1040, h=570;
    c.roundRect(x,y,w,h,28,surface);
    c.roundRectStroke(x,y,w,h,28,2,hairline);
    label(c,Face::extrabold,18,x+52,y+56,"HARDWARE H.264",amber,0.14f);
    drawWrapped(c,Face::black,50,x+52,y+84,56,"Use fixed-resolution streaming with hardware decoding?",w-104,2,ink);
    drawWrapped(c,Face::semibold,24,x+52,y+212,34,"Hardware H.264 can\xE2\x80\x99t follow resolution changes during a stream yet, so the cloud must keep one fixed resolution. This only changes the draft; it still needs Save.",w-104,2,muted);
    c.roundRect(x+52,y+296,w-104,2,1,hairline);
    const auto change=[&](float ry,const char* key,const char* from,const char* to) {
        label(c,Face::bold,22,x+52,ry,key,muted);
        const float fw=label(c,Face::mono,22,x+288,ry,from,dim);
        const float ax=x+288+fw+16;
        c.line(ax,ry,ax+20,ry,2.6f,dim);c.line(ax+13,ry-7,ax+20,ry,2.6f,dim);c.line(ax+13,ry+7,ax+20,ry,2.6f,dim);
        label(c,Face::mono,22,ax+36,ry,to,ink);
    };
    change(y+336,"Decoding",m.draft.hardware()?(m.draft.tenBit()?"Hardware HEVC":"Hardware H.264"):"Software","Hardware H.264");
    change(y+378,"Resolution policy","Adaptive","Fixed");
    c.roundRect(x+52,y+412,w-104,2,1,hairline);
    const float bw=button(c,x+52,y+446,Glyph::cross,"Use fixed resolution",true);
    c.roundRectStroke(x+48,y+442,bw+8,88,44,4,mint);
    button(c,x+52+bw+20,y+446,Glyph::circle,"Keep current draft",false);
    const Hint r[]={{HintKind::glyph,Glyph::cross,nullptr,"Use fixed resolution"},{HintKind::glyph,Glyph::circle,nullptr,"Keep current draft"}};
    hints(c,nullptr,0,r,2);
}
const char* layoutName(unsigned channels) {return channels==8?"7.1":channels==6?"5.1":"Stereo";}
void displayPane(Canvas& c, const Model& m, float x, float w) {
    string_view output(m.output?m.output:"");
    if(output.rfind("OUTPUT ",0)==0)output.remove_prefix(7);
    infoRow(c,x,368,w,"Video output",output,"VideoOut mode chosen at startup",94);
    char codecs[96]{};
    for(const auto& [on,text]:{std::pair{m.caps.hevcHdr,"HEVC Main10 HDR"},std::pair{m.caps.hevcSdr,"HEVC Main10"},std::pair{m.caps.h264Hardware,"H.264"}})
        if(on)std::snprintf(codecs+std::strlen(codecs),sizeof(codecs)-std::strlen(codecs),"%s%s",*codecs?" \xC2\xB7 ":"",text);
    infoRow(c,x,474,w,"Hardware video",*codecs?codecs:"Software decoding only","Modes that passed the startup test streams",94);
    const auto mode=m.draft.audio_mode;
    const unsigned capacity=m.audio.capacity, asked=audio::requestedChannels(mode,capacity);
    settingsRow(c,x,580,w,94,m.settingsContent&&m.settingsRow==0);
    const float hw=label(c,Face::extrabold,28,x+32,614,"Audio channels",ink);
    if(mode!=m.defaults.audio_mode)c.disc(x+32+hw+14,614,5,amber);
    char line[256];
    if(mode==audio::Mode::automatic)std::snprintf(line,sizeof(line),"Auto asks for the widest layout the startup audio port probe accepted: up to %u channels",capacity);
    else if(mode==audio::Mode::stereo)std::snprintf(line,sizeof(line),"Asks NVIDIA for 2 channels");
    else if(asked<(mode==audio::Mode::surround71?8U:6U))std::snprintf(line,sizeof(line),"The startup audio port probe accepted %u channels, so OpenNOW asks for %s",capacity,layoutName(asked));
    else std::snprintf(line,sizeof(line),"Asks NVIDIA for %u channels. The session can still negotiate stereo.",asked);
    drawFitted(c,Face::semibold,20,x+32,baselineIn(Face::semibold,20,630,28),line,w-500,dim);
    const Option o[]={{"Auto",mode==audio::Mode::automatic,false},{"Stereo",mode==audio::Mode::stereo,false},
                      {"5.1",mode==audio::Mode::surround51,false},{"7.1",mode==audio::Mode::surround71,false}};
    segmented(c,x+w-16,627,o,4);
    const float cy=684;
    c.roundRectStroke(x,cy,w,198,22,2,hairline);
    label(c,Face::extrabold,18,x+32,cy+30,"LAST SESSION AUDIO",dim,0.14f);
    const auto& a=m.audio;
    if(!a.requested) {
        drawWrapped(c,Face::semibold,22,x+32,cy+60,32,"No stream yet since OpenNOW started. After you play, this shows the layout requested and the layout negotiated for that session.",w-64,2,muted);
    } else {
        label(c,Face::semibold,18,x+32,cy+66,"Requested",dim);
        std::snprintf(line,sizeof(line),"%s \xC2\xB7 %u channels",layoutName(a.requested),a.requested);
        const float rw=label(c,Face::mono,24,x+32,cy+96,line,ink);
        const float nx=x+32+std::max(rw+48,280.0f);
        label(c,Face::semibold,18,nx,cy+66,"Negotiated",dim);
        if(a.negotiated)std::snprintf(line,sizeof(line),"%s \xC2\xB7 Opus %u channels",layoutName(a.negotiated),a.negotiated);
        else std::snprintf(line,sizeof(line),"Not negotiated");
        label(c,Face::mono,24,nx,cy+96,line,a.negotiated&&a.negotiated>=a.requested?ink:amber);
        char why[96];
        if(a.fallback)std::snprintf(why,sizeof(why),"The cloud described surround but sent stereo, so OpenNOW fell back to stereo.");
        else if(!a.negotiated)std::snprintf(why,sizeof(why),"Audio wasn\xE2\x80\x99t set up in that session.");
        else if(a.negotiated<a.requested)std::snprintf(why,sizeof(why),"This session negotiated %s instead of the requested layout.",layoutName(a.negotiated));
        else std::snprintf(why,sizeof(why),"This session negotiated the requested layout.");
        std::snprintf(line,sizeof(line),"%s The startup audio port probe accepted %u channels; that isn\xE2\x80\x99t a check of your speakers or receiver.",why,a.capacity);
        drawWrapped(c,Face::semibold,20,x+32,cy+122,28,line,w-64,2,muted);
    }
    const unsigned dirty=changedRows(m.draft,m.defaults);
    const char* problem=settingsProblem(m.draft,m.draftAvailable);
    const Color dot=dirty&&problem?coral:dirty?amber:m.settings.saveError?coral:mint;
    if(dirty&&problem)std::snprintf(line,sizeof(line),"Can\xE2\x80\x99t save yet \xC2\xB7 %s",problem);
    else if(dirty==audioBit)std::snprintf(line,sizeof(line),"Unsaved \xC2\xB7 saved with your stream settings \xC2\xB7 Options saves,");
    else if(dirty)std::snprintf(line,sizeof(line),"%u unsaved stream changes \xC2\xB7 Options saves all of them,",rowCount(dirty));
    else if(m.settings.saveError)std::snprintf(line,sizeof(line),"Couldn\xE2\x80\x99t save on this PS5 \xC2\xB7 error %d",m.settings.saveError);
    else std::snprintf(line,sizeof(line),"Saved with your stream settings \xC2\xB7 used when you press Play");
    c.disc(x+38,914,5,dot);
    const float fw=label(c,Face::bold,20,x+54,914,line,muted);
    if(dirty&&!problem) {
        glyph(c,Glyph::square,x+54+fw+18,914,muted,22);
        label(c,Face::bold,20,x+54+fw+36,914,dirty==audioBit?"reverts":"reverts all",muted);
    }
}
void settings(Canvas& c, const Model& m) {
    const char* panes[]={"Stream","Display & audio","Cover art & cache","Account","About"};
    for(unsigned i=0;i<settingsPanes;++i) {
        const float y=216+i*80.0f;
        const bool active=i==static_cast<unsigned>(m.pane);
        if(active) {
            c.roundRect(left,y,400,72,18,surface);
            c.roundRect(left+24,y+20,6,32,3,mint);
            if(!m.settingsContent)c.roundRectStroke(left-4,y-4,408,80,22,4,mint);
        }
        label(c,active?Face::black:Face::bold,28,left+46,y+36,panes[i],active?ink:muted);
    }
    const float x=592, w=right-x;
    const auto title=[&](string_view head,string_view sub){
        drawText(c,Face::black,72,x,baselineIn(Face::black,72,214,76),head,ink,-0.02f);
        drawWrapped(c,Face::semibold,26,x,302,36,sub,w,2,muted);
    };
    if(m.pane==SettingsPane::stream) {
        title("Stream","What OpenNOW asks NVIDIA for. The cloud and this PS5 decide what you actually get.");
        streamPane(c,m,x);
    } else if(m.pane==SettingsPane::cache) {
        title("Cover art & cache",m.artworkWanted?"Covers are saved on this PS5, so Library and Browse open fast after a restart.":"Saving is off, so covers download again after each restart.");
        cachePane(c,m,x,w);
    } else if(m.pane==SettingsPane::display) {
        title("Display & audio","Video output is reported by this PS5. Audio channels are a request to NVIDIA.");
        displayPane(c,m,x,w);
    } else if(m.pane==SettingsPane::account) {
        title("Account","Your NVIDIA sign-in for GeForce NOW on this PS5.");
        c.roundRect(x,380,w,126,22,surface);
        c.disc(x+32+32,443,32,raised);glyph(c,Glyph::check,x+64,443,mint,30);
        label(c,Face::extrabold,30,x+120,424,"Signed in to NVIDIA",ink);
        label(c,Face::semibold,22,x+120,462,m.login.sessionSaved?"Login saved on this PS5 and restored when OpenNOW starts.":"Signed in for now \xC2\xB7 the login couldn\xE2\x80\x99t be saved on this PS5.",m.login.sessionSaved?muted:amber);
        settingsRow(c,x,520,w,126,m.settingsContent);
        label(c,Face::extrabold,30,x+32,564,"Sign out",coral);
        label(c,Face::semibold,22,x+32,602,"Removes the saved login. Signing in again needs a new code.",muted);
        const float sw=44+12+textWidth(Face::bold,24,"Sign out\xE2\x80\xA6");
        well(c,Glyph::cross,x+w-32-sw,583);
        label(c,Face::bold,24,x+w-32-sw+56,583,"Sign out\xE2\x80\xA6",ink);
        label(c,Face::extrabold,20,x+32,690,"GOOD TO KNOW",dim,0.14f);
        drawWrapped(c,Face::semibold,22,x+32,712,32,"OpenNOW never asks for your NVIDIA password. Sign-in happens on NVIDIA\xE2\x80\x99s site with a one-time code from this PS5.",w-64,2,muted);
    } else {
        title("About","OpenNOW for PS5 is an independent, unofficial GeForce NOW client.");
        infoRow(c,x,384,w,"Version",OPENNOW_VERSION,"Alpha build");
        c.roundRect(x,514,w,240,22,surface);
        label(c,Face::extrabold,28,x+32,558,"Credits & licenses",ink);
        drawWrapped(c,Face::semibold,22,x+32,584,32,"Built on OpenNOW and OpenNOW-Switch by Open Cloud Gaming. Fonts: Nunito and IBM Plex Mono (SIL OFL 1.1). stb_truetype and stb_image (public domain / MIT). Full notices ship with the app in THIRD_PARTY_NOTICES.",w-64,4,muted);
        drawWrapped(c,Face::semibold,22,x+32,790,32,"Not affiliated with NVIDIA or Sony. GeForce NOW is a trademark of NVIDIA.",w-64,2,dim);
    }
    if(m.edit.open()){numberEditor(c,m);return;}
    if(m.confirmClear){clearConfirm(c,m);return;}
    if(m.confirmFixed){fixedConfirm(c,m);return;}
    if(m.confirmSignOut) {
        c.roundRect(0,0,1920,1080,0,rgb(0x06090C),200);
        c.roundRect(480,300,960,548,32,surface);
        label(c,Face::extrabold,22,536,370,"SIGN OUT",coral,0.14f);
        drawWrapped(c,Face::black,64,536,400,68,"Sign out of NVIDIA on this PS5?",848,2,ink);
        drawWrapped(c,Face::semibold,26,536,560,38,"The saved login is deleted from this PS5. Signing out doesn\xE2\x80\x99t remove games from your GeForce NOW library or change purchases on Steam, Epic or other stores.",848,3,muted);
        const float bw=16+48+16+textWidth(Face::black,28,"Sign out")+36;
        c.roundRectStroke(527,705,bw+18,98,49,4,mint);
        c.roundRect(536,714,bw,80,40,coral);
        c.disc(536+16+24,754,24,rgb(0x3A0E0A));glyph(c,Glyph::cross,536+40,754,ink);
        label(c,Face::black,28,536+80,754,"Sign out",rgb(0x3A0E0A));
        button(c,536+bw+20,714,Glyph::circle,"Cancel",false);
        return;
    }
    if(m.settingsContent&&m.pane==SettingsPane::stream) {
        const auto row=static_cast<StreamRow>(m.settingsRow);
        const bool editable=row==StreamRow::resolution||row==StreamRow::fps||row==StreamRow::bitrate;
        const bool canSave=changedRows(m.draft,m.defaults)&&!settingsProblem(m.draft,m.draftAvailable);
        const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move / change"},{HintKind::glyph,Glyph::cross,nullptr,row==StreamRow::reset?"Reset":"Edit"}};
        const Hint r[]={{canSave?HintKind::optionsMint:HintKind::glyph,Glyph::options,nullptr,"Save"},{HintKind::glyph,Glyph::square,nullptr,"Revert"},back};
        hints(c,l,editable||row==StreamRow::reset?2:1,r,3);
        return;
    }
    if(m.settingsContent&&m.pane==SettingsPane::display) {
        const bool canSave=changedRows(m.draft,m.defaults)&&!settingsProblem(m.draft,m.draftAvailable);
        const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move / change"}};
        const Hint r[]={{canSave?HintKind::optionsMint:HintKind::glyph,Glyph::options,nullptr,"Save"},{HintKind::glyph,Glyph::square,nullptr,"Revert"},back};
        hints(c,l,1,r,3);
        return;
    }
    if(m.settingsContent) {
        const bool change=m.pane==SettingsPane::cache&&m.settingsRow==0;
        const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,change?"Move / change":"Move"},{HintKind::glyph,Glyph::cross,nullptr,"Select"}};
        const Hint r[]={back};
        hints(c,l,2,r,1);
        return;
    }
    const Hint l[]={{HintKind::glyph,Glyph::dpad,nullptr,"Move"},{HintKind::glyph,Glyph::cross,nullptr,"Open"}};
    sectionHints(c,l,settingsRows(m.pane)?2:1);
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
    return pane==SettingsPane::stream?static_cast<unsigned>(StreamRow::count):pane==SettingsPane::cache?2:pane==SettingsPane::account||pane==SettingsPane::display?1:0;
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
    c.clear(ground);
    const bool tabs=m.screen==Screen::grid||m.screen==Screen::catalogLoading||m.screen==Screen::catalogEmpty||
                    m.screen==Screen::catalogError||m.screen==Screen::settings;
    topBar(c,m,tabs);
    switch(m.screen) {
    case Screen::signIn: signIn(c,m);break;
    case Screen::catalogLoading: case Screen::catalogEmpty: case Screen::catalogError: catalogStatus(c,m);break;
    case Screen::cleanupFailed: case Screen::streamEnded: case Screen::launchFailed: failure(c,m);break;
    case Screen::grid: grid(c,m);break;
    case Screen::search: search(c,m);break;
    case Screen::detail: detail(c,m);break;
    case Screen::settings: settings(c,m);break;
    case Screen::launching: case Screen::connecting: progress(c,m);break;
    case Screen::keyboard: streamKeyboard(c,m);break;
    }
}
}
