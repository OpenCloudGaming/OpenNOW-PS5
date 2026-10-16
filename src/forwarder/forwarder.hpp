// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../cloud.hpp"
#include "../ui/artwork.hpp"
#include <atomic>

namespace opennow::forwarder {
inline constexpr char targetTitleId[]="PPSA99082";
enum class Code { created, alreadyExists, removed, notFound, listed, available, unavailable,
                  invalidGame, noFreeTitleId, conflict, cancelled, artworkError, templateError,
                  ioError, stagingError, transportError, protocolError, uploadError };
struct Result {
    Code code=Code::ioError;
    int error=0;
    char titleId[10]{};
    unsigned count=0;
    bool ok() const noexcept {
        return code==Code::created||code==Code::alreadyExists||code==Code::removed||
               code==Code::notFound||code==Code::listed||code==Code::available;
    }
};
enum class Phase { checking, downloadingIcon, downloadingBackground, writing, publishing, installing, complete };
struct Progress { Phase phase; unsigned completed; unsigned total; };
using OnProgress=void(*)(void*,const Progress&) noexcept;
struct Job {
    const std::atomic_bool* cancel=nullptr;
    OnProgress progress=nullptr;
    void* context=nullptr;
};
struct Paths {
    const char* root="/data/homebrew";
    const char* templateRoot=nullptr;
    const char* const* collisionRoots=nullptr;
    unsigned collisionRootCount=0;
    const char* stageRoot=nullptr;
};
struct Options {
    Paths paths{};
    art::Fetch fetch=art::httpsFetch;
    void* fetchContext=nullptr;
};
struct Entry { char titleId[10]{}; Game game{}; };
using Visit=bool(*)(void*,const Entry&) noexcept;

bool available() noexcept;
const char* message(Code code) noexcept;
Result create(const Game& game,const Options& options={},const Job& job={}) noexcept;
Result list(Visit visit,void* context,const Paths& paths={}) noexcept;
Result remove(const char* titleId,const Paths& paths={}) noexcept;
bool validTileId(const char* titleId) noexcept;
bool paramJson(const Game& game,const char* titleId,char* out,std::size_t size) noexcept;
bool forwarderJson(const Game& game,char* out,std::size_t size) noexcept;
}
