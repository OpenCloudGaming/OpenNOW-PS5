// SPDX-License-Identifier: GPL-3.0-or-later
#define main preview_entry
#include "../src/main.cpp"
#undef main
#include "../src/ui/font.hpp"
#include "../src/ui/artwork_disk.hpp"
#include <cassert>
#include <filesystem>
#include <vector>

namespace { unsigned testButtons=0; bool testConnected=true; PS5_PadTouchData testTouch{}; }
extern "C" {
int sceUserServiceGetInitialUser(int* user) {*user=0;return 0;}
int scePadOpen(int,int,int,void*) {return 1;}
int scePadReadState(int,PS5_PadData* data) {data->connected=testConnected;data->buttons=testButtons;data->touch=testTouch;return 0;}
}
opennow::Http::Http() noexcept=default;
opennow::Http::~Http()=default;
namespace { bool testHardware=false; }
namespace opennow::gpu {
bool settingsAvailable(const StreamSettings& settings) noexcept {
    return validateSettings(settings)==SettingsError::none&&(!settings.hardware()||(testHardware&&!(settings.hdr()&&settings.fps>90)));
}
StreamSettings bestSettings() noexcept {return testHardware?settingsFor(StreamProfile::native_hdr90):settingsFor(StreamProfile::quality);}
}

namespace {
void game(opennow::CloudView& view,unsigned i,const char* title,const char* store,bool owned) {
    auto& g=view.games[i];
    std::snprintf(g.id,sizeof(g.id),"%u",10+i+(view.page*100));
    std::snprintf(g.title,sizeof(g.title),"%s",title);
    std::snprintf(g.store,sizeof(g.store),"%s",store);
    g.owned=owned;
}
Screen current() {
    return opennow::ui::screenFor({published,publishedCloud,publishedLibrary,section,publishedStream,publishedSession,publishedStreamFailure,searchInput.open,detailOpen});
}
int take(Request& request) {return takeCommand(request);}
bool noFetch(void*,const char*,unsigned char*,std::size_t,std::size_t& size,const std::atomic_bool&) noexcept {size=0;return false;}
int take() {Request request;return takeCommand(request);}
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

void writeV1(const char* path,StreamProfile profile) {
    unsigned char record[16]{'O','N','S','T'};
    opennow::settingsFile::write32(record+4,1);
    opennow::settingsFile::write32(record+8,static_cast<std::uint32_t>(profile));
    opennow::settingsFile::write32(record+12,opennow::settingsFile::checksum(record,12));
    FILE* file=std::fopen(path,"wb");
    assert(file&&std::fwrite(record,1,sizeof(record),file)==sizeof(record));
    std::fclose(file);
}
void storeChecks() {
    using opennow::StreamSettings;
    const char* path="build/host-tests/ui-store-settings.bin";
    std::remove(path);
    const auto available=opennow::gpu::settingsAvailable;
    const auto quality=opennow::settingsFor(StreamProfile::quality),smooth=opennow::settingsFor(StreamProfile::smooth);
    opennow::ui::StreamStore store;
    opennow::settingsFile::Saved file;
    store.load(path,quality,false,available);
    assert(!store.info.saved&&store.defaults==quality&&!store.artworkSaved&&store.info.revision==1);
    store.artwork(path,true);
    assert(opennow::settingsFile::load(path,file)==opennow::settingsFile::Status::loaded);
    assert(!file.hasSettings&&file.artworkCache&&file.settings==quality&&store.info.revision==1);
    const StreamSettings custom{1600,900,45,33000};
    store.save(path,custom,available);
    assert(store.defaults==custom&&store.info.revision==2&&store.info.saved);
    assert(opennow::settingsFile::load(path,file)==opennow::settingsFile::Status::loaded&&file.hasSettings&&file.settings==custom&&file.artworkCache);
    StreamSettings invalid=custom;invalid.width=1601;
    store.save(path,invalid,available);
    StreamSettings hardware=custom;hardware.mode=opennow::VideoMode::h264Hardware;hardware.network=opennow::NetworkPolicy::fixed;
    store.save(path,hardware,available);
    assert(store.defaults==custom&&store.info.revision==2);
    store.artwork(path,false);
    assert(opennow::settingsFile::load(path,file)==opennow::settingsFile::Status::loaded&&file.settings==custom&&file.hasSettings&&!file.artworkCache);
    assert(store.info.revision==2&&!store.info.cacheSaveError);
    assert(*store.launch(true,smooth,available)==smooth&&*store.launch(false,smooth,available)==custom);
    assert(!store.launch(true,invalid,available)&&!store.launch(true,hardware,available)&&store.defaults==custom);
    const char* broken="build/host-tests/missing-ui-store-directory/settings.bin";
    store.reset(broken);
    assert(store.info.saveError&&store.defaults==custom&&store.info.revision==2&&store.info.saved);
    assert(store.saved.hasSettings&&store.saved.settings==custom);
    store.save(broken,smooth,available);
    assert(store.info.saveError&&store.defaults==custom&&store.info.revision==2);
    store.reset(path);
    assert(!store.info.saveError);
    assert(store.defaults==quality&&store.info.revision==3&&!store.info.saved);
    assert(opennow::settingsFile::load(path,file)==opennow::settingsFile::Status::loaded&&!file.hasSettings&&!file.artworkCache);
    writeV1(path,StreamProfile::smooth);
    store.load(path,quality,true,available);
    assert(store.info.saved&&!store.info.savedUnavailable&&store.defaults==smooth&&store.artworkSaved);
    writeV1(path,StreamProfile::native_hdr120);
    store.load(path,quality,true,available);
    assert(store.info.saved&&store.info.savedUnavailable&&store.defaults==quality);
    store.save(path,smooth,available);
    assert(!store.info.savedUnavailable&&store.defaults==smooth);
    std::remove(path);
}

bool scenarios(ps5::demo::Canvas& canvas) noexcept {
    textChecks(canvas);
    opennow::Http http;
    activeHttp=&http;
    published.state=State::authenticated;published.sessionSaved=true;
    publishedDefaults=opennow::gpu::bestSettings();publishedSettings.revision=1;
    const auto press=[&](unsigned buttons){testButtons=0;draw(canvas);testButtons=buttons;draw(canvas);};

    publishedLibrary.state=CloudState::loading;publishedCloud.state=CloudState::catalog;
    for(unsigned i=0;i<14;++i)game(publishedCloud,i,"Browse Game","STEAM",i==3);
    publishedCloud.count=14;publishedCloud.hasNext=true;publishedCloud.revision=1;
    testButtons=0;draw(canvas);
    assert(section==Section::library&&current()==Screen::catalogLoading);
    press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_OPTIONS);
    assert(command.load()==0&&!detailOpen&&!http.cancelled);

