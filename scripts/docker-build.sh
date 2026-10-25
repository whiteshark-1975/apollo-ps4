#!/usr/bin/env bash
# Inside-container build (mirrors .github/workflows/build.yml).
set -euo pipefail

export OO_PS4_TOOLCHAIN="${OO_PS4_TOOLCHAIN:-/opt/OpenOrbis/PS4Toolchain}"
export PATH="/usr/lib/llvm-12/bin:/usr/bin:${PATH}"
# OO build_rules.mk expects bare llvm-ar / clang on PATH
command -v llvm-ar >/dev/null
command -v clang >/dev/null

echo "==> Open Orbis toolchain"
mkdir -p /opt/OpenOrbis
curl -sL https://github.com/illusion0001/OpenOrbis-PS4-Toolchain/releases/download/0.0.1.416/toolchain.tar.gz \
  | tar xz -C /opt
# tarball lays down OpenOrbis/PS4Toolchain under /opt
test -d "$OO_PS4_TOOLCHAIN"
cp /deps/oosdk_libraries/build_rules.mk "$OO_PS4_TOOLCHAIN/build_rules.mk"
curl -sL https://raw.githubusercontent.com/OpenOrbis/OpenOrbis-PS4-Toolchain/master/include/orbis/UserService.h \
  -o "$OO_PS4_TOOLCHAIN/include/orbis/UserService.h"
curl -sL https://raw.githubusercontent.com/OpenOrbis/OpenOrbis-PS4-Toolchain/master/include/orbis/_types/user.h \
  -o "$OO_PS4_TOOLCHAIN/include/orbis/_types/user.h"

echo "==> zlib"
make -C /deps/oosdk_libraries/zlib_partial install

echo "==> libzip"
cd /deps/oosdk_libraries/libzip-1.9.2
cmake --toolchain /deps/SDL-PS4/cmake/openorbis.cmake \
  -DBUILD_SHARED=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_DOC=OFF \
  -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DENABLE_MBEDTLS=OFF .
make -j"$(nproc)"
cp zipconf.h "$OO_PS4_TOOLCHAIN/include/zipconf.h"
cp lib/zip.h "$OO_PS4_TOOLCHAIN/include/zip.h"
cp lib/libzip.a "$OO_PS4_TOOLCHAIN/lib/libzip.a"

echo "==> mbedTLS"
sed -i '114s/.*/#define MBEDTLS_NO_UDBL_DIVISION/' /deps/mbedtls-2.16.12/include/mbedtls/config.h
mkdir -p /deps/mbedtls-2.16.12/orbis && cd /deps/mbedtls-2.16.12/orbis
cmake --toolchain /deps/SDL-PS4/cmake/openorbis.cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTING=FALSE -DENABLE_PROGRAMS=FALSE
make -j"$(nproc)"
cp -R ../include/mbedtls "$OO_PS4_TOOLCHAIN/include/"
cp library/libmbed*.a "$OO_PS4_TOOLCHAIN/lib/"

echo "==> mini18n"
cd /deps/mini18n
cmake --toolchain /deps/SDL-PS4/cmake/openorbis.cmake .
make mini18n-static
cp src/mini18n.h "$OO_PS4_TOOLCHAIN/include/mini18n.h"
cp src/libmini18n.a "$OO_PS4_TOOLCHAIN/lib/libmini18n.a"

echo "==> dbglogger apollo-lib sqlite un7zip unrar mxml jbc s3m"
make -C /deps/dbglogger -f Makefile.PS4 install
make -C /deps/apollo-lib -f Makefile.PS4 install
make -C /deps/libSQLite-ps4 install
make -C /deps/libun7zip -f Makefile.PS4 install
make -C /deps/libunrar-ps3 -f Makefile.PS4 install
make -C /deps/mxml/ps4 install
make -C /deps/ps4-libjbc install
make -C /deps/s3mplay -f Makefile.ps4 install

echo "==> SDL2"
mkdir -p /deps/SDL-PS4/orbis && cd /deps/SDL-PS4/orbis
cmake --toolchain ../cmake/openorbis.cmake ..
make -j"$(nproc)"
cp /deps/SDL-PS4/orbis/libSDL2.a "$OO_PS4_TOOLCHAIN/lib"

echo "==> curl"
mkdir -p /deps/oosdk_libraries/curl-7.64.1/orbis
cd /deps/oosdk_libraries/curl-7.64.1/orbis
cmake --toolchain /deps/SDL-PS4/cmake/openorbis.cmake .. \
  -DCMAKE_USE_MBEDTLS=1 -DUSE_UNIX_SOCKETS=0 -DENABLE_THREADED_RESOLVER=0 -DENABLE_IPV6=0
make -j"$(nproc)" libcurl
cp lib/libcurl.a "$OO_PS4_TOOLCHAIN/lib"
cp -R ../include/curl "$OO_PS4_TOOLCHAIN/include/"

echo "==> make package"
cd /src
make createzip
make -j"$(nproc)"
mkdir -p /out
cp -v IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg /out/
ls -la /out
echo "==> BUILD OK"
