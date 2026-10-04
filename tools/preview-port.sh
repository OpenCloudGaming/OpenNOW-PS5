#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
${CC:-cc} -O2 -c src/vendor/qrcodegen.c -o build/host-tests/qrcode.o
${CXX:-c++} -std=c++20 -O2 -DOPENNOW_HOST_PREVIEW -Isrc -Wl,-dead_strip src/main.cpp src/demo_renderer.cpp tests/preview_stubs.cpp build/host-tests/qrcode.o -o build/host-tests/preview
build/host-tests/preview
sips -s format png build/preview.ppm --out build/preview.png
