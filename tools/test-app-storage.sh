#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -c src/platform/network_diagnostics.c -o build/host-tests/network-diagnostics.o
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DOPENNOW_PS5=1 -Isrc tests/app_storage_test.cpp build/host-tests/network-diagnostics.o -o build/host-tests/app-storage-test
build/host-tests/app-storage-test
