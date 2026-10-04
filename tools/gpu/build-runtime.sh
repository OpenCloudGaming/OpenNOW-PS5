#!/usr/bin/env bash
# Run in the VPS GPU builder. SOURCE and BUNDLE contain the unpacked,
# SHA-256 verified SDK 1.0.0 sources and distribution respectively.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
source_root=${1:?Usage: build-runtime.sh SOURCE BUNDLE}
bundle=${2:?Usage: build-runtime.sh SOURCE BUNDLE}
export PS5_PAYLOAD_SDK="$root/.deps/native/ps5-payload-sdk"
export PATH="$PS5_PAYLOAD_SDK/bin:$PATH"
export PS5_CCACHE=0
[[ $(cat "$source_root/third_party/mesa-26.2.0/VERSION") == 26.2.0 ]]
python3 "$root/tools/gpu/kodi-additions.py" "$source_root"
# The SDK tar snapshots omit generated PSBC headers and its PS5 patch.
if ! rg -q 'is_indexed_draw_valid' "$source_root/third_party/opengnm-psbc/libpsbc/psbc_compile.h" 2>/dev/null; then
 if ! grep -q 'is_indexed_draw_valid' "$source_root/third_party/opengnm-psbc/libpsbc/psbc_compile.h"; then
  (cd "$source_root/third_party/opengnm-psbc" && patch -p1 < "$source_root/toolchain/opengnm-psbc-ps5.patch")
 fi
fi
cp "$bundle/sdk/lib/libpsbc.ps5.a" "$source_root/third_party/opengnm-psbc/libpsbc.ps5.a"
make -C "$source_root/third_party/opengnm-psbc" -f "$source_root/toolchain/Makefile.opengnm-psbc-ps5" -j2 generated
cross="$source_root/build/opennow-cross.ini"
mkdir -p "$(dirname "$cross")"
cat > "$cross" <<INI
[binaries]
c = '$PS5_PAYLOAD_SDK/bin/prospero-clang'
cpp = '$PS5_PAYLOAD_SDK/bin/prospero-clang++'
ar = '$PS5_PAYLOAD_SDK/bin/prospero-ar'
strip = '$PS5_PAYLOAD_SDK/bin/prospero-strip'
[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
[built-in options]
default_library = 'static'
[properties]
needs_exe_wrapper = true
INI
export PS5_MESA_CROSS_FILE="$cross"
bash "$source_root/toolchain/build-mesa-ps5.sh"
make -C "$source_root/tests/ps5" -f native-app.mk -j2 runtime PS5_SCANOUT_HEIGHT=2160 PS5_SCANOUT_FPS=120 PS5_DYNAMIC_SCANOUT=1 PS5_DRAW_PROFILE=0 PS5_RUNTIME_QUIET=1 \
 PS5_OPENGL_PSBC_CFLAGS="-DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 -Wno-error=unused-parameter -Wno-error=missing-field-initializers -iquote$source_root/third_party/opengnm-psbc/src -I$source_root/third_party/opengnm-psbc/src -I$source_root/third_party/opengnm-psbc/libpsbc -I$source_root/build/mesa-ps5-probe/src/compiler -I$source_root/build/mesa-ps5-probe/src/compiler/nir"
mkdir -p "$root/.deps/gpu"
cp -a "$bundle/sdk" "$root/.deps/gpu/"
cp "$source_root/build/core33-native-runtime/libps5_opengl_core33.a" "$root/.deps/gpu/sdk/lib/"
# Materialize Meson's thin archives; their object paths cannot be relocated.
while IFS= read -r library; do
 target="$root/.deps/gpu/sdk/lib/$(basename "$library")"
 [[ -f $target ]] || continue
 printf 'CREATE %s\nADDLIB %s\nSAVE\nEND\n' "$target.tmp" "$library" | "$PS5_PAYLOAD_SDK/bin/prospero-ar" -M
 mv "$target.tmp" "$target"
done < <(find "$source_root/build/mesa-ps5-probe" -name '*.a' -type f)
(cd "$root/.deps/gpu/sdk" && find lib -type f -exec sha256sum {} + > OPENNOW_SHA256SUMS)
