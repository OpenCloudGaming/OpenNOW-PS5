/*
 * ps5-native-app-boilerplate - CPU VideoOut demonstration implementation.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides the bounded drawing surface and PS5 presentation loop used by the
 * editable starter application.
 */

#include "demo_renderer.hpp"
#ifdef OPENNOW_GPU
#include "stream/native/gpu_presenter.hpp"
#include "account_file.hpp"
#include "app_storage.h"
extern "C" unsigned long long sceKernelGetProcessTime();
#endif
#ifndef OPENNOW_HOST_PREVIEW
#include <sys/event.h>
#else
#include <ctime>
#endif

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceSystemServiceHideSplashScreen();
    int open(const char *path, int flags, ...);
    long read(int descriptor, void *buffer, std::size_t size);
    int close(int descriptor);
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
    bool ps5ObserveOwnedAllocation(const void *address) noexcept;
}

extern "C" {
int sceKernelDeleteEqueue(struct kevent*);
int sceVideoOutClose(int);
int sceKernelCreateEqueue(struct kevent**,const char*);
int sceKernelWaitEqueue(struct kevent*,struct kevent*,int,int*,unsigned*);
int sceVideoOutAddFlipEvent(struct kevent*,int,void*);
}
namespace ps5::demo
{
namespace
{
std::atomic_bool stopRequested{false};
constexpr unsigned frame_width = 1920;
constexpr unsigned frame_height = 1080;
constexpr std::size_t frame_bytes = 0x1000000;
constexpr std::size_t memory_bytes = frame_bytes * 2;
constexpr std::size_t memory_alignment = 0x200000;
constexpr int memory_type_wc_garlic = 3;
constexpr int map_protection = 0x33;
constexpr std::uint64_t pixel_format_rgba8_srgb = UINT64_C(0x8000000022000000);

struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};

struct VideoAttribute
{
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

struct Glyph
{
    char character;
    std::array<std::uint8_t, 7> rows;
};

constexpr std::array<Glyph, 75> glyphs{{
    {' ', {0, 0, 0, 0, 0, 0, 0}},        {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},     {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},   {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},   {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}}, {'G', {14, 17, 16, 23, 17, 17, 14}},
    {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {31, 4, 4, 4, 4, 4, 31}},
    {'J', {7, 2, 2, 2, 18, 18, 12}},     {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}}, {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 25, 21, 19, 17, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}}, {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},       {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},  {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},  {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'a', {0,0,14,1,15,17,15}},
    {'b', {16,16,30,17,17,17,30}},
    {'c', {0,0,15,16,16,16,15}},
    {'d', {1,1,15,17,17,17,15}},
    {'e', {0,0,14,17,31,16,14}},
    {'f', {6,8,28,8,8,8,8}},
    {'g', {0,15,17,17,15,1,14}},
    {'h', {16,16,30,17,17,17,17}},
    {'i', {4,0,12,4,4,4,14}},
    {'j', {2,0,6,2,2,18,12}},
    {'k', {16,16,18,20,24,20,18}},
    {'l', {12,4,4,4,4,4,14}},
    {'m', {0,0,26,21,21,21,21}},
    {'n', {0,0,30,17,17,17,17}},
    {'o', {0,0,14,17,17,17,14}},
    {'p', {0,0,30,17,30,16,16}},
    {'q', {0,0,15,17,15,1,1}},
    {'r', {0,0,22,25,16,16,16}},
    {'s', {0,0,15,16,14,1,30}},
    {'t', {8,8,28,8,8,9,6}},
    {'u', {0,0,17,17,17,19,13}},
    {'v', {0,0,17,17,17,10,4}},
    {'w', {0,0,17,17,21,21,10}},
    {'x', {0,0,17,10,4,10,17}},
    {'y', {0,0,17,17,15,1,14}},
    {'z', {0,0,31,2,4,8,31}},
    {':', {0,4,4,0,4,4,0}},
    {'/', {1,2,2,4,8,8,16}},
    {'.', {0,0,0,0,0,6,6}},
    {'-', {0,0,0,31,0,0,0}},
    {'_', {0,0,0,0,0,0,31}},
    {'?', {14,17,1,2,4,0,4}},
    {'=', {0,31,0,31,0,0,0}},
    {'&', {12,18,20,8,21,18,13}},
    {'(', {2,4,8,8,8,4,2}},
    {')', {8,4,2,2,2,4,8}},
    {'+', {0,4,4,31,4,4,0}},
    {'|', {4,4,4,4,4,4,4}},
}};

class File final
{
  public:
    explicit File(int descriptor = -1) noexcept : descriptor_{descriptor}
    {
    }

    ~File()
    {
        reset();
    }

    File(const File &) = delete;
    File &operator=(const File &) = delete;

    File(File &&other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)}
    {
    }

    File &operator=(File &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return descriptor_ >= 0;
    }

    [[nodiscard]] int get() const noexcept
    {
        return descriptor_;
    }

  private:
    void reset() noexcept
    {
        if (descriptor_ >= 0)
        {
            (void)close(descriptor_);
            descriptor_ = -1;
        }
    }

    int descriptor_;
};

class LifetimeProbe final
{
  public:
    explicit LifetimeProbe(bool &destroyed) noexcept : destroyed_{&destroyed}
    {
    }

    ~LifetimeProbe()
    {
        *destroyed_ = true;
    }

    LifetimeProbe(const LifetimeProbe &) = delete;
    LifetimeProbe &operator=(const LifetimeProbe &) = delete;

  private:
    bool *destroyed_;
};

NotificationRequest notification{};

void copy_message(std::span<char> destination, std::string_view source) noexcept
{
    if (destination.empty())
        return;

    const std::size_t count =
        source.size() < destination.size() - 1 ? source.size() : destination.size() - 1;
    for (std::size_t index = 0; index < count; ++index)
        destination[index] = source[index];
    destination[count] = '\0';
}

void notify(std::string_view message) noexcept
{
    copy_message(std::span{notification.message}, message);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
}

[[nodiscard]] bool verify_unique_ownership() noexcept
{
    bool destroyed = false;
    {
        std::unique_ptr<LifetimeProbe> probe{new (std::nothrow_t{}) LifetimeProbe{destroyed}};
        if (!ps5ObserveOwnedAllocation(probe.get()))
            return false;
    }
    return destroyed;
}

[[noreturn]] void halt(std::string_view message) noexcept
{
    notify(message);
    for (;;)
        (void)sceKernelUsleep(1000000);
}

