#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tools/setup-native-dependencies.sh >/dev/null
sdk="$PWD/.deps/native/ps5-payload-sdk"
export PS5_PAYLOAD_SDK="$sdk"
mkdir -p .deps/gpu
"$sdk/bin/prospero-ar" p "$sdk/target/lib/libc.a" emutls.o > .deps/gpu/emutls.o
"$sdk/bin/prospero-ar" rcs .deps/gpu/libemutls.a .deps/gpu/emutls.o
sh tooling/prospero-clang18 -std=c11 -O2 -fPIC -c tools/gpu/videodec2_link_stub.c -o .deps/gpu/videodec2-link-stub.o
"$sdk/bin/prospero-lld" --shared -soname libSceVideodec2.prx -o .deps/gpu/libSceVideodec2.so .deps/gpu/videodec2-link-stub.o
make app APP_DEFINITIONS='OPENNOW_PS5=1 OPENNOW_GPU=1 GL_GLEXT_PROTOTYPES=1' \
 APP_INCLUDE_PATHS='.deps/stream/include vendor/libpeer/src .deps/gpu/sdk/include' \
 APP_STATIC_ARCHIVES='.deps/stream/lib/libpeer.a .deps/stream/lib/libsrtp2.a .deps/stream/lib/libusrsctp.a .deps/stream/lib/libmbedtls.a .deps/stream/lib/libmbedx509.a .deps/stream/lib/libmbedcrypto.a .deps/gpu/sdk/lib/libPS5OpenGL.a .deps/gpu/libemutls.a' \
 APP_IMPORT_STUBS='.deps/gpu/sdk/lib/libSceAgc.so .deps/gpu/sdk/lib/libSceAgcDriver.so .deps/gpu/libSceVideodec2.so'
python3 tools/audit-imports.py
