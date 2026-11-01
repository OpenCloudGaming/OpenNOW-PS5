// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../demo_renderer.hpp"
#include "../gfn.hpp"
#include "../cloud.hpp"
#include "../catalog_search.hpp"
#include "../stream/stream_settings.hpp"
#include "../input/stream_keyboard.hpp"
#include "stream_draft.hpp"
#include "artwork.hpp"
#include "motion.hpp"

namespace opennow::ui {
enum class Section { library, browse, settings };
enum class Screen {
    signIn, catalogLoading, catalogEmpty, catalogError, grid, search, detail, settings,
    launching, connecting, cleanupFailed, streamEnded, launchFailed, keyboard
};
enum class SettingsPane { stream, display, controller, cache, account, about };
constexpr unsigned gridColumns(Section section) noexcept {return section==Section::library?7:10;}
constexpr unsigned settingsPanes=6;

struct HomeTile {
    enum class Stage { hidden, working, done };
    Stage stage=Stage::hidden;
    bool ok=false, cancelling=false;
    unsigned percent=0, revision=0;
    int error=0;
    const char* step="";
    const char* message="";
    char titleId[10]{};
    Game game{};
};

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

unsigned qualifiedPresets(StreamProfile* out, unsigned capacity, bool (*available)(const StreamSettings&)) noexcept;
unsigned storeSiblings(const CloudView& cloud, unsigned index, unsigned* out, unsigned capacity) noexcept;

struct SettingsInfo {
    bool saved=false;
    bool savedUnavailable=false;
    bool loadCorrupt=false;
    bool loadUnreadable=false;
    int saveError=0;
    int cacheSaveError=0;
    unsigned revision=0;
};

struct AudioInfo {
    unsigned capacity=2, requested=0, negotiated=0;
    bool fallback=false;
    bool operator==(const AudioInfo&) const=default;
};

struct Model {
    Screen screen;
    Section section;
    const View& login;
    const CloudView& session;
    const CloudView& library;
    const StreamSettings& defaults;
    const StreamSettings& launch;
    const StreamSettings* choices;
    unsigned choiceCount;
    unsigned choice;
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
    const StreamSettings& draft;
    StreamCaps caps;
    bool draftAvailable;
    unsigned adjusted;
    const NumberEdit& edit;
    bool editAvailable;
    art::DiskStats disk;
    bool artworkWanted;
    bool confirmClear;
    const StreamSettings& proposal;
    bool confirmFixed;
    AudioInfo audio;
    const char* detailAction;
    const HomeTile& tile;
    Motion* motion=nullptr;
};
void render(ps5::demo::Canvas& canvas, const Model& model) noexcept;
struct Overlay { const StreamKeyboard& keyboard; bool launcher; unsigned speed; bool inputReady; };
void renderOverlay(ps5::demo::Canvas& canvas, const Overlay& overlay) noexcept;
}