[[nodiscard]] constexpr std::span<const std::uint8_t, 7> glyph_rows(char character) noexcept
{
    for (const auto &glyph : glyphs)
    {
        if (glyph.character == character)
            return glyph.rows;
    }
    return glyphs.front().rows;
}

#ifdef OPENNOW_GPU
thread_local bool linear_canvas=false;
#elif defined(OPENNOW_HOST_PREVIEW)
constexpr bool linear_canvas=true;
#else
constexpr bool linear_canvas=false;
#endif

struct TileTables
{
    std::array<std::uint32_t, frame_width> x{};
    std::array<std::uint32_t, frame_height> y{};
};

constexpr TileTables make_tile_tables() noexcept
{
    TileTables tables;
    constexpr std::uint32_t blocks_per_row = (frame_width + 127U) >> 7;
    for (std::uint32_t x = 0; x < frame_width; ++x)
        tables.x[x] = ((x >> 7) << 16) | (((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^ ((x << 4) & 0x400U) ^
                                          ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U));
    for (std::uint32_t y = 0; y < frame_height; ++y)
        tables.y[y] = (((y >> 7) * blocks_per_row) << 16) |
                      (((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^ ((y << 8) & 0x4000U));
    return tables;
}

constexpr TileTables tile_tables = make_tile_tables();

class PixelRow
{
  public:
    PixelRow(std::uint32_t *pixels, unsigned y) noexcept
        : bytes_{reinterpret_cast<std::uint8_t *>(pixels)}, linear_{linear_canvas}
    {
        if (linear_)
            bytes_ += std::size_t(y) * frame_width * 4;
        else
            tile_ = tile_tables.y[y];
    }

    std::uint32_t &operator[](unsigned x) const noexcept
    {
        if (linear_)
            return reinterpret_cast<std::uint32_t *>(bytes_)[x];
        const std::uint32_t column = tile_tables.x[x];
        const std::uint32_t offset = ((tile_ & 0xffff0000U) + (column & 0xffff0000U)) | ((tile_ ^ column) & 0xffffU);
        return *reinterpret_cast<std::uint32_t *>(bytes_ + offset);
    }

    std::uint32_t *linear() const noexcept
    {
        return linear_ ? reinterpret_cast<std::uint32_t *>(bytes_) : nullptr;
    }

  private:
    std::uint8_t *bytes_;
    std::uint32_t tile_ = 0;
    bool linear_;
};

[[nodiscard]] constexpr std::uint32_t blend(std::uint32_t background, std::uint32_t foreground,
                                            unsigned alpha) noexcept
{
    const unsigned inverse = 255 - alpha;
    std::uint32_t rb = (foreground & 0xff00ffU) * alpha + (background & 0xff00ffU) * inverse + 0x7f007fU;
    std::uint32_t g = ((foreground >> 8) & 255U) * alpha + ((background >> 8) & 255U) * inverse + 127U;
    rb = ((rb + ((rb >> 8) & 0xff00ffU) + 0x10001U) >> 8) & 0xff00ffU;
    g = ((g + (g >> 8) + 1U) >> 8) & 255U;
    return 0xff000000U | rb | (g << 8);
}

[[nodiscard]] constexpr unsigned scale_alpha(unsigned value, unsigned alpha) noexcept
{
    const unsigned product = value * alpha + 127U;
    return (product + 1U + (product >> 8)) >> 8;
}

void blend_into(std::uint32_t &pixel, Color color, unsigned alpha) noexcept
{
    if (alpha == 0)
        return;
    pixel = alpha >= 255 ? static_cast<std::uint32_t>(color) : blend(pixel, static_cast<std::uint32_t>(color), alpha);
}

void fill_span(const PixelRow &row, int begin, int end, Color color, unsigned alpha) noexcept
{
    if (std::uint32_t *pixels = row.linear(); pixels && begin < end)
    {
        const auto value = static_cast<std::uint32_t>(color);
        if (alpha >= 255)
            std::fill(pixels + begin, pixels + end, value);
        else if (alpha > 0)
            for (std::uint32_t *pixel = pixels + begin, *last = pixels + end; pixel < last; ++pixel)
                *pixel = blend(*pixel, value, alpha);
        return;
    }
    if (alpha >= 255)
        for (int x = begin; x < end; ++x)
            row[static_cast<unsigned>(x)] = static_cast<std::uint32_t>(color);
    else if (alpha > 0)
        for (int x = begin; x < end; ++x)
        {
            auto &pixel = row[static_cast<unsigned>(x)];
            pixel = blend(pixel, static_cast<std::uint32_t>(color), alpha);
        }
}

void fill_rect(std::uint32_t *pixels, const Clip &clip, int x, int y, int width, int height, Color color,
               unsigned alpha = 255) noexcept
{
    const int left = std::max(x, clip.left), right = std::min(x + width, clip.right);
    const int top = std::max(y, clip.top), bottom = std::min(y + height, clip.bottom);
    if (left >= right)
        return;
    for (int row = top; row < bottom; ++row)
        fill_span(PixelRow(pixels, static_cast<unsigned>(row)), left, right, color, alpha);
}

bool inside(const Clip &clip, int x, int y) noexcept
{
    return x >= clip.left && x < clip.right && y >= clip.top && y < clip.bottom;
}

struct Interior
{
    int begin = 0, end = 0;
    bool solid = false;
};

template <typename Distance, typename Inside>
void fill_shape(std::uint32_t *pixels, const Clip &clip, float left, float top, float right, float bottom,
                Color color, unsigned alpha, Distance distance, Inside interior_of) noexcept
{
    const int x0 = std::max(clip.left, static_cast<int>(std::floor(left)) - 1);
    const int y0 = std::max(clip.top, static_cast<int>(std::floor(top)) - 1);
    const int x1 = std::min(clip.right, static_cast<int>(std::ceil(right)) + 1);
    const int y1 = std::min(clip.bottom, static_cast<int>(std::ceil(bottom)) + 1);
    if (x0 >= x1 || alpha == 0)
        return;
    for (int y = y0; y < y1; ++y)
    {
        const PixelRow row(pixels, static_cast<unsigned>(y));
        const float py = y + 0.5f;
        const Interior interior = interior_of(py);
        const int begin = std::clamp(interior.begin, x0, x1);
        const int end = std::clamp(interior.end, begin, x1);
        const auto edge = [&](int from, int to) {
            for (int x = from; x < to; ++x)
            {
                const float d = distance(x + 0.5f, py);
                if (d >= 0.5f)
                    continue;
                const float coverage = d <= -0.5f ? 1.0f : 0.5f - d;
                blend_into(row[static_cast<unsigned>(x)], color,
                           static_cast<unsigned>(coverage * static_cast<float>(alpha) + 0.5f));
            }
        };
        edge(x0, begin);
        if (interior.solid)
            fill_span(row, begin, end, color, alpha);
        edge(end, x1);
    }
}

template <typename Distance>
void fill_shape(std::uint32_t *pixels, const Clip &clip, float left, float top, float right, float bottom,
                Color color, unsigned alpha, Distance distance) noexcept
{
    fill_shape(pixels, clip, left, top, right, bottom, color, alpha, distance, [](float) { return Interior{}; });
}

Interior round_rect_interior(float py, float x, float y, float w, float h, float r, float inset,
                             bool solid) noexcept
{
    r = std::min({r, w / 2, h / 2});
    const float qy = std::abs(py - (y + h / 2)) - (h / 2 - r);
    const float reach = r - inset;
    if (qy > reach)
        return {};
    const float half = w / 2 - r + (qy > 0 ? std::sqrt(reach * reach - qy * qy) : reach);
    const float cx = x + w / 2;
    return {static_cast<int>(std::ceil(cx - half - 0.5f)), static_cast<int>(std::floor(cx + half - 0.5f)) + 1, solid};
}

float round_rect_distance(float px, float py, float x, float y, float w, float h, float r) noexcept
{
    r = std::min({r, w / 2, h / 2});
    const float qx = std::abs(px - (x + w / 2)) - (w / 2 - r);
    const float qy = std::abs(py - (y + h / 2)) - (h / 2 - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

float segment_distance(float px, float py, float ax, float ay, float bx, float by) noexcept
{
    const float dx = bx - ax, dy = by - ay;
    const float length = dx * dx + dy * dy;
    const float t = length > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / length, 0.0f, 1.0f) : 0.0f;
    const float ex = px - ax - t * dx, ey = py - ay - t * dy;
    return std::sqrt(ex * ex + ey * ey);
}

float length(float x, float y) noexcept
{
    return std::sqrt(x * x + y * y);
}

void fill_circle(std::uint32_t *pixels, const Clip &clip, int center_x, int center_y, int radius,
                 Color color) noexcept
{
    for (int y = -radius; y <= radius; ++y)
    {
        int half = 0;
        while ((half + 1) * (half + 1) + y * y <= radius * radius)
            ++half;
        fill_rect(pixels, clip, center_x - half, center_y + y, 2 * half + 1, 1, color);
    }
}

void fill_triangle(std::uint32_t *pixels, const Clip &clip, int center_x, int top, unsigned half_width,
                   unsigned height, Color color) noexcept
{
    for (unsigned row = 0; row < height; ++row)
    {
        const int half = static_cast<int>(row * half_width / height);
        fill_rect(pixels, clip, center_x - half, top + static_cast<int>(row), 2 * half + 1, 1, color);
    }
}

void draw_text(std::uint32_t *pixels, const Clip &clip, int x, int y, std::string_view value,
               unsigned scale, Color color, unsigned alpha) noexcept
{
    const int size = static_cast<int>(scale);
    for (const char character : value)
    {
        const auto rows = glyph_rows(character);
        for (unsigned row = 0; row < rows.size(); ++row)
        {
            for (unsigned column = 0; column < 5; ++column)
            {
                if ((rows[row] & (1U << (4 - column))) != 0)
                    fill_rect(pixels, clip, x + static_cast<int>(column) * size, y + static_cast<int>(row) * size,
                              size, size, color, alpha);
            }
        }
        x += 6 * size;
        if (x >= clip.right)
            return;
    }
}

void flush_range(void *address, std::size_t length) noexcept
{
    auto *at = static_cast<std::uint8_t *>(address);
    const auto *end = at + length;

    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
} // namespace

int Canvas::shiftX() const noexcept {return static_cast<int>(std::lround(offsetX_));}
int Canvas::shiftY() const noexcept {return static_cast<int>(std::lround(offsetY_));}
unsigned Canvas::faded(unsigned alpha) const noexcept
{
    return opacity_ >= 255 ? std::min(alpha, 255U) : scale_alpha(std::min(alpha, 255U), opacity_);
}

void Canvas::setClip(float x, float y, float width, float height) noexcept
{
    clip_.left = std::max(clip_.left, static_cast<int>(std::floor(x + offsetX_)));
    clip_.top = std::max(clip_.top, static_cast<int>(std::floor(y + offsetY_)));
    clip_.right = std::min(clip_.right, static_cast<int>(std::ceil(x + offsetX_ + width)));
    clip_.bottom = std::min(clip_.bottom, static_cast<int>(std::ceil(y + offsetY_ + height)));
}

void Canvas::resetClip() noexcept {clip_ = Clip{};}

void Canvas::image(unsigned x,unsigned y,unsigned width,unsigned height,const std::uint32_t* data) noexcept
{
    if(!data)return;
    const int left=static_cast<int>(x)+shiftX(), top=static_cast<int>(y)+shiftY();
    const int c0=std::max(0,clip_.left-left), c1=std::min(static_cast<int>(width),clip_.right-left);
    const unsigned alpha=faded(255);
    for(unsigned row=0;row<height;++row) {
        const int py=top+static_cast<int>(row);
        if(py<clip_.top||py>=clip_.bottom)continue;
        const PixelRow target(pixels_,static_cast<unsigned>(py));
        const std::uint32_t* source=data+std::size_t(row)*width;
        for(int col=c0;col<c1;++col)blend_into(target[static_cast<unsigned>(left+col)],static_cast<Color>(source[col]),alpha);
    }
}

void Canvas::clear(Color color) noexcept
{
    fill_rect(pixels_, clip_, 0, 0, frame_width, frame_height, color, faded(255));
}

void Canvas::rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                       Color color) noexcept
{
    fill_rect(pixels_, clip_, static_cast<int>(x) + shiftX(), static_cast<int>(y) + shiftY(),
              static_cast<int>(std::min(width, frame_width)), static_cast<int>(std::min(height, frame_height)), color,
              faded(255));
}

void Canvas::circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept
{
    fill_circle(pixels_, clip_, static_cast<int>(center_x) + shiftX(), static_cast<int>(center_y) + shiftY(),
                static_cast<int>(std::min(radius, frame_width)), color);
}

void Canvas::triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                      Color color) noexcept
{
    fill_triangle(pixels_, clip_, static_cast<int>(center_x) + shiftX(), static_cast<int>(top) + shiftY(),
                  half_width, std::min(height, frame_height), color);
}

void Canvas::button(unsigned x, unsigned y, Button button, unsigned size, Color color) noexcept
{
    if (size < 16) return;
    // Draw every symbol in the same square, using thin, antialiased outlines.
    const auto line = [](float px, float py, float ax, float ay, float bx, float by) {
        const float dx = bx - ax, dy = by - ay;
        const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) /
                                  (dx * dx + dy * dy), 0.0f, 1.0f);
        return std::hypot(px - ax - t * dx, py - ay - t * dy);
    };
    const int left = static_cast<int>(x) + shiftX(), top = static_cast<int>(y) + shiftY();
    const unsigned alpha = faded(255);
    for (unsigned row = 0; row < size; ++row)
    {
        for (unsigned column = 0; column < size; ++column)
        {
            const int px_screen = left + static_cast<int>(column), py_screen = top + static_cast<int>(row);
            if (!inside(clip_, px_screen, py_screen))
                continue;
            unsigned coverage = 0;
            for (unsigned sy = 0; sy < 4; ++sy)
            for (unsigned sx = 0; sx < 4; ++sx)
            {
                const float px = (column + (sx + 0.5f) / 4) / size;
                const float py = (row + (sy + 0.5f) / 4) / size;
                bool ink = false;
                switch (button)
                {
                case Button::cross:
                    ink = std::min(line(px, py, .23f, .23f, .77f, .77f),
                                   line(px, py, .77f, .23f, .23f, .77f)) < .035f;
                    break;
                case Button::circle:
                    ink = std::abs(std::hypot(px - .5f, py - .5f) - .32f) < .035f;
                    break;
                case Button::square:
                    ink = std::abs(std::max(std::abs(px - .5f), std::abs(py - .5f)) - .30f) < .035f;
                    break;
                case Button::triangle:
                    ink = std::min({line(px, py, .5f, .16f, .85f, .78f),
                                    line(px, py, .85f, .78f, .15f, .78f),
                                    line(px, py, .15f, .78f, .5f, .16f)}) < .035f;
                    break;
                case Button::up:
                case Button::down:
                {
                    const float qy = button == Button::up ? py : 1 - py;
                    ink = std::min({line(px, qy, .25f, .46f, .5f, .21f),
                                    line(px, qy, .5f, .21f, .75f, .46f),
                                    line(px, qy, .5f, .21f, .5f, .79f)}) < .035f;
                    break;
                }
                case Button::dpad:
                    // Four distinct direction arrows, matching the controller's D-pad.
                    for (unsigned turn = 0; turn < 4; ++turn)
                    {
                        const float qx = turn == 0 ? px : turn == 1 ? py : turn == 2 ? 1-px : 1-py;
                        const float qy = turn == 0 ? py : turn == 1 ? 1-px : turn == 2 ? 1-py : px;
                        ink |= std::min({line(qx,qy,.38f,.28f,.5f,.16f),
                                         line(qx,qy,.5f,.16f,.62f,.28f),
                                         line(qx,qy,.5f,.16f,.5f,.37f)}) < .027f;
                    }
                    break;
                case Button::options:
                    for (float yy : {.30f, .50f, .70f})
                        ink |= line(px, py, .25f, yy, .75f, yy) < .03f;
                    break;
                case Button::l1:
                case Button::r1:
                {
                    const float qx = std::max(std::abs(px-.5f)-.32f, 0.0f);
                    const float qy = std::max(std::abs(py-.5f)-.20f, 0.0f);
                    ink = std::abs(std::hypot(qx,qy)-.09f) < .025f;
                    break;
                }
                }
                coverage += ink;
            }
            if (!coverage) continue;
            blend_into(PixelRow(pixels_, static_cast<unsigned>(py_screen))[static_cast<unsigned>(px_screen)], color,
                       (coverage * alpha + 8) / 16);
        }
    }
    if (button == Button::l1 || button == Button::r1)
    {
        const unsigned scale = size / 20;
        draw_text(pixels_, clip_, left + static_cast<int>(size - 11*scale)/2, top + static_cast<int>(size - 7*scale)/2,
                  button == Button::l1 ? "L1" : "R1", scale, color, alpha);
    }
}

