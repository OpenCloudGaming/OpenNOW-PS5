// SPDX-License-Identifier: GPL-3.0-or-later
#define main preview_entry
#include "../src/main.cpp"
#undef main
#include "../src/ui/font.hpp"
#include <cassert>

namespace { unsigned testButtons=0; }
extern "C" {
int sceUserServiceGetInitialUser(int* user) {*user=0;return 0;}
int scePadOpen(int,int,int,void*) {return 1;}
int scePadReadState(int,PS5_PadData* data) {data->connected=true;data->buttons=testButtons;return 0;}
}
opennow::Http::Http() noexcept=default;
opennow::Http::~Http()=default;

namespace {
using opennow::CloudState;
using opennow::StreamProfile;
using opennow::ui::Screen;
void game(unsigned i,const char* title,const char* store) {
    std::snprintf(publishedCloud.games[i].id,sizeof(publishedCloud.games[i].id),"%u",10+i);
    std::snprintf(publishedCloud.games[i].title,sizeof(publishedCloud.games[i].title),"%s",title);
    std::snprintf(publishedCloud.games[i].store,sizeof(publishedCloud.games[i].store),"%s",store);
}
Screen current() {
    return opennow::ui::screenFor({published,publishedCloud,publishedStream,publishedSession,publishedStreamFailure,searchInput.open,detailOpen});
}
void textChecks(ps5::demo::Canvas& canvas) {
    using opennow::ui::Face;
    assert(opennow::ui::loadFonts());
    const std::string_view bad("A\xC3\x28" "B\xE2\x82" "\xF0\x9F\x98\x80" "\xED\xA0\x80",13);
    std::size_t at=0;unsigned count=0;char32_t points[16]{};
    while(at<bad.size()&&count<16)points[count++]=opennow::ui::nextCodepoint(bad,at);
    assert(count==8);
    assert(points[0]=='A'&&points[1]==0xFFFD&&points[2]=='('&&points[3]=='B'&&points[4]==0xFFFD);
    assert(points[6]==0x1F600&&points[count-1]==0xFFFD);
    const std::string_view title="Ultra Long Title: The Definitive Remastered Collector\xE2\x80\x99s Edition With Every Expansion \xE6\x97\xA5\xE6\x9C\xAC";
    const std::size_t prefix=opennow::ui::fittingPrefix(Face::black,30,title,216);
    assert(prefix>0&&prefix<title.size()&&opennow::ui::textWidth(Face::black,30,title.substr(0,prefix))<=216);
    const float drawn=opennow::ui::drawFitted(canvas,Face::black,104,96,300,title,600,ps5::demo::Color::white);
    assert(drawn<=601);
    assert(opennow::ui::drawWrapped(canvas,Face::black,30,0,0,34,title,216,4,ps5::demo::Color::white,255,false)==4);
    assert(opennow::ui::drawWrapped(canvas,Face::semibold,28,0,0,40,"Short",400,3,ps5::demo::Color::white,255,false)==1);
    assert(opennow::ui::textWidth(Face::mono,22,bad)>0);
}

bool scenarios(ps5::demo::Canvas& canvas) noexcept {
    textChecks(canvas);
    opennow::Http http;
    activeHttp=&http;
    published.state=State::authenticated;
    publishedCloud.state=CloudState::catalog;
    publishedProfile=StreamProfile::quality;
    for(unsigned i=0;i<static_cast<unsigned>(StreamProfile::count);++i)
        if(!opennow::settingsFor(static_cast<StreamProfile>(i)).hardware)publishedProfileMask|=1U<<i;
    const auto press=[&](unsigned buttons){testButtons=0;draw(canvas);testButtons=buttons;draw(canvas);};
    assert(current()==Screen::catalogEmpty);
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.load()==0&&!detailOpen);
    press(PS5_PAD_BUTTON_OPTIONS);
    assert(command.load()==0&&!searchInput.open&&!http.cancelled);

