// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../demo_renderer.hpp"
#include "../gfn.hpp"
#include "../cloud.hpp"
#include "../catalog_search.hpp"
#include "../stream/stream_settings.hpp"
#include "../input/stream_keyboard.hpp"
#include "artwork.hpp"

namespace opennow::ui {
enum class Section { library, browse, settings };
enum class Screen {
    signIn, catalogLoading, catalogEmpty, catalogError, grid, search, detail, settings,
    launching, connecting, cleanupFailed, streamEnded, launchFailed, keyboard
};
enum class SettingsPane { stream, display, account, about };
constexpr unsigned gridColumns=6;
constexpr unsigned settingsPanes=4;

struct Inputs {
    const View& login;
    const CloudView& session;
    const CloudView& library;
    Section section;
    bool streaming, sessionOwned, streamFailed, searchOpen, detailOpen;
};
Screen screenFor(const Inputs&) noexcept;
const CloudView& catalogFor(Section section,const CloudView& session,const CloudView& library) noexcept;
unsigned settingsRows(SettingsPane pane) noexcept;

unsigned availableProfiles(unsigned mask, StreamProfile* out, unsigned capacity) noexcept;
constexpr unsigned profileBit(StreamProfile profile) {return 1U<<static_cast<unsigned>(profile);}
unsigned storeSiblings(const CloudView& cloud, unsigned index, unsigned* out, unsigned capacity) noexcept;

struct SettingsInfo {
    bool saved=false;
    bool savedUnavailable=false;
    bool loadCorrupt=false;
    bool loadUnreadable=false;
    int saveError=0;
};

struct Model {
    Screen screen;
    Section section;
    const View& login;
    const CloudView& session;
    const CloudView& library;
    StreamProfile profile;
    StreamProfile launchProfile;
    unsigned profileMask;
    unsigned focus;
    const CatalogSearch& search;
    const char* searchText;
    const char* output;
    bool streaming;
    SettingsPane pane;
    bool settingsContent;
    unsigned settingsRow;
    bool confirmSignOut;
    SettingsInfo settings;
    art::Cache* art;
    unsigned frame;
    const StreamKeyboard& keyboard;
    bool inputReady;
};
void render(ps5::demo::Canvas& canvas, const Model& model) noexcept;
}
