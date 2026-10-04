#!/usr/bin/env python3
"""Kodi's additions to the ps5-opengl sources, applied by
scripts/18-build-ps5-opengl.sh before the SDK is rebuilt.

One addition remains: the driver exports its video out handle, which Kodi's
window system needs (refresh rate, vblank clock, VRR). Diagnostics that
earlier versions added (driver profile to klog, clear-path counters) are
removed from the source tree when found, so the tree converges to upstream
plus this one function.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

RUNTIME = "src/platform/ps5_agc_native_runtime.c"
SCREEN = "src/gallium/ps5/ps5_screen.c"
EGL = "src/egl/ps5_egl.c"

# (file, anchor, text inserted after the anchor)
ADDITIONS = [
    (RUNTIME, 'static int runtime_video_handle = -1;\n', '\n/* KODI-PS5: video out handle once presentation has opened it (-1 before). */\nint ps5_opengl_video_out_handle(void);\nint ps5_opengl_video_out_handle(void)\n{\n    return runtime_video_handle;\n}\n'),
]

# HDR output (optional): re-register the scanout buffers with another pixel
# format - the HDR 10-bit BT.2020 PQ format (0x8100070422000000, as
# ProsperoLight registers it) or back to the driver's SDR one. Both formats are
# 32 bits per pixel, so the buffers stay the same memory.
HDR = [
    (RUNTIME, None, """
/* KODI-PS5: switch the scanout buffers' pixel format (HDR output) in place:
 * sceVideoOutSubmitChangeBufferAttribute2 on buffer set 0, applied by
 * VideoOut at the next flip - the call games use to toggle HDR. Deliberately
 * no unregister/register fallback: the set on screen cannot be unregistered
 * (RESOURCE_BUSY), the re-register then fails SLOT_OCCUPIED, and that sequence
 * can leave the next present hanging (EVO Player, hardware 2026-09-28).
 * results[0] = change result; results[1..3] unused (0x7fffffff).
 * Returns 0 if the new format is in effect. */
int ps5_opengl_set_scanout_format(uint64_t pixel_format, int32_t results[4]);
#if defined(AGC_RUNTIME_PACKAGES) /* the runtime's video state lives in this build only */
int sceVideoOutSubmitChangeBufferAttribute2(int32_t handle, int32_t set_index,
                                            const void *attribute, int32_t category,
                                            void *option);
int
ps5_opengl_set_scanout_format(uint64_t pixel_format, int32_t results[4])
{
    static uint64_t current = VIDEO_OUT_PIXEL_FORMAT;
    for (int i = 0; i < 4; ++i)
        results[i] = 0x7fffffff;
    if (!runtime_video_registered || runtime_video_handle < 0 || !runtime_video_api.set_attribute2)
        return -1;
    if (pixel_format == current)
        return 0;
    video_attribute_t attribute = {{0}};
    runtime_video_api.set_attribute2(&attribute, pixel_format, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     0, 0, 0);
    results[0] = sceVideoOutSubmitChangeBufferAttribute2(runtime_video_handle, 0, &attribute, 0,
                                                         NULL);
    if (results[0] == 0)
        current = pixel_format;
    return results[0];
}
#else
int
ps5_opengl_set_scanout_format(uint64_t pixel_format, int32_t results[4])
{
    (void)pixel_format;
    for (int i = 0; i < 4; ++i)
        results[i] = 0x7fffffff;
    return -1;
}
#endif
""", "append"),
]

# Written against ps5-opengl 122aa899f9255e37d776b2a5317c86e5e9679907 (2026-09-25,
# "Document native preparation workers and rendering qualification limits"),
# see PS5-OPENGL-COMMIT; scripts/00-setup-wsl.sh checks that revision out. The
# additions are inserted at anchor lines of three files; an anchor that a newer
# revision has changed is reported as "skipped, anchor differs" with its label.
# Zero-copy video (optional: skipped, with a note, if a revision differs).
# A sampled 2D texture over memory the driver does not own (the hardware video
# decoder's frames), handed to GL as an EGL image; see the Kodi renderer
# xbmc/platform/ps5/video/RendererPS5.cpp. (file, anchor, text, where)
ZERO_COPY = [
    (SCREEN, "   unsigned render_arena_slot_count;\n",
     "   bool kodi_foreign_memory; /* KODI-PS5: memory owned by someone else (zero-copy video) */\n",
     "after"),
    (SCREEN, "   simple_mtx_unlock(&ps5->resource_mutex);\n   ps5_release_resource_memory(resource->stencil_data,\n",
     """   if (resource->kodi_foreign_memory) { /* KODI-PS5: not ours to unmap */
      resource->data = NULL;
      resource->allocation_size = 0;
      resource->direct_start = -1;
   }
