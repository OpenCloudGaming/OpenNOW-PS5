// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../demo_renderer.hpp"
#include "../gfn.hpp"
#include "../cloud.hpp"
#include "../catalog_search.hpp"
#include "../stream/stream_settings.hpp"

namespace opennow::ui {
enum class Screen {
    signIn, catalogLoading, catalogEmpty, catalogError, library, search, detail,
    launching, connecting, cleanupFailed, streamEnded
};
constexpr unsigned gridColumns=6;

struct Inputs {
    const View& login;
    const CloudView& cloud;
    bool streaming, sessionOwned, streamFailed, searchOpen, detailOpen;
};
Screen screenFor(const Inputs&) noexcept;

unsigned availableProfiles(unsigned mask, StreamProfile* out, unsigned capacity) noexcept;
constexpr unsigned profileBit(StreamProfile profile) {return 1U<<static_cast<unsigned>(profile);}
unsigned storeSiblings(const CloudView& cloud, unsigned index, unsigned* out, unsigned capacity) noexcept;

struct Model {
    Screen screen;
    const View& login;
    const CloudView& cloud;
    StreamProfile profile;
    unsigned profileMask;
    unsigned focus;
    const CatalogSearch& search;
    const char* output;
    bool streaming;
};
void render(ps5::demo::Canvas& canvas, const Model& model) noexcept;
}
