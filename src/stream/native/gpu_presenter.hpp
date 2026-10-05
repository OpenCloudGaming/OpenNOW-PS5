// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../stream_settings.hpp"
#include "../hardware_video_contract.hpp"
#include "../../demo_renderer.hpp"
namespace opennow::gpu {
// All GL and VideoOut operations belong to the main presentation thread.
bool initialize() noexcept;
bool available() noexcept;
void shutdown() noexcept;
bool profileAvailable(StreamProfile) noexcept;
StreamProfile bestProfile() noexcept;
bool settingsAvailable(const StreamSettings&) noexcept;
StreamSettings bestSettings() noexcept;
std::optional<video::NativeMode> allocationFor(const StreamSettings&) noexcept;
bool drawVideo(const video::NativeSurface&,const video::NativeMode&) noexcept;
void drawInterface(const std::uint32_t*) noexcept;
bool swap() noexcept;
void setOverlay(bool active,bool changed) noexcept;
bool overlayPending() noexcept;
bool hasRetainedVideo() noexcept;
void invalidateVideo() noexcept;
bool present(const std::uint32_t* pixels) noexcept;
const char* outputLabel() noexcept;
}