""", "before"),
    (SCREEN, "   uint64_t *epoch = stencil ? &texture->stencil_publication_epoch : &texture->texture_publication_epoch;\n",
     """   /* KODI-PS5: foreign memory is written by the video decoder, not the CPU:
    * nothing to flush out of the CPU's caches. */
   if (texture->kodi_foreign_memory)
      return;
""", "before"),
    (SCREEN, None, """
/* KODI-PS5: a sampled, linear 2D texture over existing GPU-visible memory
 * (zero-copy video). The row pitch must be what the driver itself would use
 * for a linear texture of that width. */
struct pipe_resource *ps5_kodi_resource_from_memory(struct pipe_screen *screen,
                                                    enum pipe_format format,
                                                    unsigned width, unsigned height,
                                                    unsigned stride, void *data);
struct pipe_resource *
ps5_kodi_resource_from_memory(struct pipe_screen *screen, enum pipe_format format,
                              unsigned width, unsigned height, unsigned stride, void *data)
{
   const unsigned format_size = ps5_texture_format_size(format);
   if (!screen || !data || !format_size || !width || !height ||
       stride != ((format_size * width + 255u) & ~255u) || ((uintptr_t)data & 255u))
      return NULL;
   struct ps5_resource *resource = calloc(1, sizeof(*resource));
   if (!resource)
      return NULL;
   resource->base.target = PIPE_TEXTURE_2D;
   resource->base.format = format;
   resource->base.width0 = width;
   resource->base.height0 = (uint16_t)height;
   resource->base.depth0 = 1;
   resource->base.array_size = 1;
   resource->base.last_level = 0;
   resource->base.bind = PIPE_BIND_SAMPLER_VIEW;
   resource->base.usage = PIPE_USAGE_DEFAULT;
   resource->base.reference.count = 1;
   resource->base.screen = screen;
   resource->data = data;
   resource->size = resource->allocation_size = (size_t)stride * height;
   resource->stride = stride;
   resource->level_stride[0] = stride;
   resource->level_offset[0] = 0;
   resource->layer_stride = resource->size;
   resource->direct_start = -1;
   resource->stencil_direct_start = -1;
   resource->kodi_foreign_memory = true;
   return &resource->base;
}
""", "append"),
    (EGL, "struct ps5_egl_config {\n",
     """/* KODI-PS5: EGL images over existing memory (zero-copy video) */
static bool ps5_kodi_validate_egl_image(struct pipe_frontend_screen *fscreen, void *image);
static bool ps5_kodi_get_egl_image(struct pipe_frontend_screen *fscreen, void *image,
                                   struct st_egl_image *out);

""", "before"),
    (EGL, "      ps5_display.frontend.set_background_context = ps5_background_context;\n",
     """      ps5_display.frontend.validate_egl_image = ps5_kodi_validate_egl_image; /* KODI-PS5 */
      ps5_display.frontend.get_egl_image = ps5_kodi_get_egl_image;
""", "after"),
    (EGL, None, """
/* KODI-PS5: EGL images over existing memory, for Kodi's zero-copy video. The
 * image handle is passed to glEGLImageTargetTexture2DOES; Mesa asks these
 * hooks for the resource behind it. */
#define PS5_KODI_IMAGE_MAGIC 0x4b4f4449u
struct ps5_kodi_image {
   uint32_t magic;
   struct pipe_resource *resource;
};
struct pipe_resource *ps5_kodi_resource_from_memory(struct pipe_screen *screen,
                                                    enum pipe_format format,
                                                    unsigned width, unsigned height,
                                                    unsigned stride, void *data);

static bool
ps5_kodi_validate_egl_image(struct pipe_frontend_screen *fscreen, void *image)
{
   (void)fscreen;
   const struct ps5_kodi_image *img = image;
   return img && img->magic == PS5_KODI_IMAGE_MAGIC && img->resource;
}

static bool
ps5_kodi_get_egl_image(struct pipe_frontend_screen *fscreen, void *image,
                       struct st_egl_image *out)
{
   if (!ps5_kodi_validate_egl_image(fscreen, image))
      return false;
   const struct ps5_kodi_image *img = image;
   memset(out, 0, sizeof(*out));
   pipe_resource_reference(&out->texture, img->resource);
   out->format = img->resource->format;
   out->level = 0;
   out->layer = 0;
   return true;
}

/* components 1 or 2, bytes per component 1 or 2: R8, RG8, R16, RG16 */
void *ps5_opengl_memory_image_create(void *data, unsigned width, unsigned height,
                                     unsigned stride, unsigned components,
                                     unsigned bytes_per_component);
