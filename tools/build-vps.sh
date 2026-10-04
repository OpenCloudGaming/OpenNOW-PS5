#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
# This script never invokes Docker on the local workstation.
remote=/home/ubuntu/opennow-ps5
ssh -o BatchMode=yes -o ConnectTimeout=10 vps "mkdir -p '$remote'"
rsync -az --exclude=.git --exclude=.deps --exclude=build --exclude=dist --exclude=runtime "$root/" "vps:$remote/"
ssh vps "cd '$remote' && sudo -n docker build -t opennow-ps5-builder . && sudo -n docker run --rm -v '$remote:/work' opennow-ps5-builder bash -c 'bash tools/setup-stream-sources.sh && if [ ! -f .deps/stream/lib/libusrsctp.a ]; then bash tools/build-stream-deps.sh; fi && bash tools/build-peer.sh && make app && python3 tools/audit-imports.py'"
mkdir -p "$root/dist"
rsync -az "vps:$remote/dist/" "$root/dist/"
python3 "$root/tools/package-manifest.py"
