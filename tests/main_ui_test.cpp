// SPDX-License-Identifier: GPL-3.0-or-later
#define main preview_entry
#include "../src/main.cpp"
#undef main
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
bool scenarios(ps5::demo::Canvas& canvas) noexcept {
    opennow::Http http;
    activeHttp=&http;
    published.state=State::authenticated;
    publishedCloud.state=opennow::CloudState::catalog;
    const auto press=[&](unsigned buttons){testButtons=0;draw(canvas);testButtons=buttons;draw(canvas);};
    press(PS5_PAD_BUTTON_OPTIONS);
    assert(command.load()==0&&!searchInput.open&&!http.cancelled);
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.exchange(0)==5&&!searchInput.open&&!http.cancelled);
    press(PS5_PAD_BUTTON_TRIANGLE);
    assert(searchInput.open);
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(!searchInput.open&&command.load()==0&&!http.cancelled);
    publishedCloud.state=opennow::CloudState::starting;
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(command.exchange(0)==2&&http.cancelled);
    http.cancelled=false;
    publishedSession=true;publishedCloud.state=opennow::CloudState::queued;
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(command.exchange(0)==2&&http.cancelled);
    http.cancelled=false;publishedCloud.state=opennow::CloudState::failed;
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.load()==0);
    press(PS5_PAD_BUTTON_OPTIONS|PS5_PAD_BUTTON_TOUCH_PAD);
    assert(command.exchange(0)==2&&http.cancelled);
    http.cancelled=false;publishedSession=false;
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.exchange(0)==6);
    publishedCloud.state=opennow::CloudState::catalog;publishedStreamFailure=true;
    press(PS5_PAD_BUTTON_CROSS);
    assert(command.exchange(0)==5);
    published.state=State::failed;publishedSession=true;
    press(PS5_PAD_BUTTON_CIRCLE);
    assert(command.exchange(0)==10&&http.cancelled);
    http.cancelled=false;published.state=State::authenticated;publishedSession=false;
    testButtons=0;draw(canvas);
    activeHttp=nullptr;
    std::puts("Native draw controller scenarios passed");
    return true;
}
}

int main() {ps5::demo::run(scenarios,"Native controller checks");}
