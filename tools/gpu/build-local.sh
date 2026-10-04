#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/../.."
docker build --provenance=false -t opennow-ps5-builder .
docker build --provenance=false -t opennow-ps5-gpu-builder -f tools/gpu/Dockerfile .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" opennow-ps5-gpu-builder bash -c '
    set -euo pipefail
    bash tools/setup-native-dependencies.sh
    bash tools/setup-stream-sources.sh
    bash tools/build-stream-deps.sh
    bash tools/build-peer.sh
    bash tools/gpu/setup-sources.sh
    version=$(python3 -c '\''import json; print(json.load(open("upstream-lock.json"))["ps5-opengl-SDK-version"])'\'')
    bash tools/gpu/build-runtime.sh /work/.deps/gpu/source "/work/.deps/gpu/ps5-opengl-sdk-$version"
    bash tools/gpu/build-app.sh
    python3 tools/package-manifest.py
'
