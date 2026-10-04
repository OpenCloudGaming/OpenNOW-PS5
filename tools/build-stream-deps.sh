#!/usr/bin/env bash
set -euo pipefail
cd /work
prefix=/work/.deps/stream
source=/work/.deps/stream-sources
mkdir -p "$prefix"
# Secure entropy is supplied at runtime via mbedtls_hardware_poll.
python3 - <<'PY'
from pathlib import Path
p=Path('.deps/stream-sources/mbedtls/include/mbedtls/mbedtls_config.h')
s=p.read_text().split('/* OpenNOW Switch mbedtls overrides */')[0].split('/* OpenNOW PS5 overrides */')[0]
s=s.replace('//#define MBEDTLS_SSL_DTLS_SRTP','#define MBEDTLS_SSL_DTLS_SRTP')
s+='\n/* OpenNOW PS5 overrides */\n#define MBEDTLS_NO_PLATFORM_ENTROPY\n#define MBEDTLS_ENTROPY_HARDWARE_ALT\n#undef MBEDTLS_NET_C\n#undef MBEDTLS_TIMING_C\n'
p.write_text(s)
for name in ('user_malloc.h','user_socketvar.h'):
 p=Path('.deps/stream-sources/usrsctp/usrsctplib')/name
 p.write_text(p.read_text().replace('#define __SWITCH__ 1','/* PS5 uses the FreeBSD socket ABI. */'))
PY
common=(-DCMAKE_TOOLCHAIN_FILE=/work/tools/stream-toolchain.cmake -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF)
cmake -S "$source/mbedtls" -B build/stream-mbedtls "${common[@]}" -DMBEDTLS_FATAL_WARNINGS=OFF -DGEN_FILES=ON -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF
cmake --build build/stream-mbedtls -j2
cmake --install build/stream-mbedtls
cmake -S "$source/libsrtp" -B build/stream-srtp "${common[@]}" -DTEST_APPS=OFF -DENABLE_OPENSSL=OFF
cmake --build build/stream-srtp -j2
cmake --install build/stream-srtp
cmake -S "$source/usrsctp" -B build/stream-sctp "${common[@]}" -Dsctp_build_programs=OFF -Dsctp_build_shared_lib=OFF -Dsctp_debug=OFF -Dsctp_werror=OFF
cmake --build build/stream-sctp -j2
cmake --install build/stream-sctp
