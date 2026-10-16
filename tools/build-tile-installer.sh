#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -z ${PS5_PAYLOAD_SDK:-} ]]; then
    bash tools/setup-native-dependencies.sh >/dev/null
fi
sdk=${PS5_PAYLOAD_SDK:-"$PWD/.deps/native/ps5-payload-sdk"}
output="$PWD/build/forwarder/opennow-tile-installer.elf"
objects="$PWD/build/tile-installer"
mkdir -p "$objects" "$(dirname "$output")"
common=(-Os -g0 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DOPENNOW_PS5=1 -Isrc)
PS5_PAYLOAD_SDK="$sdk" "$sdk/bin/prospero-clang" -std=c11 "${common[@]}" -Wno-unreachable-code-generic-assoc \
    -c src/vendor/cJSON.c -o "$objects/cJSON.o"
PS5_PAYLOAD_SDK="$sdk" "$sdk/bin/prospero-clang++" -std=c++20 "${common[@]}" \
    -fno-exceptions -fno-rtti -Wl,--build-id=none -Wl,--gc-sections \
    tooling/tile_installer/main.cpp src/forwarder/tile_store.cpp \
    src/forwarder/launch_args.cpp "$objects/cJSON.o" -o "$output"
python3 tools/validate-elevation-helper.py "$output"
