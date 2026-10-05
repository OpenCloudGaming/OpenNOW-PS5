#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests build/preview build/preview-art
${CC:-cc} -O2 -c src/vendor/qrcodegen.c -o build/host-tests/qrcode.o
link_flags=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then link_flags=(-Wl,-dead_strip); fi
${CC:-cc} -O2 -c src/vendor/cJSON.c -o build/host-tests/preview-cjson.o
${CXX:-c++} -std=c++20 -O2 -ffunction-sections -fdata-sections -DOPENNOW_HOST_PREVIEW -Isrc "${link_flags[@]}" \
    src/main.cpp src/demo_renderer.cpp src/ui/font.cpp src/ui/tv_ui.cpp src/ui/artwork.cpp src/cloud.cpp src/gfn.cpp \
    tests/preview_stubs.cpp build/host-tests/qrcode.o build/host-tests/preview-cjson.o -o build/host-tests/preview
covers=(
    101611411/ZZ/GAME_BOX_ART_01_1f4d5087-b764-4dc8-9b23-5a3e42d8fd4e.jpg 101611411/ZZ/HERO_IMAGE_01_7b84d3a8-1e36-4f45-9f82-11de22a0105f.jpg
    101238711/ZZ/GAME_BOX_ART_01_967e038e-78ce-45bf-9d89-adb8c7c7abb1.jpg 101238711/ZZ/HERO_IMAGE_01_652bc3b4-bf4a-4531-bdc2-d95acee16d15.jpg
    100886511/ZZ/GAME_BOX_ART_01_69d32e41-2ca2-4bde-bc17-dc819da911dd.jpg 100886511/ZZ/HERO_IMAGE_01_505d79e5-ac1d-456a-8909-4d176179aa63.jpg
    102328711/ZZ/GAME_BOX_ART_01_f28bd852-eb9a-4ea6-906d-285b080fb9d6.jpg 102328711/ZZ/HERO_IMAGE_01_1d900eb6-5732-4132-96c8-b0cfa5c8fdb7.jpg
)
for path in "${covers[@]}"; do
    width=272; [[ $path == *HERO_IMAGE* ]] && width=960
    url="https://img.nvidiagrid.net/apps/$path;f=jpg;w=$width"
    file="build/preview-art/$(printf '%s' "$path;f=jpg;w=$width" | tr '/;=' '___')"
    [[ -s $file ]] || curl -fsS --max-time 30 -o "$file" "$url" || rm -f "$file"
done
scenes=(signin-idle signin-requesting signin-code signin-failed library library-art-loading library-loading library-scanning library-empty library-error
    library-page-error browse browse-row2 browse-long browse-loading-more search detail detail-hardware settings
    settings-stream settings-stream-custom settings-stream-software settings-stream-editor settings-stream-unqualified settings-stream-fixed
    settings-save-error settings-display settings-cache settings-cache-clear settings-cache-off settings-account settings-signout settings-about starting
    queued connecting keyboard keyboard-modifiers keyboard-waiting cleanup-failed stream-ended launch-failed)
for scene in "${scenes[@]}"; do
    rm -rf build/preview-cache
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
