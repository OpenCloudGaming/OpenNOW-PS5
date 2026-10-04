#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash tools/setup-stream-sources.sh
mkdir -p build/host-tests/ws-crypto
source=.deps/stream-sources/mbedtls
flags=(-O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections)
link_flags=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then link_flags=(-Wl,-dead_strip); fi
for name in sha1 base64 platform_util constant_time; do
    ${CC:-cc} -std=c11 "${flags[@]}" -I"$source/include" -I"$source/library" \
        -c "$source/library/$name.c" -o "build/host-tests/ws-crypto/$name.o"
done
for name in websocket_handshake websocket_client; do
    sources=("tests/${name}_test.cpp")
    libraries=()
    if [[ $name == websocket_client ]]; then
        sources+=(src/stream/WebSocketClient.cpp)
        libraries+=(-lcurl)
    fi
    ${CXX:-c++} -std=c++20 -Wall -Wextra -Werror "${flags[@]}" "${link_flags[@]}" \
        -Isrc -Isrc/stream -I"$source/include" "${sources[@]}" \
        build/host-tests/ws-crypto/*.o "${libraries[@]}" -o "build/host-tests/$name-test"
done
build/host-tests/websocket_handshake-test
UBSAN_OPTIONS=halt_on_error=1 python3 tests/websocket_client_test.py build/host-tests/websocket_client-test
