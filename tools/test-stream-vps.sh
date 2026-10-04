#!/usr/bin/env bash
set -euo pipefail
cd /work
cmake -S .deps/stream-sources/mbedtls -B build/host-mbedtls -DCMAKE_C_COMPILER=/usr/bin/clang-18 -DGEN_FILES=ON -DMBEDTLS_FATAL_WARNINGS=OFF -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/host-mbedtls --target mbedcrypto -j2 >/dev/null
clang++-18 -std=c++20 -Wall -Wextra -Werror -Isrc/stream -I.deps/stream-sources/mbedtls/include tests/websocket_handshake_test.cpp build/host-mbedtls/library/libmbedcrypto.a -o build/websocket-handshake-test
build/websocket-handshake-test
printf 'WebSocket handshake validation passed\n'