    publishedLibrary.state=CloudState::failed;std::snprintf(publishedLibrary.message,sizeof(publishedLibrary.message),"Library request failed (HTTP 503)");
    assert(current()==Screen::catalogError);
    {
        Request request;
        press(PS5_PAD_BUTTON_CROSS);
        assert(take(request)==12&&request.index==0);
    }
    publishedLibrary.state=CloudState::catalog;publishedLibrary.revision=1;publishedLibrary.hasNext=true;publishedLibrary.page=12;
    assert(current()==Screen::catalogEmpty);
    {
        Request request;
        press(PS5_PAD_BUTTON_CROSS);
        assert(take(request)==12&&request.index==13&&request.direction==1&&section==Section::library);
    }
    publishedLibrary.hasNext=false;publishedLibrary.page=0;
    assert(current()==Screen::catalogEmpty);
    press(PS5_PAD_BUTTON_CROSS);
    assert(section==Section::browse&&current()==Screen::grid&&command.load()==0);
    press(PS5_PAD_BUTTON_L1);
    assert(section==Section::library&&current()==Screen::catalogEmpty);

    const char* titles[][2]={{"Alpha","STEAM"},{"Beta","XBOX"},{"Dungeons II","XBOX"},{"Delta","EPIC"},{"Echo","GOG"},{"Foxtrot","UBISOFT"},{"Golf","STEAM"},{"Dungeons II","STEAM"}};
    for(unsigned i=0;i<8;++i)game(publishedLibrary,i,titles[i][0],titles[i][1],true);
    publishedLibrary.count=8;publishedLibrary.revision=2;
    testButtons=0;draw(canvas);
    assert(browsers[0].focus==0&&current()==Screen::grid);
    press(PS5_PAD_BUTTON_LEFT);assert(browsers[0].focus==0&&command.load()==0);
    press(PS5_PAD_BUTTON_UP);assert(browsers[0].focus==0&&command.load()==0);
    press(PS5_PAD_BUTTON_RIGHT);assert(browsers[0].focus==1);
    press(PS5_PAD_BUTTON_DOWN);assert(browsers[0].focus==7);
    press(PS5_PAD_BUTTON_DOWN);assert(browsers[0].focus==7&&command.load()==0);
    press(PS5_PAD_BUTTON_RIGHT);assert(browsers[0].focus==7&&command.load()==0);
    press(PS5_PAD_BUTTON_UP);assert(browsers[0].focus==1);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);
    assert(browsers[0].focus==4);
    press(PS5_PAD_BUTTON_DOWN);assert(browsers[0].focus==7);
    press(PS5_PAD_BUTTON_LEFT);assert(browsers[0].focus==6);
    press(PS5_PAD_BUTTON_UP);assert(browsers[0].focus==0);
    assert(command.load()==0&&!http.cancelled);

    press(PS5_PAD_BUTTON_L1|PS5_PAD_BUTTON_R1);
    assert(command.load()==0&&!http.cancelled);
    section=Section::library;testButtons=0;draw(canvas);

    {
        Request request;
        press(PS5_PAD_BUTTON_SQUARE);
        assert(take(request)==12&&request.index==0);
    }

    press(PS5_PAD_BUTTON_R1);
    assert(section==Section::browse&&browsers[1].focus==0&&browsers[0].focus==0&&current()==Screen::grid);
    for(unsigned i=0;i<13;++i)press(PS5_PAD_BUTTON_RIGHT);
    assert(browsers[1].focus==13&&command.load()==0);
    {
        Request request;
        press(PS5_PAD_BUTTON_RIGHT);
        assert(take(request)==7&&request.index==1&&browsers[1].target==1&&browsers[1].pending==1);
    }
    publishedLibrary.state=CloudState::loading;
    section=Section::library;
    assert(current()==Screen::grid);
    publishedLibrary.count=0;
    assert(current()==Screen::catalogLoading);
    publishedLibrary.count=8;publishedLibrary.state=CloudState::catalog;
    section=Section::browse;
    publishedCloud.state=CloudState::loading;
    testButtons=0;draw(canvas);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_CROSS);
    assert(command.load()==0&&!detailOpen&&browsers[1].focus==13);
    publishedCloud.state=CloudState::failed;
    std::snprintf(publishedCloud.message,sizeof(publishedCloud.message),"Catalog request failed (HTTP 502)");
    assert(current()==Screen::grid);
    {
        Request request;
        press(PS5_PAD_BUTTON_CROSS);
        assert(take(request)==7&&request.index==1&&!detailOpen);
    }
    publishedCloud.state=CloudState::catalog;publishedCloud.page=1;publishedCloud.hasNext=false;publishedCloud.count=5;
    for(unsigned i=0;i<5;++i)game(publishedCloud,i,"Second Page","EPIC",false);
    publishedCloud.revision=2;
    testButtons=0;draw(canvas);
    assert(browsers[1].focus==0&&browsers[1].target==0);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);
    assert(browsers[1].focus==4);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_DOWN);
    assert(command.load()==0);
    press(PS5_PAD_BUTTON_UP);
    {
        Request request;
        assert(take(request)==7&&request.index==0&&request.direction==-1&&browsers[1].target==2);
    }
    publishedCloud.page=0;publishedCloud.hasNext=true;publishedCloud.count=14;
    for(unsigned i=0;i<14;++i)game(publishedCloud,i,"Browse Game","STEAM",i==3);
    publishedCloud.revision=3;
    testButtons=0;draw(canvas);
    assert(browsers[1].focus==13);
    for(unsigned i=0;i<10;++i)press(PS5_PAD_BUTTON_LEFT);
    assert(browsers[1].focus==3);
    {
        Request request;
        press(PS5_PAD_BUTTON_SQUARE);
        assert(take(request)==7&&request.index==0&&browsers[1].target==0);
        publishedCloud.revision=4;std::swap(publishedCloud.games[3],publishedCloud.games[9]);
        testButtons=0;draw(canvas);
        assert(browsers[1].focus==9&&!std::strcmp(publishedCloud.games[browsers[1].focus].id,"13"));
    }

    press(PS5_PAD_BUTTON_L1);
    assert(section==Section::library&&browsers[0].focus==0);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_RIGHT);
    press(PS5_PAD_BUTTON_CROSS);
    assert(detailOpen&&current()==Screen::detail&&detailSection==Section::library&&command.load()==0);
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(!detailOpen&&command.load()==0&&!http.cancelled&&current()==Screen::grid);
    press(PS5_PAD_BUTTON_CROSS);
    press(PS5_PAD_BUTTON_OPTIONS);press(PS5_PAD_BUTTON_SQUARE);press(PS5_PAD_BUTTON_R1);press(PS5_PAD_BUTTON_TRIANGLE);
    assert(command.load()==0&&detailOpen&&section==Section::library&&!searchInput.open);
    press(PS5_PAD_BUTTON_LEFT);assert(browsers[0].focus==2);
    press(PS5_PAD_BUTTON_RIGHT);assert(browsers[0].focus==7);
    press(PS5_PAD_BUTTON_RIGHT);assert(browsers[0].focus==7);
    press(PS5_PAD_BUTTON_UP);assert(detailChoice==0);
    press(PS5_PAD_BUTTON_DOWN);assert(detailChoice==1&&command.load()==0);
    press(PS5_PAD_BUTTON_L1);assert(detailChoice==2&&command.load()==0);
    {
        Request launched;
        press(PS5_PAD_BUTTON_CROSS);
        assert(take(launched)==5&&launched.index==7&&launched.source==1&&launched.hasSettings);
        assert(launched.settings==opennow::settingsFor(StreamProfile::smooth));
        assert(!detailOpen&&publishedDefaults==opennow::settingsFor(StreamProfile::quality)&&draft==publishedDefaults);
        press(PS5_PAD_BUTTON_CROSS);
        assert(detailChoice==0);
        press(PS5_PAD_BUTTON_DOWN);
        assert(launched.settings==opennow::settingsFor(StreamProfile::smooth)&&lastLaunch.settings==launched.settings);
        assert(command.load()==0&&detailChoice==1);
        submit(5,{1,0,1,true,opennow::settingsFor(StreamProfile::smooth)});
        submit(16,{-1,-1,1,true,opennow::settingsFor(StreamProfile::compatibility)});
        Request next;
        assert(take(next)==16&&next.index==-1&&next.hasSettings&&next.settings==opennow::settingsFor(StreamProfile::compatibility));
        assert(take(next)==0&&next.index==-1&&!next.hasSettings&&next.source==-1);
        submit(5,{4,1,1,true,opennow::settingsFor(StreamProfile::smooth)});
        command.store(10);
        assert(take(next)==10);
        press(PS5_PAD_BUTTON_CIRCLE);
        assert(!detailOpen&&command.load()==0);
    }
    storeChecks();
    {
        const auto before=publishedCloud;
        opennow::StreamSettings unqualified=opennow::settingsFor(StreamProfile::native_4k120);
        rejectLaunch(publishedCloud,opennow::ui::settingsProblem(unqualified,opennow::gpu::settingsAvailable(unqualified)),publishedLibrary.games[7]);
        assert(current()==Screen::launchFailed&&publishedCloud.launchError&&publishedCloud.state==CloudState::catalog);
        assert(!std::strcmp(publishedCloud.current.title,publishedLibrary.games[7].title)&&std::strstr(publishedCloud.message,"Not qualified"));
        testButtons=0;draw(canvas);
        press(PS5_PAD_BUTTON_CIRCLE);assert(take()==15);
        publishedCloud=before;
    }

    press(PS5_PAD_BUTTON_TRIANGLE);
    assert(searchInput.open&&section==Section::browse&&current()==Screen::search);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_CROSS);
    assert(!std::strcmp(searchInput.text,"B"));
    press(PS5_PAD_BUTTON_SQUARE);assert(!*searchInput.text);
    press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_OPTIONS);
    assert(take()==8&&!searchInput.open&&!std::strcmp(pendingSearch,"B")&&browsers[1].target==1);
    press(PS5_PAD_BUTTON_TRIANGLE);
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(!searchInput.open&&command.load()==0&&!http.cancelled);

    press(PS5_PAD_BUTTON_R1);
    assert(section==Section::settings&&current()==Screen::settings&&!settingsContent);
    press(PS5_PAD_BUTTON_R1);assert(section==Section::settings);
    press(PS5_PAD_BUTTON_DOWN);assert(settingsPane==SettingsPane::display);
    press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_CROSS);assert(!settingsContent);
    press(PS5_PAD_BUTTON_UP);press(PS5_PAD_BUTTON_UP);assert(settingsPane==SettingsPane::stream);
    press(PS5_PAD_BUTTON_RIGHT);assert(settingsContent&&settingsRow==0);
    {
        using opennow::ui::StreamRow;
        using opennow::StreamSettings;
        const auto row=[&](StreamRow target){while(settingsRow>static_cast<unsigned>(target))press(PS5_PAD_BUTTON_UP);while(settingsRow<static_cast<unsigned>(target))press(PS5_PAD_BUTTON_DOWN);};
        const auto ack=[&](const StreamSettings& saved){publishedDefaults=saved;++publishedSettings.revision;testButtons=0;draw(canvas);};
        Request request;
        const auto quality=opennow::settingsFor(StreamProfile::quality),smooth=opennow::settingsFor(StreamProfile::smooth);
        assert(draft==quality);
        press(PS5_PAD_BUTTON_RIGHT);
        assert(draft==smooth&&command.load()==0&&publishedDefaults==quality);
        press(PS5_PAD_BUTTON_CIRCLE);assert(!settingsContent);
        press(PS5_PAD_BUTTON_L1);assert(section==Section::browse);
        press(PS5_PAD_BUTTON_R1);assert(section==Section::settings&&draft==smooth);
        press(PS5_PAD_BUTTON_RIGHT);assert(settingsContent&&settingsRow==0);
        press(PS5_PAD_BUTTON_OPTIONS);
        assert(take(request)==16&&request.hasSettings&&request.settings==smooth);
        ack(smooth);
        assert(draft==smooth&&!opennow::ui::changedRows(draft,publishedDefaults));
        press(PS5_PAD_BUTTON_OPTIONS);assert(command.load()==0);

        row(StreamRow::fps);
        press(PS5_PAD_BUTTON_RIGHT);assert(draft.fps==60);
        press(PS5_PAD_BUTTON_LEFT);assert(draft.fps==59);
        press(PS5_PAD_BUTTON_L1);assert(draft.fps==30);
        press(PS5_PAD_BUTTON_R1);press(PS5_PAD_BUTTON_R1);assert(draft.fps==60);
        press(PS5_PAD_BUTTON_LEFT);
        press(PS5_PAD_BUTTON_OPTIONS);
        assert(take(request)==16&&request.settings.fps==59);
        publishedSettings.saveError=5;testButtons=0;draw(canvas);
        assert(draft.fps==59&&publishedDefaults==smooth);
        publishedSettings.saveError=0;
        press(PS5_PAD_BUTTON_SQUARE);assert(draft==smooth);

        row(StreamRow::bitrate);
        press(PS5_PAD_BUTTON_RIGHT);assert(draft.bitrate_kbps==21000);
        press(PS5_PAD_BUTTON_R1);assert(draft.bitrate_kbps==25000);
        press(PS5_PAD_BUTTON_CROSS);assert(numberEdit.open()&&numberEdit.row==StreamRow::bitrate);
        press(PS5_PAD_BUTTON_LEFT);press(PS5_PAD_BUTTON_LEFT);press(PS5_PAD_BUTTON_UP);
        press(PS5_PAD_BUTTON_CROSS);assert(numberEdit.open()&&draft.bitrate_kbps==25000);
        press(PS5_PAD_BUTTON_CIRCLE);assert(!numberEdit.open()&&settingsContent&&draft.bitrate_kbps==25000);
        press(PS5_PAD_BUTTON_SQUARE);

        row(StreamRow::resolution);
        press(PS5_PAD_BUTTON_CROSS);assert(numberEdit.open()&&numberEdit.cursor==7&&!std::strcmp(numberEdit.digits,"12800720"));
        press(PS5_PAD_BUTTON_UP);assert(!std::strcmp(numberEdit.digits,"12800721"));
        press(PS5_PAD_BUTTON_CROSS);assert(numberEdit.open()&&draft==smooth);
        press(PS5_PAD_BUTTON_R1);assert(!std::strcmp(numberEdit.digits,"16000900"));
        press(PS5_PAD_BUTTON_R1);press(PS5_PAD_BUTTON_R1);assert(!std::strcmp(numberEdit.digits,"19201080"));
        press(PS5_PAD_BUTTON_L1);press(PS5_PAD_BUTTON_CROSS);
        assert(!numberEdit.open()&&draft.width==1600&&draft.height==900);
        press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_LEFT);press(PS5_PAD_BUTTON_LEFT);press(PS5_PAD_BUTTON_LEFT);
        press(PS5_PAD_BUTTON_UP);assert(numberEdit.cursor==4&&!std::strcmp(numberEdit.digits,"16001900"));
        press(PS5_PAD_BUTTON_CROSS);assert(numberEdit.open());
        press(PS5_PAD_BUTTON_L1);assert(numberEdit.open());
        press(PS5_PAD_BUTTON_CIRCLE);
        assert(draft.width==1600&&draft.height==900);
        draft.width=1601;
        press(PS5_PAD_BUTTON_OPTIONS);assert(command.load()==0);
        draft.width=1600;draft.mode=opennow::VideoMode::h264Hardware;draft.network=opennow::NetworkPolicy::fixed;
        press(PS5_PAD_BUTTON_OPTIONS);assert(command.load()==0);
        press(PS5_PAD_BUTTON_SQUARE);assert(draft==smooth);

        row(StreamRow::decoding);
        press(PS5_PAD_BUTTON_LEFT);assert(!draft.hardware()&&!confirmFixed);
        testHardware=true;
        const auto experimental=opennow::settingsFor(StreamProfile::experimental);
        draft=experimental;
        press(PS5_PAD_BUTTON_LEFT);
        assert(confirmFixed&&draft==experimental&&proposal.mode==opennow::VideoMode::h264Hardware&&proposal.network==opennow::NetworkPolicy::fixed);
        press(PS5_PAD_BUTTON_OPTIONS);press(PS5_PAD_BUTTON_SQUARE);press(PS5_PAD_BUTTON_DOWN);
        assert(confirmFixed&&draft==experimental&&command.load()==0&&settingsRow==static_cast<unsigned>(StreamRow::decoding));
        press(PS5_PAD_BUTTON_CIRCLE);
        assert(!confirmFixed&&draft==experimental&&settingsContent);
        press(PS5_PAD_BUTTON_LEFT);press(PS5_PAD_BUTTON_CROSS);
        assert(!confirmFixed&&draft.mode==opennow::VideoMode::h264Hardware&&draft.network==opennow::NetworkPolicy::fixed);
        assert(draft.width==1920&&draft.height==1080&&draft.fps==60&&draft.bitrate_kbps==75000);
        press(PS5_PAD_BUTTON_RIGHT);assert(!draft.hardware()&&adjusted==0);
        press(PS5_PAD_BUTTON_LEFT);assert(!confirmFixed&&draft.mode==opennow::VideoMode::h264Hardware);
        row(StreamRow::codec);
        press(PS5_PAD_BUTTON_RIGHT);assert(draft.mode==opennow::VideoMode::hevcMain10SdrHardware);
        row(StreamRow::hdr);
        press(PS5_PAD_BUTTON_RIGHT);assert(draft.mode==opennow::VideoMode::hevcMain10HdrHardware);
        draft.network=opennow::NetworkPolicy::adaptive;
        row(StreamRow::codec);
        press(PS5_PAD_BUTTON_LEFT);assert(confirmFixed&&draft.hdr());
        press(PS5_PAD_BUTTON_CIRCLE);assert(draft.hdr()&&draft.network==opennow::NetworkPolicy::adaptive);
        row(StreamRow::decoding);
        press(PS5_PAD_BUTTON_RIGHT);
        assert(draft.mode==opennow::VideoMode::h264Software&&adjusted==2);
        draft=opennow::settingsFor(StreamProfile::native_4k120);
        press(PS5_PAD_BUTTON_RIGHT);
        assert(draft.mode==opennow::VideoMode::h264Software&&draft.width==1920&&draft.height==1080&&draft.fps==60&&adjusted==2);
        assert(publishedDefaults==smooth&&command.load()==0);
        draft=opennow::settingsFor(StreamProfile::native_hdr120);
        press(PS5_PAD_BUTTON_OPTIONS);assert(command.load()==0);
        draft.fps=90;
        press(PS5_PAD_BUTTON_OPTIONS);
        assert(take(request)==16&&request.settings==opennow::settingsFor(StreamProfile::native_hdr90));
        press(PS5_PAD_BUTTON_SQUARE);
        testHardware=false;

        row(StreamRow::reset);
        press(PS5_PAD_BUTTON_CROSS);assert(take()==17);
        ack(quality);
        assert(draft==quality);
    }
    press(PS5_PAD_BUTTON_L1);assert(section==Section::settings);
    press(PS5_PAD_BUTTON_CIRCLE);assert(!settingsContent&&command.load()==0&&!http.cancelled);
    {
        static opennow::art::Cache cacheArt(noFetch,nullptr);
        artCache=&cacheArt;
        draft.fps=55;
        const unsigned revision=publishedSettings.revision;
        press(PS5_PAD_BUTTON_DOWN);press(PS5_PAD_BUTTON_DOWN);assert(settingsPane==SettingsPane::cache);
        press(PS5_PAD_BUTTON_RIGHT);assert(settingsContent&&settingsRow==0);
        press(PS5_PAD_BUTTON_LEFT);assert(!artworkWanted&&settingsContent);
        press(PS5_PAD_BUTTON_CROSS);assert(artworkWanted);
        press(PS5_PAD_BUTTON_LEFT);assert(!artworkWanted);
        assert(command.load()==0&&draft.fps==55&&publishedSettings.revision==revision);
        press(PS5_PAD_BUTTON_DOWN);press(PS5_PAD_BUTTON_CROSS);assert(confirmClear);
        press(PS5_PAD_BUTTON_CIRCLE);assert(!confirmClear&&settingsContent);
        confirmClear=true;press(PS5_PAD_BUTTON_CROSS);assert(!confirmClear&&settingsContent&&command.load()==0);
        press(PS5_PAD_BUTTON_UP);press(PS5_PAD_BUTTON_RIGHT);assert(artworkWanted);
        press(PS5_PAD_BUTTON_CIRCLE);assert(!settingsContent);
        artCache=nullptr;
        press(PS5_PAD_BUTTON_UP);press(PS5_PAD_BUTTON_UP);
        press(PS5_PAD_BUTTON_RIGHT);assert(draft.fps==55);
        press(PS5_PAD_BUTTON_SQUARE);assert(draft==opennow::settingsFor(StreamProfile::quality));
        press(PS5_PAD_BUTTON_CIRCLE);
    }
    {
        namespace fs=std::filesystem;
        const fs::path parent="build/host-tests/ui-cache-parent",root=parent/"artwork",sentinel=parent/"settings.bin";
        fs::remove_all(parent);fs::create_directories(root);
        {std::FILE* file=std::fopen(sentinel.c_str(),"wb");assert(file&&std::fputs("keep",file)>=0);std::fclose(file);}
        {
            opennow::art::DiskCache disk(root.c_str());
            std::vector<unsigned char> scratch(opennow::art::DiskCache::slotBytes),data(4096,0x5a);
            disk.initialize(scratch.data(),scratch.size());
            disk.put("https://img.nvidiagrid.net/apps/1/ZZ/GAME_BOX_ART_01_test.jpg;f=jpg;w=272",0,data.data(),data.size());
            assert(disk.available()&&disk.count()==1);
        }
        static opennow::art::Cache disabled(noFetch,nullptr,root.c_str());
        disabled.setDiskEnabled(false);
        while(disabled.step()){}
        auto stats=disabled.diskStats();
        assert(!stats.enabled&&!stats.available&&!stats.busy&&stats.count==0);
        artCache=&disabled;artworkWanted=false;
        settingsPane=SettingsPane::cache;settingsContent=true;settingsRow=1;
        press(PS5_PAD_BUTTON_CROSS);assert(confirmClear);
        press(PS5_PAD_BUTTON_CROSS);assert(!confirmClear&&settingsContent);
        while(disabled.step()){}
        stats=disabled.diskStats();
        assert(!stats.enabled&&!stats.busy&&!stats.error&&!artworkWanted);
        opennow::art::DiskCache check(root.c_str());
        std::vector<unsigned char> scratch(opennow::art::DiskCache::slotBytes);
        check.initialize(scratch.data(),scratch.size());
        assert(check.available()&&check.count()==0);
        char kept[8]{};
        {std::FILE* file=std::fopen(sentinel.c_str(),"rb");assert(file&&std::fread(kept,1,sizeof(kept),file)==4);std::fclose(file);}
        assert(!std::strcmp(kept,"keep"));
        testButtons=0;draw(canvas);
        artCache=nullptr;artworkWanted=true;
        settingsPane=SettingsPane::stream;settingsContent=false;settingsRow=0;
        testButtons=0;draw(canvas);
        fs::remove_all(parent);
    }
    press(PS5_PAD_BUTTON_DOWN);press(PS5_PAD_BUTTON_DOWN);press(PS5_PAD_BUTTON_DOWN);assert(settingsPane==SettingsPane::account);
    press(PS5_PAD_BUTTON_CROSS);assert(settingsContent&&settingsRow==0);
    press(PS5_PAD_BUTTON_CROSS);assert(confirmSignOut&&command.load()==0);
    press(PS5_PAD_BUTTON_L1);press(PS5_PAD_BUTTON_TRIANGLE);press(PS5_PAD_BUTTON_OPTIONS);
    assert(confirmSignOut&&section==Section::settings&&command.load()==0);
    press(PS5_PAD_BUTTON_CIRCLE);assert(!confirmSignOut&&settingsContent&&command.load()==0&&!http.cancelled);
    press(PS5_PAD_BUTTON_CROSS);assert(confirmSignOut);
    press(PS5_PAD_BUTTON_CROSS);
    assert(take()==11&&http.cancelled&&!confirmSignOut);
    http.cancelled=false;
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(take()==10&&http.cancelled);
    http.cancelled=false;
    section=Section::library;

    static opennow::art::Cache testArt(noFetch,nullptr);
    artCache=&testArt;
    testButtons=0;draw(canvas);
    assert(!testArt.paused());
    publishedCloud.state=CloudState::starting;
    testButtons=0;draw(canvas);
    assert(testArt.paused());
    assert(current()==Screen::launching);
    press(PS5_PAD_BUTTON_OPTIONS);press(PS5_PAD_BUTTON_L1);
    assert(command.load()==0&&section==Section::library);
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(take()==2&&http.cancelled);
    http.cancelled=false;
    publishedSession=true;publishedCloud.state=CloudState::queued;publishedCloud.queuePosition=4;
    press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_LEFT);press(PS5_PAD_BUTTON_R1);
    assert(command.load()==0&&!detailOpen&&section==Section::library);
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(take()==2&&http.cancelled);
    http.cancelled=false;
    publishedCloud.state=CloudState::ready;
    assert(current()==Screen::connecting);
    publishedCloud.state=CloudState::failed;
    assert(current()==Screen::cleanupFailed);
    press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_TRIANGLE);
    assert(command.load()==0&&!searchInput.open);
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(take()==2&&http.cancelled);
    http.cancelled=false;
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(take()==10&&http.cancelled);
    http.cancelled=false;publishedSession=false;

    publishedCloud.launchError=true;
    assert(current()==Screen::launchFailed);
    {
        Request request;
        press(PS5_PAD_BUTTON_CROSS);
        assert(take(request)==5&&request.index==7&&request.source==1);
        press(PS5_PAD_BUTTON_L1);press(PS5_PAD_BUTTON_TRIANGLE);
        assert(command.load()==0&&section==Section::library);
        press(PS5_PAD_BUTTON_CIRCLE);
        assert(take()==15&&!http.cancelled);
    }
    publishedCloud.launchError=false;publishedCloud.state=CloudState::catalog;
    publishedStreamFailure=true;
    assert(current()==Screen::streamEnded);
    {
        Request request;
        press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_L1);
        assert(command.load()==0&&section==Section::library);
        press(PS5_PAD_BUTTON_CROSS);
        assert(take(request)==5&&request.index==7&&request.source==1);
        press(PS5_PAD_BUTTON_SQUARE);
        assert(take()==15);
    }
    publishedStreamFailure=false;

    publishedStream=true;publishedSession=true;
    assert(current()==Screen::connecting);
    for(unsigned button:{PS5_PAD_BUTTON_CROSS,PS5_PAD_BUTTON_CIRCLE,PS5_PAD_BUTTON_TRIANGLE,PS5_PAD_BUTTON_SQUARE,PS5_PAD_BUTTON_L1,PS5_PAD_BUTTON_R1,PS5_PAD_BUTTON_OPTIONS,PS5_PAD_BUTTON_UP}) {
        press(button);
        assert(command.load()==0&&!http.cancelled&&!searchInput.open&&!detailOpen&&section==Section::library);
    }
    {
        using opennow::InputEvent;
        InputEvent e;
        const auto drain=[&]{while(inputQueue.take(e,true)){}};
        const auto cancelled=[&]{return inputQueue.take(e,true)&&e.kind==InputEvent::Kind::cancel;};
        drain();
        publishedInputReady=false;
        testTouch.fingers=1;testTouch.touch[0]={300,300,4,{}};
        testButtons=PS5_PAD_BUTTON_TOUCH_PAD;draw(canvas);
        testTouch.touch[0]={400,300,4,{}};draw(canvas);
        assert(!inputQueue.size()&&publishedPad.buttons==0);
        testTouch={};testButtons=0;draw(canvas);
        publishedInputReady=true;draw(canvas);
        testButtons=PS5_PAD_BUTTON_CROSS|PS5_PAD_BUTTON_TOUCH_PAD;draw(canvas);
        assert(publishedPad.connected&&publishedPad.buttons==PS5_PAD_BUTTON_CROSS);
        assert(inputQueue.take(e,false)&&e.kind==InputEvent::Kind::button&&e.button==1&&e.down&&!inputQueue.size());
        testButtons=PS5_PAD_BUTTON_CROSS;draw(canvas);
        assert(inputQueue.take(e,false)&&e.button==1&&!e.down&&!inputQueue.size());
        testTouch.fingers=2;testTouch.touch[0]={300,300,4,{}};testTouch.touch[1]={600,300,5,{}};
        testButtons=PS5_PAD_BUTTON_TOUCH_PAD;draw(canvas);
        assert(inputQueue.take(e,false)&&e.button==3&&e.down&&publishedPad.buttons==0);
        testTouch.fingers=1;testButtons=0;draw(canvas);
        assert(inputQueue.take(e,false)&&e.button==3&&!e.down&&!inputQueue.size());
        testTouch.touch[0]={360,280,4,{}};draw(canvas);
        assert(inputQueue.take(e,false)&&e.kind==InputEvent::Kind::move&&e.dx==60&&e.dy==-20);
        testTouch={};
        testButtons=PS5_PAD_BUTTON_OPTIONS;draw(canvas);
        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD;draw(canvas);
        assert(take()==2&&http.cancelled&&!inputQueue.size()&&publishedPad.buttons==PS5_PAD_BUTTON_OPTIONS);
        http.cancelled=false;

        press(PS5_PAD_BUTTON_SQUARE);assert(publishedPad.buttons==PS5_PAD_BUTTON_SQUARE);
        press(PS5_PAD_BUTTON_OPTIONS);assert(publishedPad.buttons==PS5_PAD_BUTTON_OPTIONS);
        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_SQUARE;draw(canvas);
        assert(publishedPad.buttons==PS5_PAD_BUTTON_TOUCH_PAD&&!inputQueue.size()&&command.load()==0&&!keyboard.open);
        draw(canvas);
        assert(publishedPad.buttons==PS5_PAD_BUTTON_TOUCH_PAD&&!inputQueue.size());
        testButtons=PS5_PAD_BUTTON_OPTIONS;draw(canvas);
        assert(publishedPad.buttons==0);
        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_CROSS;draw(canvas);
        assert(publishedPad.buttons==PS5_PAD_BUTTON_CROSS);
        testButtons=0;draw(canvas);
        assert(publishedPad.buttons==0&&!inputQueue.size());
        press(PS5_PAD_BUTTON_OPTIONS);assert(publishedPad.buttons==PS5_PAD_BUTTON_OPTIONS);

        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TRIANGLE;draw(canvas);
        assert(keyboard.open&&!publishedPad.connected&&publishedPad.buttons==0&&cancelled()&&!inputQueue.size());
        press(PS5_PAD_BUTTON_CROSS);
        assert(inputQueue.take(e,true)&&e.kind==InputEvent::Kind::key&&e.key.vk==0x51&&!e.key.modifiers);
        press(PS5_PAD_BUTTON_SQUARE);
        assert(inputQueue.take(e,true)&&e.key.vk==0x08);
        press(PS5_PAD_BUTTON_TRIANGLE);
        assert(inputQueue.take(e,true)&&e.key.vk==0x20);
        press(PS5_PAD_BUTTON_L1);press(PS5_PAD_BUTTON_RIGHT);press(PS5_PAD_BUTTON_CROSS);
        assert(inputQueue.take(e,true)&&e.key.vk==0x57&&e.key.modifiers==opennow::modifierShift);
        assert(command.load()==0&&!publishedPad.connected);
        testTouch.fingers=1;testTouch.touch[0]={300,300,4,{}};
        press(PS5_PAD_BUTTON_TOUCH_PAD);
        testTouch.touch[0]={500,300,4,{}};testButtons=0;draw(canvas);
        assert(!inputQueue.size()&&!publishedPad.connected&&keyboard.open);
        testTouch={};
        publishedInputReady=false;
        assert(draw(canvas)&&!draw(canvas));
        press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_SQUARE);press(PS5_PAD_BUTTON_TRIANGLE);
        assert(!inputQueue.size()&&keyboard.open);
        testButtons=0;draw(canvas);
        assert(!draw(canvas));
        publishedInputReady=true;
        assert(draw(canvas)&&!draw(canvas));
        press(PS5_PAD_BUTTON_CROSS);
        assert(inputQueue.size()==1);
        press(PS5_PAD_BUTTON_CIRCLE);
        assert(!keyboard.open&&publishedPad.connected&&publishedPad.buttons==0&&cancelled()&&!inputQueue.size());
        testButtons=PS5_PAD_BUTTON_CIRCLE|PS5_PAD_BUTTON_CROSS;draw(canvas);
        assert(publishedPad.buttons==PS5_PAD_BUTTON_CROSS);
        press(PS5_PAD_BUTTON_CIRCLE);
        assert(publishedPad.buttons==PS5_PAD_BUTTON_CIRCLE&&!keyboard.open);

        press(PS5_PAD_BUTTON_OPTIONS);
        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TRIANGLE;draw(canvas);
        assert(keyboard.open&&cancelled());
        testButtons=PS5_PAD_BUTTON_OPTIONS;draw(canvas);
        press(PS5_PAD_BUTTON_CROSS);
        testButtons=PS5_PAD_BUTTON_OPTIONS;draw(canvas);
        assert(inputQueue.size()==1);
        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TRIANGLE;draw(canvas);
        assert(!keyboard.open&&publishedPad.buttons==0&&cancelled()&&!inputQueue.size());
        testButtons=0;draw(canvas);

        press(PS5_PAD_BUTTON_OPTIONS);
        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TRIANGLE;draw(canvas);
        assert(keyboard.open&&cancelled());
        press(PS5_PAD_BUTTON_CROSS);
        assert(inputQueue.size()==1);
        testConnected=false;draw(canvas);
        assert(!keyboard.open&&!publishedPad.connected&&publishedPad.buttons==0&&cancelled()&&!inputQueue.size());
        testConnected=true;testButtons=0;draw(canvas);
        assert(!keyboard.open&&publishedPad.connected&&!inputQueue.size());

        press(PS5_PAD_BUTTON_OPTIONS);
        testButtons=PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TRIANGLE;draw(canvas);
        assert(keyboard.open&&cancelled());
        press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
        assert(take()==2&&http.cancelled&&!inputQueue.size());
        http.cancelled=false;
        testButtons=0;draw(canvas);
        assert(!draw(canvas));
        publishedStream=false;
        assert(draw(canvas)&&!keyboard.open&&cancelled());
        publishedStream=true;
        testButtons=PS5_PAD_BUTTON_TRIANGLE;draw(canvas);
        assert(!keyboard.open&&publishedPad.buttons==PS5_PAD_BUTTON_TRIANGLE);
    }
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(take()==2&&http.cancelled);
    http.cancelled=false;publishedStream=false;publishedSession=false;
    testButtons=0;draw(canvas);
    assert(!testArt.paused());
    artCache=nullptr;

    published.state=State::failed;publishedSession=true;
    assert(current()==Screen::signIn);
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(take()==10&&http.cancelled);
    http.cancelled=false;publishedSession=false;
    press(PS5_PAD_BUTTON_CROSS);
    assert(take()==1);
    published.state=State::waiting;
    press(PS5_PAD_BUTTON_CROSS);press(PS5_PAD_BUTTON_R1);
    assert(command.load()==0&&section==Section::library);
    published.state=State::authenticated;
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(take()==10&&http.cancelled);
    http.cancelled=false;testButtons=0;draw(canvas);
    activeHttp=nullptr;
    std::puts("Native draw controller scenarios passed");
    return true;
}
}

int main() {ps5::demo::run(scenarios,"Native controller checks");}
