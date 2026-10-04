#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
remote=/home/ubuntu/opennow-ps5
# Docker stays on the VPS. build-runtime.sh documents the SDK source rebuild.
rsync -az --exclude=.git --exclude=.deps --exclude=build --exclude=dist --exclude=runtime "$root/" "vps:$remote/"
ssh vps "cd '$remote' && sudo -n docker build -t opennow-ps5-gpu-builder -f tools/gpu/Dockerfile . && sudo -n docker run --rm -v '$remote:/work' opennow-ps5-gpu-builder bash -c 'cd /work; test -f .deps/gpu/sdk/lib/libPS5OpenGL.a; bash tools/build-peer.sh && bash tools/gpu/build-app.sh'"
rsync -az "vps:$remote/dist/" "$root/dist/"
python3 "$root/tools/package-manifest.py"
