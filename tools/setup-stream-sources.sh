#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .deps
revision=8069d3e535c2aaaac0488bd5b456809c6493282f
archive=.deps/opennow-switch-source.tar.gz
if [[ ! -f $archive ]]; then
 curl -fsSL "https://codeload.github.com/OpenCloudGaming/OpenNOW-Switch/tar.gz/$revision" -o "$archive"
fi
echo "ec568fcfba57370c3f8a1ce0508ad651a384c2aa8e6c41d238224dd72ec69cff  $archive" | sha256sum -c -
if [[ ! -f .deps/stream-sources/mbedtls/CMakeLists.txt ]]; then
 mkdir -p .deps/stream-sources
 tar -xzf "$archive" -C .deps/stream-sources --strip-components=4 "OpenNOW-Switch-$revision/extern/libpeer/third_party/mbedtls" "OpenNOW-Switch-$revision/extern/libpeer/third_party/libsrtp" "OpenNOW-Switch-$revision/extern/libpeer/third_party/usrsctp"
fi
