#!/bin/bash
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

# Static zlib, libpng and giflib for a MinGW-w64 cross build of the Win32
# reader. The project CMake never downloads; this script prepares a prefix
# outside the checkout from pinned, checksum-verified release tarballs:
#
#   win32/build_mingw_dependencies.sh /abs/prefix
#   cmake -S . -B /abs/build-mingw \
#     -DCMAKE_TOOLCHAIN_FILE=win32/toolchain-mingw-w64.cmake \
#     -DCMAKE_PREFIX_PATH=/abs/prefix -DGEIST_BUILD_WIN32_READER=ON
#
# The licence texts land in <prefix>/licenses for the reader's artefact.
prefix=${1:?usage: build_mingw_dependencies.sh absolute-prefix}
case "$prefix" in /*) ;; *) echo 'Prefix must be absolute' >&2; exit 1 ;; esac
host=${GEIST_MINGW_HOST:-x86_64-w64-mingw32}
toolchain="$(cd "$(dirname "$0")" && pwd)/toolchain-mingw-w64.cmake"
scratch=$(mktemp -d)  # honours TMPDIR
trap 'rm -rf "$scratch"' EXIT

fetch() { # url file sha256
  curl -fL --retry 3 "$1" -o "$scratch/$2"
  echo "$3  $scratch/$2" | sha256sum -c -
}
fetch https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz zlib.tar.gz \
  9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
fetch https://downloads.sourceforge.net/libpng/libpng-1.6.59.tar.xz png.tar.xz \
  d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389
fetch https://downloads.sourceforge.net/project/giflib/giflib-5.x/giflib-5.2.2.tar.gz gif.tar.gz \
  be7ffbd057cadebe2aa144542fd90c6838c6a083b5e8a9048b8ee3b66b29d5fb

mkdir -p "$prefix/lib" "$prefix/include" "$prefix/licenses" \
  "$scratch/zlib" "$scratch/png" "$scratch/gif"
tar -xf "$scratch/zlib.tar.gz" -C "$scratch/zlib" --strip-components=1
tar -xf "$scratch/png.tar.xz" -C "$scratch/png" --strip-components=1
tar -xf "$scratch/gif.tar.gz" -C "$scratch/gif" --strip-components=1

# zlib: its own win32 makefile builds the static archive only.
make -C "$scratch/zlib" -f win32/Makefile.gcc -j1 libz.a PREFIX="$host-"
cp "$scratch/zlib/libz.a" "$prefix/lib/"
cp "$scratch/zlib/zlib.h" "$scratch/zlib/zconf.h" "$prefix/include/"
cp "$scratch/zlib/LICENSE" "$prefix/licenses/zlib.txt"

cmake -S "$scratch/png" -B "$scratch/png-build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_PREFIX_PATH="$prefix" \
  -DZLIB_ROOT="$prefix" -DZLIB_USE_STATIC_LIBS=ON \
  -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
cmake --build "$scratch/png-build" -j1
cp "$scratch/png-build/libpng16.a" "$prefix/lib/"
cp "$scratch/png/png.h" "$scratch/png/pngconf.h" "$scratch/png-build/pnglibconf.h" "$prefix/include/"
cp "$scratch/png/LICENSE" "$prefix/licenses/libpng.txt"

make -C "$scratch/gif" -j1 libgif.a CC="$host-gcc" AR="$host-ar" CFLAGS="-O2 -Wall"
cp "$scratch/gif/libgif.a" "$prefix/lib/"
cp "$scratch/gif/gif_lib.h" "$prefix/include/"
cp "$scratch/gif/COPYING" "$prefix/licenses/giflib.txt"
ls -l "$prefix/lib" "$prefix/licenses"