void *
ps5_opengl_memory_image_create(void *data, unsigned width, unsigned height, unsigned stride,
                               unsigned components, unsigned bytes_per_component)
{
   enum pipe_format format = PIPE_FORMAT_NONE;
   if (components == 1 && bytes_per_component == 1)
      format = PIPE_FORMAT_R8_UNORM;
   else if (components == 2 && bytes_per_component == 1)
      format = PIPE_FORMAT_R8G8_UNORM;
   else if (components == 1 && bytes_per_component == 2)
      format = PIPE_FORMAT_R16_UNORM;
   else if (components == 2 && bytes_per_component == 2)
      format = PIPE_FORMAT_R16G16_UNORM;
   if (format == PIPE_FORMAT_NONE || !ps5_display.screen)
      return NULL;
   struct pipe_resource *resource = ps5_kodi_resource_from_memory(
      ps5_display.screen, format, width, height, stride, data);
   if (!resource)
      return NULL;
   struct ps5_kodi_image *img = calloc(1, sizeof(*img));
   if (!img) {
      pipe_resource_reference(&resource, NULL);
      return NULL;
   }
   img->magic = PS5_KODI_IMAGE_MAGIC;
   img->resource = resource;
   return img;
}

void ps5_opengl_memory_image_destroy(void *image);
void
ps5_opengl_memory_image_destroy(void *image)
{
   struct ps5_kodi_image *img = image;
   if (!img || img->magic != PS5_KODI_IMAGE_MAGIC)
      return;
   img->magic = 0;
   pipe_resource_reference(&img->resource, NULL);
   free(img);
}
""", "append"),
]

# Texts earlier versions inserted; removed when present.
# Earlier revisions of the HDR addition: the first wrote its printf's newline
# escape as a real line break (a compile error), the second had the escape
# right but no in-place attribute change. Both are removed before the current
# form is added.
HDR_PREVIOUS = """
/* KODI-PS5: switch the scanout buffers' pixel format (HDR output). Waits for
 * pending flips, unregisters buffer set 0 and registers the same two buffers
 * with the new format; if that is refused, the previous format is registered
 * again. Returns the registration result for the requested format. */
int ps5_opengl_set_scanout_format(uint64_t pixel_format);
#if defined(AGC_RUNTIME_PACKAGES) /* the runtime's video state lives in this build only */
int
ps5_opengl_set_scanout_format(uint64_t pixel_format)
{
    static uint64_t current = VIDEO_OUT_PIXEL_FORMAT;
    if (!runtime_video_registered || runtime_video_handle < 0 || !runtime_video_framebuffer ||
        !runtime_video_api.unregister_buffers || !runtime_video_api.register_buffers2 ||
        !runtime_video_api.set_attribute2)
        return -1;
    if (pixel_format == current)
        return 0;
    for (int i = 0; i < 200 && runtime_video_api.is_flip_pending &&
                    runtime_video_api.is_flip_pending(runtime_video_handle) > 0; ++i)
        sceKernelUsleep(1000);
    uint8_t *framebuffer = runtime_video_framebuffer;
    video_buffer_t buffers[2] = {
        {framebuffer, NULL, NULL, NULL},
        {framebuffer + (runtime_video_framebuffer_size >= FRAMEBUFFER_POOL_BYTES
                            ? FRAMEBUFFER_BYTES : 0),
         NULL, NULL, NULL}
    };
    const int unregister_rc = runtime_video_api.unregister_buffers(runtime_video_handle, 0);
    video_attribute_t attribute = {{0}};
    runtime_video_api.set_attribute2(&attribute, pixel_format, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     0, 0, 0);
    const int register_rc = runtime_video_api.register_buffers2(runtime_video_handle, 0, 0,
                                                                buffers, 2, &attribute, 0, NULL);
    int restore_rc = 0;
    if (register_rc != 0) {
        video_attribute_t previous = {{0}};
        runtime_video_api.set_attribute2(&previous, current, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                         0, 0, 0);
        restore_rc = runtime_video_api.register_buffers2(runtime_video_handle, 0, 0, buffers, 2,
                                                         &previous, 0, NULL);
    } else {
        current = pixel_format;
    }
    printf("[kodi-ps5] scanout format %016llx: unregister=%08x register=%08x restore=%08x\\n",
           (unsigned long long)pixel_format, (uint32_t)unregister_rc, (uint32_t)register_rc,
           (uint32_t)restore_rc);
    return register_rc;
}
#else
int
ps5_opengl_set_scanout_format(uint64_t pixel_format)
{
    (void)pixel_format;
    return -1;
}
#endif
"""
HDR_WITH_FALLBACK = """
/* KODI-PS5: switch the scanout buffers' pixel format (HDR output). First the
 * in-place way (sceVideoOutSubmitChangeBufferAttribute2 on buffer set 0, which
 * takes effect at the next flip); if that is refused, pending flips are
 * drained and the same two buffers are unregistered and registered again with
 * the new format, and if that is refused too, the previous format is
 * registered again. results[0..3] = change, unregister, register, restore
 * (0x7fffffff = not attempted). Returns 0 if the new format is in effect. */
