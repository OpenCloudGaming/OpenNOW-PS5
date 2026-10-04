// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../demo_renderer.hpp"
#include <cstddef>
#include <string_view>

namespace opennow::ui {
enum class Face { semibold, bold, extrabold, black, mono, count };

bool loadFonts() noexcept;
bool fontsLoaded() noexcept;
char32_t nextCodepoint(std::string_view text, std::size_t& at) noexcept;
float textWidth(Face face, float size, std::string_view text, float tracking = 0) noexcept;
float ascent(Face face, float size) noexcept;
float descent(Face face, float size) noexcept;
float drawText(ps5::demo::Canvas& canvas, Face face, float size, float x, float baseline,
               std::string_view text, ps5::demo::Color color, float tracking = 0,
               unsigned alpha = 255) noexcept;
std::size_t fittingPrefix(Face face, float size, std::string_view text, float maxWidth,
                          float tracking = 0) noexcept;
float drawFitted(ps5::demo::Canvas& canvas, Face face, float size, float x, float baseline,
                 std::string_view text, float maxWidth, ps5::demo::Color color,
                 float tracking = 0, unsigned alpha = 255) noexcept;
unsigned drawWrapped(ps5::demo::Canvas& canvas, Face face, float size, float x, float top,
                     float lineHeight, std::string_view text, float maxWidth, unsigned maxLines,
                     ps5::demo::Color color, unsigned alpha = 255, bool draw = true) noexcept;
}
