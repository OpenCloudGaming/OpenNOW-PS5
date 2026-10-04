#!/usr/bin/env bash
export PS5_PAYLOAD_SDK=/work/.deps/native/ps5-payload-sdk
export PS5_CLANG=/usr/bin/clang-18
exec sh /work/tooling/prospero-clang18 "$@"
