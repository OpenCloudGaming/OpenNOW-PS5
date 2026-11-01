// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opennow::forwarder::tileManifest {
enum Index : unsigned { program, runtime, parameters, icon, background0, background1, forwarder, count };
struct FileSpec {
    const char* path;
    const char* name;
    unsigned parent;
    std::uint64_t limit;
    unsigned mode;
    bool exactSize;
};
// Preserve the existing wire order. Parent indices are root=0, sce_sys=1,
// sce_module=2; the store and upload protocol share these paths and limits.
inline constexpr FileSpec files[]={
    {"eboot.bin","eboot.bin",0,2*1024*1024,0755,false},
    {"sce_module/libc.prx","libc.prx",2,2*1024*1024,0755,false},
    {"sce_sys/param.json","param.json",1,16383,0644,false},
    {"sce_sys/icon0.png","icon0.png",1,2*1024*1024,0644,false},
    {"sce_sys/pic0.dds","pic0.dds",1,3840*2160+148,0644,true},
    {"sce_sys/pic1.dds","pic1.dds",1,3840*2160+148,0644,true},
    {"forwarder.json","forwarder.json",0,16383,0644,false}
};
static_assert(std::size(files)==count);
}