int ps5_opengl_set_scanout_format(uint64_t pixel_format, int32_t results[4]);
#if defined(AGC_RUNTIME_PACKAGES) /* the runtime's video state lives in this build only */
int sceVideoOutSubmitChangeBufferAttribute2(int32_t handle, int32_t set_index,
                                            const void *attribute, int32_t category,
                                            void *option);
int
ps5_opengl_set_scanout_format(uint64_t pixel_format, int32_t results[4])
{
    static uint64_t current = VIDEO_OUT_PIXEL_FORMAT;
    for (int i = 0; i < 4; ++i)
        results[i] = 0x7fffffff;
    if (!runtime_video_registered || runtime_video_handle < 0 || !runtime_video_framebuffer ||
        !runtime_video_api.unregister_buffers || !runtime_video_api.register_buffers2 ||
        !runtime_video_api.set_attribute2)
        return -1;
    if (pixel_format == current)
        return 0;
    video_attribute_t attribute = {{0}};
    runtime_video_api.set_attribute2(&attribute, pixel_format, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     0, 0, 0);
    results[0] = sceVideoOutSubmitChangeBufferAttribute2(runtime_video_handle, 0, &attribute, 0,
                                                         NULL);
    if (results[0] == 0) {
        current = pixel_format;
        return 0;
    }
    /* drain pending flips the way ProsperoLight does before unregistering */
    for (unsigned waits = 0; waits < 120 && runtime_video_api.is_flip_pending &&
                             runtime_video_api.is_flip_pending(runtime_video_handle) > 0; ++waits)
        runtime_video_api.wait_vblank(runtime_video_handle);
    uint8_t *framebuffer = runtime_video_framebuffer;
    video_buffer_t buffers[2] = {
        {framebuffer, NULL, NULL, NULL},
        {framebuffer + (runtime_video_framebuffer_size >= FRAMEBUFFER_POOL_BYTES
                            ? FRAMEBUFFER_BYTES : 0),
         NULL, NULL, NULL}
    };
    results[1] = runtime_video_api.unregister_buffers(runtime_video_handle, 0);
    results[2] = runtime_video_api.register_buffers2(runtime_video_handle, 0, 0, buffers, 2,
                                                     &attribute, 0, NULL);
    if (results[2] == 0) {
        current = pixel_format;
        return 0;
    }
    video_attribute_t previous = {{0}};
    runtime_video_api.set_attribute2(&previous, current, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     0, 0, 0);
    results[3] = runtime_video_api.register_buffers2(runtime_video_handle, 0, 0, buffers, 2,
                                                     &previous, 0, NULL);
    return results[2];
}
#else
int
ps5_opengl_set_scanout_format(uint64_t pixel_format, int32_t results[4])
{
    (void)pixel_format;
    for (int i = 0; i < 4; ++i)
        results[i] = 0x7fffffff;
    return -1;
}
#endif
"""
HDR_BROKEN = [(RUNTIME, HDR_WITH_FALLBACK),
              (RUNTIME, HDR_PREVIOUS),
              (RUNTIME, HDR_PREVIOUS.replace('restore=%08x' + chr(92) + 'n",', 'restore=%08x' + chr(10) + '",'))]

REMOVED = [
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which clear path each clear takes (GPU depth, CPU depth, GPU\n * color, CPU color), reported with the profile. */\nstatic uint64_t ps5_kodi_clear_paths[4];\n#define PS5_KODI_COUNT_CLEAR(path) \\\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED)\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which path each clear takes; reported every 1000 clears. */\nstatic uint64_t ps5_kodi_clear_paths[4];\nstatic void ps5_kodi_count_clear(unsigned path) __attribute__((unused));\nstatic void ps5_kodi_count_clear(unsigned path)\n{\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED);\n   uint64_t total = 0;\n   for (unsigned i = 0; i < 4; ++i)\n      total += __atomic_load_n(&ps5_kodi_clear_paths[i], __ATOMIC_RELAXED);\n   if (total % 1000 == 0)\n      printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64\n             " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",\n             ps5_kodi_clear_paths[0], ps5_kodi_clear_paths[1],\n             ps5_kodi_clear_paths[2], ps5_kodi_clear_paths[3]);\n}\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\n#include <unistd.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   /* only with Kodi\'s kodi-debug switch in the title folder */\n   static int enabled = -1;\n   if (enabled < 0)\n      enabled = access("/app0/kodi-debug", F_OK) == 0;\n   if (!enabled)\n      return 0;\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which path each clear takes; reported every 1000 clears. */\nstatic uint64_t ps5_kodi_clear_paths[4];\nstatic void ps5_kodi_count_clear(unsigned path) __attribute__((unused));\nstatic void ps5_kodi_count_clear(unsigned path)\n{\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED);\n   uint64_t total = 0;\n   for (unsigned i = 0; i < 4; ++i)\n      total += __atomic_load_n(&ps5_kodi_clear_paths[i], __ATOMIC_RELAXED);\n   if (total % 1000 == 0)\n      printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64\n             " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",\n             ps5_kodi_clear_paths[0], ps5_kodi_clear_paths[1],\n             ps5_kodi_clear_paths[2], ps5_kodi_clear_paths[3]);\n}\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\n#include <stdlib.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   /* only with Kodi\'s kodi-debug switch (read by Kodi at start-up) */\n   static int enabled = -1;\n   if (enabled < 0)\n      enabled = getenv("KODI_PS5_DEBUG") != NULL;\n   if (!enabled)\n      return 0;\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which path each clear takes; reported every 1000 clears. */\nstatic uint64_t ps5_kodi_clear_paths[4];\nstatic void ps5_kodi_count_clear(unsigned path) __attribute__((unused));\nstatic void ps5_kodi_count_clear(unsigned path)\n{\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED);\n   uint64_t total = 0;\n   for (unsigned i = 0; i < 4; ++i)\n      total += __atomic_load_n(&ps5_kodi_clear_paths[i], __ATOMIC_RELAXED);\n   if (total % 1000 == 0)\n      printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64\n             " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",\n             ps5_kodi_clear_paths[0], ps5_kodi_clear_paths[1],\n             ps5_kodi_clear_paths[2], ps5_kodi_clear_paths[3]);\n}\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n         ps5_kodi_count_clear(0); /* KODI-PS5: GPU depth/stencil clear */\n#endif\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n         ps5_kodi_count_clear(1); /* KODI-PS5: CPU depth/stencil clear */\n#endif\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n      ps5_kodi_count_clear(2); /* KODI-PS5: GPU color clear */\n#endif\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n   if (buffers & PIPE_CLEAR_COLOR)\n      ps5_kodi_count_clear(3); /* KODI-PS5: CPU color clear */\n#endif\n'),
]

for rel, text in REMOVED + HDR_BROKEN:
    path = root / rel
    source = path.read_text()
    if text in source:
        path.write_text(source.replace(text, "", 1))
        print(f"  {rel}: removed an earlier Kodi diagnostic ({text.strip().splitlines()[0][:50]})")

failed = False
for rel, anchor, text in ADDITIONS:
    path = root / rel
    source = path.read_text()
    if text.strip() in source:
        print(f"  {rel}: already applied (video out handle export)")
        continue
    if source.count(anchor) != 1:
        print(f"!! {rel}: anchor not found exactly once: {anchor.strip()[:80]}")
        failed = True
        continue
    path.write_text(source.replace(anchor, anchor + text, 1))
    print(f"  {rel}: applied (video out handle export)")

for rel, anchor, text, where in HDR + ZERO_COPY:
    path = root / rel
    source = path.read_text()
    label = text.strip().splitlines()[0][:64]
    if text.strip() in source:
        print(f"  {rel}: already applied ({label})")
        continue
    if where == "append":
        path.write_text(source.rstrip("\n") + "\n" + text)
        print(f"  {rel}: applied ({label})")
        continue
    if source.count(anchor) != 1:
        print(f"  {rel}: skipped, anchor differs in this revision ({label}) - zero-copy unavailable")
        continue
    replacement = anchor + text if where == "after" else text + anchor
    path.write_text(source.replace(anchor, replacement, 1))
    print(f"  {rel}: applied ({label})")

leftover = [t for rel, t in REMOVED if t in (root / rel).read_text()]
if "KODI-PS5: profiler" in (root / SCREEN).read_text() or leftover:
    print("!! ps5_screen.c still carries Kodi diagnostics that no known version matches")
    failed = True
sys.exit(1 if failed else 0)
