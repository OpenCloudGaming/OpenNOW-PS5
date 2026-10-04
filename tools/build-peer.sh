#!/usr/bin/env bash
set -euo pipefail
cd /work
mkdir -p build/peer .deps/stream/lib
for source in vendor/libpeer/src/*.c; do
 case "$source" in */peer_signaling.c|*/ssl_transport.c|*/mdns.c) continue;; esac
 tools/stream-cc.sh -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections -DOPENNOW_PS5=1 -DDISABLE_PEER_SIGNALING=1 -DCONFIG_USE_USRSCTP=1 -DLOG_LEVEL=-1 -I.deps/stream/include -Ivendor/libpeer/src -Isrc/vendor -c "$source" -o "build/peer/$(basename "$source" .c).o"
done
llvm-ar-18 rcs .deps/stream/lib/libpeer.a build/peer/*.o