void Canvas::text(unsigned x, unsigned y, std::string_view value, unsigned scale,
                  Color color) noexcept
{
    draw_text(pixels_, clip_, static_cast<int>(x) + shiftX(), static_cast<int>(y) + shiftY(), value, scale, color,
              faded(255));
}

void Canvas::coverage(int x, int y, unsigned width, unsigned height, const std::uint8_t *mask,
                      unsigned stride, Color color, unsigned alpha) noexcept
{
    if (!mask)
        return;
    x += shiftX();
    y += shiftY();
    alpha = faded(alpha);
    const int c0 = std::max(0, clip_.left - x);
    const int c1 = std::min(static_cast<int>(width), clip_.right - x);
    for (unsigned row = 0; row < height; ++row)
    {
        const int py = y + static_cast<int>(row);
        if (py < clip_.top || py >= clip_.bottom)
            continue;
        const PixelRow target(pixels_, static_cast<unsigned>(py));
        const std::uint8_t *values = mask + std::size_t(row) * stride;
        for (int column = c0; column < c1; ++column)
            if (const unsigned value = values[column])
                blend_into(target[static_cast<unsigned>(x + column)], color, scale_alpha(value, alpha));
    }
}

void Canvas::roundRect(float x, float y, float w, float h, float radius, Color color,
                       unsigned alpha) noexcept
{
    if (w <= 0 || h <= 0)
        return;
    x += offsetX_;
    y += offsetY_;
    fill_shape(
        pixels_, clip_, x, y, x + w, y + h, color, faded(alpha),
        [&](float px, float py) { return round_rect_distance(px, py, x, y, w, h, radius); },
        [&](float py) { return round_rect_interior(py, x, y, w, h, radius, 0.5f, true); });
}