    const char* titles[][2]={{"Alpha","STEAM"},{"Beta","XBOX"},{"Dungeons II","XBOX"},{"Delta","EPIC"},{"Echo","GOG"},{"Foxtrot","UBISOFT"},{"Golf","STEAM"},{"Dungeons II","STEAM"}};
    for(unsigned i=0;i<8;++i)game(i,titles[i][0],titles[i][1]);
    publishedCloud.count=8;publishedCloud.revision=1;
    testButtons=0;draw(canvas);
    assert(libraryFocus==0&&current()==Screen::library);
    press(PS5_PAD_BUTTON_LEFT);assert(libraryFocus==0);
    press(PS5_PAD_BUTTON_UP);assert(libraryFocus==0);
    press(PS5_PAD_BUTTON_RIGHT);assert(libraryFocus==1);
    press(PS5_PAD_BUTTON_DOWN);assert(libraryFocus==7);
    press(PS5_PAD_BUTTON_DOWN);assert(libraryFocus==7);
    press(PS5_PAD_BUTTON_RIGHT);assert(libraryFocus==7);
    press(PS5_PAD_BUTTON_UP);assert(libraryFocus==1);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);
    assert(libraryFocus==4);
    press(PS5_PAD_BUTTON_DOWN);assert(libraryFocus==7);
    press(PS5_PAD_BUTTON_LEFT);assert(libraryFocus==6);
    press(PS5_PAD_BUTTON_UP);assert(libraryFocus==0);
    assert(command.load()==0&&!http.cancelled);
    press(PS5_PAD_BUTTON_L1);assert(command.exchange(0)==9);
    press(PS5_PAD_BUTTON_R1);assert(command.exchange(0)==7);
    press(PS5_PAD_BUTTON_SQUARE);assert(command.exchange(0)==6);

    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);
    assert(libraryFocus==2);
    press(PS5_PAD_BUTTON_CROSS);
    assert(detailOpen&&command.load()==0&&current()==Screen::detail&&detailProfile==StreamProfile::quality);
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(!detailOpen&&command.load()==0&&!http.cancelled&&current()==Screen::library);
    press(PS5_PAD_BUTTON_CROSS);
    assert(detailOpen);
    press(PS5_PAD_BUTTON_OPTIONS);
    assert(command.load()==0&&detailOpen);
    press(PS5_PAD_BUTTON_SQUARE);press(PS5_PAD_BUTTON_R1);
    assert(command.load()==0&&detailOpen);
    press(PS5_PAD_BUTTON_LEFT);assert(libraryFocus==2);
    press(PS5_PAD_BUTTON_RIGHT);assert(libraryFocus==7);
    press(PS5_PAD_BUTTON_RIGHT);assert(libraryFocus==7);
    press(PS5_PAD_BUTTON_UP);assert(command.load()==0&&detailProfile==StreamProfile::quality);
    press(PS5_PAD_BUTTON_DOWN);
    {
        Request request;
        assert(detailProfile==StreamProfile::smooth&&takeCommand(request)==14&&request.index==-1&&request.profile==static_cast<int>(StreamProfile::smooth));
    }
    press(PS5_PAD_BUTTON_L1);
    assert(detailProfile==StreamProfile::experimental&&command.load()==14);
    {
        Request request;
        assert(takeCommand(request)==14);
        opennow::StreamProfile workerProfile=StreamProfile::quality;
        assert(acceptProfile(request.profile,publishedProfileMask,workerProfile)&&workerProfile==StreamProfile::experimental);
        assert(!acceptProfile(-1,publishedProfileMask,workerProfile));
        assert(!acceptProfile(static_cast<int>(StreamProfile::native_hdr120),publishedProfileMask,workerProfile)&&workerProfile==StreamProfile::experimental);
        assert(!acceptProfile(99,publishedProfileMask,workerProfile));
    }
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.load()==5&&!detailOpen);
    {
        Request launched;
        assert(takeCommand(launched)==5);
        press(PS5_PAD_BUTTON_CROSS);
        press(PS5_PAD_BUTTON_DOWN);
        assert(launched.index==7&&launched.profile==static_cast<int>(StreamProfile::experimental));
        Request next;
        assert(takeCommand(next)==14&&next.index==-1&&next.profile==static_cast<int>(StreamProfile::smooth));
        assert(takeCommand(next)==0&&next.index==-1&&next.profile==-1);
        submit(5,{1,static_cast<int>(StreamProfile::smooth)});
        submit(14,{-1,static_cast<int>(StreamProfile::quality)});
        assert(takeCommand(next)==14&&next.index==-1&&next.profile==static_cast<int>(StreamProfile::quality));
        submit(5,{4,static_cast<int>(StreamProfile::smooth)});
        command.store(10);
        assert(takeCommand(next)==10);
        press(PS5_PAD_BUTTON_CIRCLE);
        assert(!detailOpen&&command.load()==0);
    }
    press(PS5_PAD_BUTTON_CROSS);assert(detailOpen);
    testButtons=PS5_PAD_BUTTON_L1;draw(canvas);
    {Request discarded;assert(takeCommand(discarded)==14);}
    detailProfile=StreamProfile::quality;
    testButtons=PS5_PAD_BUTTON_L1|PS5_PAD_BUTTON_R1;draw(canvas);
    assert(command.exchange(0)==11&&http.cancelled&&!detailOpen);
    http.cancelled=false;testButtons=0;draw(canvas);
    press(PS5_PAD_BUTTON_CROSS);assert(detailOpen);
    publishedCloud.revision=2;testButtons=0;draw(canvas);
    assert(libraryFocus==0&&!detailOpen);

    press(PS5_PAD_BUTTON_TRIANGLE);
    assert(searchInput.open&&current()==Screen::search);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_CROSS);
    assert(!std::strcmp(searchInput.text,"B"));
    press(PS5_PAD_BUTTON_SQUARE);assert(!*searchInput.text);
    press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_OPTIONS);
    assert(command.exchange(0)==8&&!searchInput.open&&!std::strcmp(pendingSearch,"B"));
    press(PS5_PAD_BUTTON_TRIANGLE);
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(!searchInput.open&&command.load()==0&&!http.cancelled);

    publishedCloud.state=CloudState::starting;
    assert(current()==Screen::launching);
    press(PS5_PAD_BUTTON_OPTIONS);assert(command.load()==0);
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(command.exchange(0)==2&&http.cancelled);
    http.cancelled=false;
    publishedSession=true;publishedCloud.state=CloudState::queued;publishedCloud.queuePosition=4;
    assert(current()==Screen::launching);
    press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_LEFT);
    assert(command.load()==0&&!detailOpen);
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(command.exchange(0)==2&&http.cancelled);
    http.cancelled=false;
    publishedCloud.state=CloudState::ready;
    assert(current()==Screen::connecting);
    publishedCloud.state=CloudState::failed;
    assert(current()==Screen::cleanupFailed);
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.load()==0);
    press(PS5_PAD_BUTTON_TRIANGLE);
    assert(!searchInput.open);
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(command.exchange(0)==2&&http.cancelled);
    http.cancelled=false;
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(command.exchange(0)==10&&http.cancelled);
    http.cancelled=false;publishedSession=false;
    assert(current()==Screen::catalogError);
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.exchange(0)==6);
    publishedCloud.state=CloudState::catalog;publishedStreamFailure=true;libraryFocus=3;
    assert(current()==Screen::streamEnded);
    press(PS5_PAD_BUTTON_RIGHT);assert(libraryFocus==3);
    press(PS5_PAD_BUTTON_CROSS);
    {
        Request retry;
        assert(takeCommand(retry)==5&&retry.index==3&&retry.profile==-1);
    }
    press(PS5_PAD_BUTTON_SQUARE);
    assert(command.exchange(0)==6);
    publishedStreamFailure=false;
    publishedStream=true;publishedSession=true;
    assert(current()==Screen::connecting);
    for(unsigned button:{PS5_PAD_BUTTON_CROSS,PS5_PAD_BUTTON_CIRCLE,PS5_PAD_BUTTON_TRIANGLE,PS5_PAD_BUTTON_SQUARE,PS5_PAD_BUTTON_L1,PS5_PAD_BUTTON_R1,PS5_PAD_BUTTON_OPTIONS,PS5_PAD_BUTTON_UP}) {
        press(button);
        assert(command.load()==0&&!http.cancelled&&!searchInput.open&&!detailOpen);
    }
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(command.exchange(0)==2&&http.cancelled);
    http.cancelled=false;publishedStream=false;publishedSession=false;
    published.state=State::failed;publishedSession=true;
    assert(current()==Screen::signIn);
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(command.exchange(0)==10&&http.cancelled);
    http.cancelled=false;publishedSession=false;
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.exchange(0)==1);
    published.state=State::waiting;
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.load()==0);
    published.state=State::authenticated;
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(command.exchange(0)==10&&http.cancelled);
    http.cancelled=false;testButtons=0;draw(canvas);
    activeHttp=nullptr;
    std::puts("Native draw controller scenarios passed");
    return true;
}
}

int main() {ps5::demo::run(scenarios,"Native controller checks");}
