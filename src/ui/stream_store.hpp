// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../settings_file.hpp"
#include "tv_ui.hpp"
#include <optional>

namespace opennow::ui {
struct StreamStore {
    using Available=bool (*)(const StreamSettings&) noexcept;
    settingsFile::Saved saved;
    StreamSettings best, defaults;
    SettingsInfo info;
    bool artworkSaved=true;
    void load(const char* path,const StreamSettings& fallback,bool artwork,Available available) noexcept {
        best=fallback;info={};
        const auto status=settingsFile::load(path,saved);
        info.loadCorrupt=status==settingsFile::Status::corrupt;
        info.loadUnreadable=status==settingsFile::Status::unreadable;
        if(status!=settingsFile::Status::loaded)saved={false,best,artwork};
        info.saved=saved.hasSettings;
        info.savedUnavailable=saved.hasSettings&&!available(saved.settings);
        defaults=saved.hasSettings&&!info.savedUnavailable?saved.settings:best;
        artworkSaved=saved.artworkCache;
        info.revision=1;
    }
    bool accept(const StreamSettings& s,Available available) const noexcept {return validateSettings(s)==SettingsError::none&&available(s);}
    void save(const char* path,const StreamSettings& s,Available available) noexcept {
        if(!accept(s,available))return;
        const settingsFile::Saved next{true,s,saved.artworkCache};
        info.saveError=settingsFile::save(path,next);
        if(info.saveError)return;
        saved=next;defaults=s;++info.revision;
        info.saved=true;info.savedUnavailable=info.loadCorrupt=info.loadUnreadable=false;
    }
    void reset(const char* path) noexcept {
        const settingsFile::Saved next{false,best,saved.artworkCache};
        info.saveError=settingsFile::save(path,next);
        if(info.saveError)return;
        saved=next;defaults=best;++info.revision;
        info.saved=info.savedUnavailable=info.loadCorrupt=info.loadUnreadable=false;
    }
    void artwork(const char* path,bool wanted) noexcept {
        if(wanted==artworkSaved)return;
        artworkSaved=wanted;
        auto next=saved;next.artworkCache=wanted;
        info.cacheSaveError=settingsFile::save(path,next);
        if(!info.cacheSaveError)saved=next;
    }
    std::optional<StreamSettings> launch(bool requested,const StreamSettings& s,Available available) const noexcept {
        if(!requested)return defaults;
        if(!accept(s,available))return std::nullopt;
        return s;
    }
};
}
