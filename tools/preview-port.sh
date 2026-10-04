#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
${CC:-cc} -O2 -c src/vendor/qrcodegen.c -o build/host-tests/qrcode.o
link_flags=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then link_flags=(-Wl,-dead_strip); fi
${CXX:-c++} -std=c++20 -O2 -ffunction-sections -fdata-sections -DOPENNOW_HOST_PREVIEW -Isrc "${link_flags[@]}" src/main.cpp src/demo_renderer.cpp tests/preview_stubs.cpp build/host-tests/qrcode.o -o build/host-tests/preview
build/host-tests/preview
if command -v sips >/dev/null; then
    sips -s format png build/preview.ppm --out build/preview.png
else
    python3 - <<'PY'
from PIL import Image
with Image.open("build/preview.ppm") as image:
    image.save("build/preview.png")
PY
fi
