#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests build/preview
${CC:-cc} -O2 -c src/vendor/qrcodegen.c -o build/host-tests/qrcode.o
link_flags=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then link_flags=(-Wl,-dead_strip); fi
${CXX:-c++} -std=c++20 -O2 -ffunction-sections -fdata-sections -DOPENNOW_HOST_PREVIEW -Isrc "${link_flags[@]}" \
    src/main.cpp src/demo_renderer.cpp src/ui/font.cpp src/ui/tv_ui.cpp tests/preview_stubs.cpp \
    build/host-tests/qrcode.o -o build/host-tests/preview
scenes=(signin-idle signin-requesting signin-code signin-failed loading library library-row2 library-long
    empty catalog-error search detail detail-hardware starting queued preparing connecting cleanup-failed
    stream-ended)
for scene in "${scenes[@]}"; do
    OPENNOW_PREVIEW_SCENE=$scene build/host-tests/preview
    if command -v sips >/dev/null; then
        sips -s format png build/preview.ppm --out "build/preview/$scene.png" >/dev/null
    else
        python3 - "$scene" <<'PY'
import sys
from PIL import Image
with Image.open("build/preview.ppm") as image:
    image.save(f"build/preview/{sys.argv[1]}.png")
PY
    fi
done
cp build/preview/library.png build/preview.png
echo "Rendered ${#scenes[@]} scenes into build/preview/"