void Canvas::roundRectStroke(float x, float y, float w, float h, float radius, float thickness,
                             Color color, unsigned alpha) noexcept
{
    x += offsetX_;
    y += offsetY_;
    const float half = thickness / 2;
    fill_shape(
        pixels_, clip_, x, y, x + w, y + h, color, faded(alpha),
        [&](float px, float py) {
            return std::abs(round_rect_distance(px, py, x + half, y + half, w - thickness, h - thickness,
                                                radius - half)) - half;
        },
        [&](float py) {
            return round_rect_interior(py, x + half, y + half, w - thickness, h - thickness, radius - half,
                                       half + 0.5f, false);
        });
}

void Canvas::verticalGradient(float x, float y, float w, float h, float radius, Color top,
                              Color bottom) noexcept
{
    x += offsetX_;
    y += offsetY_;
    const unsigned alpha = faded(255);
    const int y0 = std::max(clip_.top, static_cast<int>(std::floor(y)));
    const int y1 = std::min(clip_.bottom, static_cast<int>(std::ceil(y + h)));
    for (int row = y0; row < y1; ++row)
    {
        const float t = std::clamp((row + 0.5f - y) / h, 0.0f, 1.0f);
        std::uint32_t mixed = 0xff000000;
        for (unsigned shift : {0U, 8U, 16U})
        {
            const float a = static_cast<float>((static_cast<std::uint32_t>(top) >> shift) & 255U);
            const float b = static_cast<float>((static_cast<std::uint32_t>(bottom) >> shift) & 255U);
            mixed |= static_cast<std::uint32_t>(a + (b - a) * t + 0.5f) << shift;
        }
        const int x0 = std::max(clip_.left, static_cast<int>(std::floor(x)));
        const int x1 = std::min(clip_.right, static_cast<int>(std::ceil(x + w)));
        const float py = row + 0.5f;
        const bool corner = py < y + radius || py > y + h - radius;
        const PixelRow target(pixels_, static_cast<unsigned>(row));
        for (int column = x0; column < x1; ++column)
        {
            const float px = column + 0.5f;
            const float d = corner || px < x + 1 || px > x + w - 1 ? round_rect_distance(px, py, x, y, w, h, radius) : -1.0f;
            if (d >= 0.5f)
                continue;
            blend_into(target[static_cast<unsigned>(column)], static_cast<Color>(mixed),
                       d <= -0.5f ? alpha : static_cast<unsigned>((0.5f - d) * static_cast<float>(alpha) + 0.5f));
        }
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, float thickness, Color color,
                  unsigned alpha) noexcept
{
    x0 += offsetX_;
    x1 += offsetX_;
    y0 += offsetY_;
    y1 += offsetY_;
    const float pad = thickness / 2;
    fill_shape(pixels_, clip_, std::min(x0, x1) - pad, std::min(y0, y1) - pad, std::max(x0, x1) + pad,
               std::max(y0, y1) + pad, color, faded(alpha), [&](float px, float py) {
                   return segment_distance(px, py, x0, y0, x1, y1) - pad;
               });
}

void Canvas::ring(float cx, float cy, float radius, float thickness, Color color, unsigned alpha) noexcept
{
    cx += offsetX_;
    cy += offsetY_;
    const float outer = radius + thickness / 2;
    fill_shape(
        pixels_, clip_, cx - outer, cy - outer, cx + outer, cy + outer, color, faded(alpha),
        [&](float px, float py) { return std::abs(length(px - cx, py - cy) - radius) - thickness / 2; },
        [&](float py) {
            return round_rect_interior(py, cx - radius, cy - radius, 2 * radius, 2 * radius, radius,
                                       thickness / 2 + 0.5f, false);
        });
}

void Canvas::arc(float cx, float cy, float radius, float thickness, float start, float sweep, Color color,
                 unsigned alpha) noexcept
{
    if (sweep <= 0)
        return;
    if (sweep >= 6.2831853f)
    {
        ring(cx, cy, radius, thickness, color, alpha);
        return;
    }
    cx += offsetX_;
    cy += offsetY_;
    const float half = thickness / 2, outer = radius + half;
    const float ax = cx + std::cos(start) * radius, ay = cy + std::sin(start) * radius;
    const float bx = cx + std::cos(start + sweep) * radius, by = cy + std::sin(start + sweep) * radius;
    fill_shape(
        pixels_, clip_, cx - outer, cy - outer, cx + outer, cy + outer, color, faded(alpha),
        [&](float px, float py) {
            float angle = std::atan2(py - cy, px - cx) - start;
            angle -= 6.2831853f * std::floor(angle / 6.2831853f);
            if (angle <= sweep)
                return std::abs(length(px - cx, py - cy) - radius) - half;
            return std::min(length(px - ax, py - ay), length(px - bx, py - by)) - half;
        },
        [&](float py) {
            return round_rect_interior(py, cx - radius, cy - radius, 2 * radius, 2 * radius, radius, half + 0.5f,
                                       false);
        });
}

void Canvas::disc(float cx, float cy, float radius, Color color, unsigned alpha) noexcept
{
    cx += offsetX_;
    cy += offsetY_;
    fill_shape(
        pixels_, clip_, cx - radius, cy - radius, cx + radius, cy + radius, color, faded(alpha),
        [&](float px, float py) { return length(px - cx, py - cy) - radius; },
        [&](float py) {
            return round_rect_interior(py, cx - radius, cy - radius, 2 * radius, 2 * radius, radius, 0.5f, true);
        });
}

void Canvas::imageRounded(int x, int y, unsigned width, unsigned height,
                          const std::uint32_t *data, float radius, unsigned alpha) noexcept
{
    alpha = faded(alpha);
    if (!data || alpha == 0)
        return;
    x += shiftX();
    y += shiftY();
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    const int c0 = std::max(0, clip_.left - x);
    const int c1 = std::min(static_cast<int>(width), clip_.right - x);
    for (unsigned row = 0; row < height; ++row)
    {
        const int py = y + static_cast<int>(row);
        if (py < clip_.top || py >= clip_.bottom)
            continue;
        const PixelRow target(pixels_, static_cast<unsigned>(py));
        const std::uint32_t *source = data + std::size_t(row) * width;
        const float cy = static_cast<float>(row) + 0.5f;
        const bool corner = cy < radius || cy > h - radius;
        if (std::uint32_t *pixels = target.linear(); pixels && c0 < c1)
        {
            std::uint32_t *out = pixels + x;
            int begin = c0, end = c1;
            if (corner)
            {
                while (begin < c1 && begin + 0.5f < radius)
                    ++begin;
                while (end > begin && end - 0.5f > w - radius)
                    --end;
            }
            const auto edge = [&](int from, int to) {
                for (int column = from; column < to; ++column)
                {
                    const float d = round_rect_distance(column + 0.5f, cy, 0, 0, w, h, radius);
                    if (d >= 0.5f)
                        continue;
                    blend_into(out[column], static_cast<Color>(source[column]),
                               d > -0.5f ? static_cast<unsigned>((0.5f - d) * static_cast<float>(alpha) + 0.5f) : alpha);
                }
            };
            edge(c0, begin);
            if (alpha >= 255)
                std::copy(source + begin, source + end, out + begin);
            else
                for (int column = begin; column < end; ++column)
                    out[column] = blend(out[column], source[column], alpha);
            edge(end, c1);
            continue;
        }
        for (int column = c0; column < c1; ++column)
        {
            const float cx = static_cast<float>(column) + 0.5f;
            unsigned coverage = alpha;
            if (corner && (cx < radius || cx > w - radius))
            {
                const float d = round_rect_distance(cx, cy, 0, 0, w, h, radius);
                if (d >= 0.5f)
                    continue;
                if (d > -0.5f)
                    coverage = static_cast<unsigned>((0.5f - d) * static_cast<float>(alpha) + 0.5f);
            }
            blend_into(target[static_cast<unsigned>(x + column)], static_cast<Color>(source[column]), coverage);
        }
    }
}

void Canvas::imageScaled(float x, float y, float w, float h, const std::uint32_t *data, unsigned sourceWidth,
                         unsigned sourceHeight, float radius, unsigned alpha) noexcept
{
    alpha = faded(alpha);
    if (!data || alpha == 0 || w < 1 || h < 1 || sourceWidth < 2 || sourceHeight < 2)
        return;
    x += offsetX_;
    y += offsetY_;
    const int x0 = std::max(clip_.left, static_cast<int>(std::floor(x)));
    const int y0 = std::max(clip_.top, static_cast<int>(std::floor(y)));
    const int x1 = std::min(clip_.right, static_cast<int>(std::ceil(x + w)));
    const int y1 = std::min(clip_.bottom, static_cast<int>(std::ceil(y + h)));
    const float sx = static_cast<float>(sourceWidth) / w, sy = static_cast<float>(sourceHeight) / h;
    const float maxU = static_cast<float>(sourceWidth - 1), maxV = static_cast<float>(sourceHeight - 1);
    for (int row = y0; row < y1; ++row)
    {
        const PixelRow target(pixels_, static_cast<unsigned>(row));
        const float py = row + 0.5f;
        const float v = std::clamp((py - y) * sy - 0.5f, 0.0f, maxV);
        const unsigned v0 = static_cast<unsigned>(v), v1 = std::min(v0 + 1, sourceHeight - 1);
        const unsigned fy = static_cast<unsigned>((v - static_cast<float>(v0)) * 256);
        const std::uint32_t *top = data + std::size_t(v0) * sourceWidth;
        const std::uint32_t *bottom = data + std::size_t(v1) * sourceWidth;
        const bool corner = py < y + radius || py > y + h - radius;
        for (int column = x0; column < x1; ++column)
        {
            const float px = column + 0.5f;
            const float d = corner || px < x + radius || px > x + w - radius
                                ? round_rect_distance(px, py, x, y, w, h, radius)
                                : std::max({x - px, px - x - w, y - py, py - y - h});
            if (d >= 0.5f)
                continue;
            const unsigned coverage =
                d <= -0.5f ? alpha : static_cast<unsigned>((0.5f - d) * static_cast<float>(alpha) + 0.5f);
            const float u = std::clamp((px - x) * sx - 0.5f, 0.0f, maxU);
            const unsigned u0 = static_cast<unsigned>(u), u1 = std::min(u0 + 1, sourceWidth - 1);
            const unsigned fx = static_cast<unsigned>((u - static_cast<float>(u0)) * 256);
            std::uint32_t mixed = 0xff000000U;
            for (unsigned shift : {0U, 8U, 16U})
            {
                const auto channel = [&](std::uint32_t pixel) { return (pixel >> shift) & 255U; };
                const unsigned upper = channel(top[u0]) * (256 - fx) + channel(top[u1]) * fx;
                const unsigned lower = channel(bottom[u0]) * (256 - fx) + channel(bottom[u1]) * fx;
                mixed |= ((upper * (256 - fy) + lower * fy + 32768U) >> 16) << shift;
            }
            blend_into(target[static_cast<unsigned>(column)], static_cast<Color>(mixed), coverage);
        }
    }
}

void Canvas::imageDoubled(const std::uint32_t *data, unsigned sourceWidth, unsigned sourceHeight, unsigned alpha,
                          const std::uint32_t *under) noexcept
{
    alpha = under ? std::min(alpha, 255U) : faded(alpha);
    if (!data || (alpha == 0 && !under))
        return;
    const int x = shiftX(), y = shiftY();
    const int x0 = std::max(clip_.left, x), x1 = std::min(clip_.right, x + 2 * static_cast<int>(sourceWidth));
    const int y0 = std::max(clip_.top, y), y1 = std::min(clip_.bottom, y + 2 * static_cast<int>(sourceHeight));
    for (int row = y0; row < y1; ++row)
    {
        const PixelRow target(pixels_, static_cast<unsigned>(row));
        const std::size_t offset = std::size_t((row - y) / 2) * sourceWidth;
        const std::uint32_t *source = data + offset;
        const std::uint32_t *below = under ? under + offset : nullptr;
        if (std::uint32_t *pixels = target.linear(); pixels && x0 < x1)
        {
            std::uint32_t *out = pixels + x;
            const auto put = [&](int at) {
                const std::uint32_t over = source[at / 2];
                out[at] = below ? blend(below[at / 2], over, alpha) : alpha >= 255 ? over : blend(out[at], over, alpha);
            };
            int at = x0 - x;
            const int last = x1 - x;
            if (at < last && (at & 1))
                put(at++);
            const auto pairs = [&](auto value) {
                const std::uint32_t *from = source + at / 2, *under_from = below ? below + at / 2 : nullptr;
                std::uint32_t *to = out + at;
                const int count = (last - at) / 2;
                for (int i = 0; i < count; ++i)
                {
                    const std::uint64_t pixel = value(from[i], under_from ? under_from[i] : 0);
                    const std::uint64_t pair = pixel | (pixel << 32);
                    std::memcpy(to + 2 * i, &pair, sizeof(pair));
                }
                at += 2 * count;
            };
            if (below)
                pairs([&](std::uint32_t over, std::uint32_t base) { return blend(base, over, alpha); });
            else if (alpha >= 255)
                pairs([](std::uint32_t over, std::uint32_t) { return over; });
            for (; at < last; ++at)
                put(at);
            continue;
        }
        for (int column = x0; column < x1;)
        {
            const int sourceX = (column - x) / 2;
            const bool pair = ((column - x) & 1) == 0 && column + 1 < x1;
            auto &first = target[static_cast<unsigned>(column)];
            const std::uint32_t value = below ? blend(below[sourceX], source[sourceX], alpha)
                                        : alpha >= 255 ? source[sourceX]
                                                       : blend(first, source[sourceX], alpha);
            first = value;
            if (pair)
                (&first)[1] = below || alpha >= 255 ? value : blend((&first)[1], source[sourceX], alpha);
            column += pair ? 2 : 1;
        }
    }
}

void Canvas::scrim(float x, float y, float w, float h, float radius, float from, Color color, unsigned alpha) noexcept
{
    x += offsetX_;
    y += offsetY_;
    alpha = faded(alpha);
    const float top = y + h * from, span = h * (1 - from);
    const int y0 = std::max(clip_.top, static_cast<int>(std::floor(top)));
    const int y1 = std::min(clip_.bottom, static_cast<int>(std::ceil(y + h)));
    const int x0 = std::max(clip_.left, static_cast<int>(std::floor(x)));
    const int x1 = std::min(clip_.right, static_cast<int>(std::ceil(x + w)));
    for (int row = y0; row < y1; ++row)
    {
        const float py = row + 0.5f;
        const unsigned shade = static_cast<unsigned>(std::clamp((py - top) / span, 0.0f, 1.0f) * static_cast<float>(alpha) + 0.5f);
        if (!shade)
            continue;
        const PixelRow target(pixels_, static_cast<unsigned>(row));
        const bool corner = py > y + h - radius;
        int begin = x0, end = x1;
        if (!corner)
        {
            while (begin < x1 && begin + 0.5f < x + 1)
                ++begin;
            while (end > begin && end - 0.5f > x + w - 1)
                --end;
        }
        else
            begin = end = x1;
        const auto edge = [&](int from, int to) {
            for (int column = from; column < to; ++column)
            {
                const float d = round_rect_distance(column + 0.5f, py, x, y, w, h, radius);
                if (d >= 0.5f)
                    continue;
                blend_into(target[static_cast<unsigned>(column)], color,
                           d <= -0.5f ? shade : static_cast<unsigned>((0.5f - d) * static_cast<float>(shade) + 0.5f));
            }
        };
        edge(x0, begin);
        fill_span(target, begin, end, color, shade);
        edge(end, x1);
    }
}

void Canvas::polygon(const float *points, unsigned count, Color color, unsigned alpha) noexcept
{
    if (!points || count < 3)
        return;
    const float ox = offsetX_, oy = offsetY_;
    float left = points[0], right = points[0], top = points[1], bottom = points[1];
    for (unsigned i = 1; i < count; ++i)
    {
        left = std::min(left, points[i * 2]);
        right = std::max(right, points[i * 2]);
        top = std::min(top, points[i * 2 + 1]);
        bottom = std::max(bottom, points[i * 2 + 1]);
    }
    fill_shape(pixels_, clip_, left + ox, top + oy, right + ox, bottom + oy, color, faded(alpha),
               [&](float px, float py) {
                   px -= ox;
                   py -= oy;
                   float nearest = 1e9f;
                   bool inside = false;
                   for (unsigned i = 0, j = count - 1; i < count; j = i++)
                   {
                       const float ax = points[j * 2], ay = points[j * 2 + 1];
                       const float bx = points[i * 2], by = points[i * 2 + 1];
                       nearest = std::min(nearest, segment_distance(px, py, ax, ay, bx, by));
                       if ((by > py) != (ay > py) && px < (ax - bx) * (py - by) / (ay - by) + bx)
                           inside = !inside;
                   }
                   return inside ? -nearest : nearest;
               });
}

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept
{
    copy_message(destination, fallback);
    File descriptor{open(path, 0)};
    if (!descriptor.valid() || destination.empty())
        return;

    const long count = read(descriptor.get(), destination.data(), destination.size() - 1);
    if (count <= 0)
        return;

    std::size_t length = static_cast<std::size_t>(count);
    while (length > 0 && (destination[length - 1] == '\r' || destination[length - 1] == '\n'))
        --length;
    destination[length] = '\0';
}

void requestStop() noexcept {stopRequested.store(true);}

#ifdef OPENNOW_HOST_PREVIEW
namespace {
void writePpm(const std::uint32_t* pixels,const char* path) noexcept
{
    auto* file=std::fopen(path,"wb");
    if (!file) std::exit(1);
    std::fprintf(file,"P6\n%u %u\n255\n",frame_width,frame_height);
    static unsigned char row[frame_width*3];
    for (unsigned y=0;y<frame_height;++y) {
        const PixelRow source(const_cast<std::uint32_t*>(pixels),y);
        for (unsigned x=0;x<frame_width;++x) {
            const auto pixel=source[x];
            row[x*3]=static_cast<unsigned char>(pixel);row[x*3+1]=static_cast<unsigned char>(pixel>>8);row[x*3+2]=static_cast<unsigned char>(pixel>>16);
        }
        std::fwrite(row,1,sizeof(row),file);
    }
    std::fclose(file);
}
}

void runFrames(DrawScene draw, unsigned count, PreviewStep step, const char *directory, const char *finalPath,
               bool report) noexcept
{
    auto* pixels=static_cast<std::uint32_t*>(std::calloc(frame_bytes,1));
    auto* times=static_cast<double*>(std::calloc(count?count:1,sizeof(double)));
    if (!pixels||!times) std::exit(1);
    Canvas canvas(pixels);
    unsigned drawn=0;
    for (unsigned frame=0;frame<count;++frame) {
        step(frame);
        timespec begin{},end{};
        clock_gettime(CLOCK_MONOTONIC,&begin);
        const bool changed=draw(canvas);
        clock_gettime(CLOCK_MONOTONIC,&end);
        if (changed) times[drawn++]=double(end.tv_sec-begin.tv_sec)*1e3+double(end.tv_nsec-begin.tv_nsec)/1e6;
        if (directory) {
            char path[512];
            std::snprintf(path,sizeof(path),"%s/frame-%04u.ppm",directory,frame);
            writePpm(pixels,path);
        }
    }
    if (finalPath) writePpm(pixels,finalPath);
    if (!report) {std::free(times); std::free(pixels); return;}
    std::sort(times,times+drawn);
    double total=0;
    for (unsigned i=0;i<drawn;++i) total+=times[i];
    if (drawn) std::printf("frames=%u drawn=%u mean=%.3fms p50=%.3fms p95=%.3fms max=%.3fms\n",count,drawn,total/drawn,
                           times[drawn/2],times[std::min(drawn-1,drawn*95/100)],times[drawn-1]);
    else std::printf("frames=%u drawn=0\n",count);
    std::free(times); std::free(pixels);
}
#endif

#ifdef OPENNOW_GPU
namespace
{
// Summarizes interface frame cost in ui-perf.log: CPU raster, GPU upload plus
// swap, and the interval between consecutive presented frames.
class FramePerf
{
  public:
    void frame(std::uint64_t begin, std::uint64_t drawn, std::uint64_t presented) noexcept
    {
        const auto interval = previous_ && begin - previous_ < 100000 ? presented - previous_ : 0;
        previous_ = presented;
        raster_[count_] = static_cast<std::uint32_t>(drawn - begin);
        present_[count_] = static_cast<std::uint32_t>(presented - drawn);
        interval_[count_] = static_cast<std::uint32_t>(interval);
        if (++count_ == size)
            flush();
    }
    void idle() noexcept { previous_ = 0; }

  private:
    static constexpr unsigned size = 240;
    struct Stats
    {
        float p50, p95, max;
    };
    static Stats stats(std::uint32_t *values, unsigned count) noexcept
    {
        if (!count)
            return {};
        std::sort(values, values + count);
        return {values[count / 2] / 1000.0f, values[count * 95 / 100] / 1000.0f, values[count - 1] / 1000.0f};
    }
    void flush() noexcept
    {
        std::uint32_t intervals[size];
        unsigned measured = 0, late = 0;
        for (unsigned i = 0; i < size; ++i)
            if (interval_[i])
            {
                intervals[measured++] = interval_[i];
                late += interval_[i] > 17500;
            }
        const Stats raster = stats(raster_, size), present = stats(present_, size), interval = stats(intervals, measured);
        count_ = 0;
        if (lines_ >= 200)
            return;
        ++lines_;
        char line[256];
        const int length = std::snprintf(line, sizeof(line),
            "frames=%u raster p50=%.2f p95=%.2f max=%.2f present p50=%.2f p95=%.2f max=%.2f "
            "interval n=%u p50=%.2f p95=%.2f max=%.2f over17.5=%u\n",
            size, raster.p50, raster.p95, raster.max, present.p50, present.p95, present.max, measured, interval.p50,
            interval.p95, interval.max, late);
        const int fd = opennow::accountFile::open(opennow_storage_file("ui-perf.log"), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0)
            return;
        (void)opennow::accountFile::write(fd, line, static_cast<std::size_t>(std::max(length, 0)));
        (void)opennow::accountFile::close(fd);
    }
    std::uint32_t raster_[size]{}, present_[size]{}, interval_[size]{};
    unsigned count_ = 0, lines_ = 0;
    std::uint64_t previous_ = 0;
};
} // namespace
#endif

void run(DrawScene draw, std::string_view ready_message) noexcept
{
#ifdef OPENNOW_HOST_PREVIEW
    (void)ready_message;
    auto* pixels=static_cast<std::uint32_t*>(std::calloc(frame_bytes,1));
    if (!pixels) std::exit(1);
    Canvas canvas(pixels); draw(canvas);
    writePpm(pixels,"build/preview.ppm");
    std::free(pixels); std::exit(0);
#else
#ifdef OPENNOW_GPU
    if(opennow::gpu::available()) {
        linear_canvas=true;
        auto* pixels=static_cast<std::uint32_t*>(std::calloc(frame_width*frame_height,4));
        if(!pixels)halt("OpenNOW: UI allocation failed");
        Canvas canvas(pixels);
        (void)sceSystemServiceHideSplashScreen();
        static FramePerf perf;
        while(!stopRequested.load()) {
            const auto begin=sceKernelGetProcessTime();
            if(draw(canvas)) {
                const auto drawn=sceKernelGetProcessTime();
                if(!opennow::gpu::present(pixels))halt("OpenNOW: GPU presentation failed");
                perf.frame(begin,drawn,sceKernelGetProcessTime());
            } else {perf.idle();sceKernelUsleep(1000);}
        }
        std::free(pixels);
        return;
    }
#endif
    if (!verify_unique_ownership())
        halt("OpenNOW: unique ownership failed");
    if (draw == nullptr)
        halt("OpenNOW: scene callback missing");

    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0)
        halt("OpenNOW: sceVideoOutOpen failed");

    const std::size_t pool_size = sceKernelGetDirectMemorySize();
    if (pool_size < memory_bytes)
        halt("OpenNOW: insufficient direct memory");

    std::int64_t physical_address = 0;
    int result =
        sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool_size), memory_bytes,
                                      memory_alignment, memory_type_wc_garlic, &physical_address);
    if (result < 0)
        halt("OpenNOW: direct-memory allocation failed");

    void *mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, memory_bytes, map_protection, 0, physical_address,
                                      memory_alignment);
    if (result < 0)
        halt("OpenNOW: direct-memory mapping failed");

    Canvas first{static_cast<std::uint32_t *>(mapped)};
    auto *second_frame = static_cast<std::uint8_t *>(mapped) + frame_bytes;
    Canvas second{reinterpret_cast<std::uint32_t *>(second_frame)};
    draw(first);
    // The second buffer is drawn before its first presentation.
    flush_range(mapped, memory_bytes);

    std::array<VideoBuffer, 2> buffers{{
        {mapped, nullptr, nullptr, nullptr},
        {second_frame, nullptr, nullptr, nullptr},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, frame_width,
                                   frame_height, 0, 0, 0);

    result = sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(),
                                         static_cast<std::int32_t>(buffers.size()), &attribute, 0,
                                         nullptr);
    if (result < 0)
        halt("OpenNOW: buffer registration failed");
    struct kevent* queue=nullptr;
    if (sceKernelCreateEqueue(&queue,"opennow-flip")<0 || sceVideoOutAddFlipEvent(queue,video,nullptr)<0)
        halt("OpenNOW: flip event setup failed");
    if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0)
        halt("OpenNOW: initial flip failed");

    (void)sceVideoOutWaitVblank(video);
    notify(ready_message);

    // Wait for completion before recycling a displayed buffer.
    unsigned index=0;
    std::int64_t serial=1;
    while(!stopRequested.load()) {
        struct kevent event{}; int count=0; unsigned timeout=2000000;
        const int waited=sceKernelWaitEqueue(queue,&event,1,&count,&timeout);
        if(stopRequested.load())break;
        if(waited<0||count!=1)halt("OpenNOW: flip completion timed out");
        index^=1;
        auto& canvas=index ? second : first;
        // Poll promptly so a 60 FPS stream does not wait another 16 ms for a flip.
        while (!stopRequested.load()&&!draw(canvas)) sceKernelUsleep(2000);
        if(stopRequested.load())break;
        flush_range(index ? second_frame : mapped,frame_bytes);
        if (sceVideoOutSubmitFlip(video,index,1,++serial)<0)
            halt("OpenNOW: flip failed");
    }
    (void)sceKernelDeleteEqueue(queue);
    (void)sceVideoOutClose(video);
#endif
}
} // namespace ps5::demo
