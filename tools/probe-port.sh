#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
${CC:-cc} -std=c11 -O2 -Wno-deprecated-declarations -c src/vendor/cJSON.c -o build/host-tests/cJSON-probe.o
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -Isrc tests/probe.cpp src/gfn.cpp src/cloud.cpp src/http.cpp build/host-tests/cJSON-probe.o -lcurl -o build/host-tests/gfn-probe
build/host-tests/gfn-probe
