#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/../.."
version=$(python3 -c 'import json; print(json.load(open("upstream-lock.json"))["ps5-opengl-SDK-version"])')
digest=$(python3 -c 'import json; print(json.load(open("upstream-lock.json"))["ps5-opengl-SDK-archive-sha256"])')
cache="$PWD/.deps/gpu"
archive="$cache/ps5-opengl-sdk-$version.tar.gz"
bundle="$cache/ps5-opengl-sdk-$version"
source_root="$cache/source"
mkdir -p "$cache"
if [[ ! -f "$archive" ]]; then
    curl -fL "https://github.com/blackbearreloaded/ps5-opengl/releases/download/v$version/ps5-opengl-sdk-$version.tar.gz" -o "$archive.download"
    printf '%s  %s\n' "$digest" "$archive.download" | sha256sum --check --strict
    mv "$archive.download" "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha256sum --check --strict
if [[ ! -d "$bundle" ]]; then
    tar -xzf "$archive" -C "$cache"
fi
(cd "$bundle" && sha256sum --check --strict SHA256SUMS)
if [[ ! -f "$source_root/.opennow-source-digest" ]] || [[ $(cat "$source_root/.opennow-source-digest") != "$digest" ]]; then
    rm -rf "$source_root"
    mkdir -p "$source_root"
    tar -xf "$bundle/sources/ps5-opengl.tar" -C "$source_root" --strip-components=1
    for component in opengnm-psbc SPIRV-Headers; do
        mkdir -p "$source_root/third_party/$component"
        tar -xf "$bundle/sources/$component.tar" -C "$source_root/third_party/$component" --strip-components=1
    done
    tar -xf "$bundle/sources/mesa-26.2.0.tar.xz" -C "$source_root/third_party"
    cp "$bundle/sources/mesa-26.2.0.tar.xz" "$source_root/third_party/"
    printf '%s\n' "$digest" > "$source_root/.opennow-source-digest"
fi
