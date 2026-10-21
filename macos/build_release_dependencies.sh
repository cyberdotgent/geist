#!/bin/bash
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

# Existing library dependencies only; never downloaded by project CMake.
# Keep the build outside the checkout and expose one universal static prefix.
prefix=${1:?usage: build_release_dependencies.sh absolute-prefix}
case "$prefix" in /*) ;; *) echo 'Prefix must be absolute' >&2; exit 1 ;; esac
scratch=$(mktemp -d)
export MACOSX_DEPLOYMENT_TARGET=13.0
sdk=$(xcrun --sdk macosx --show-sdk-path)
curl -fL --retry 3 https://downloads.sourceforge.net/libpng/libpng-1.6.59.tar.xz -o "$scratch/png.tar.xz"
curl -fL --retry 3 https://downloads.sourceforge.net/project/giflib/giflib-5.x/giflib-5.2.2.tar.gz -o "$scratch/gif.tar.gz"
echo "d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389  $scratch/png.tar.xz" | shasum -a 256 -c -
echo "be7ffbd057cadebe2aa144542fd90c6838c6a083b5e8a9048b8ee3b66b29d5fb  $scratch/gif.tar.gz" | shasum -a 256 -c -
tar -xf "$scratch/png.tar.xz" -C "$scratch"
mkdir -p "$prefix/lib" "$prefix/include" "$prefix/licenses"
for arch in arm64 x86_64; do
  cmake -S "$scratch/libpng-1.6.59" -B "$scratch/png-$arch" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DPNG_SHARED=OFF -DPNG_STATIC=ON \
    -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF -DPNG_ARM_NEON=off
  cmake --build "$scratch/png-$arch" --parallel 3
  mkdir "$scratch/gif-$arch"
  tar -xf "$scratch/gif.tar.gz" -C "$scratch/gif-$arch" --strip-components=1
  make -C "$scratch/gif-$arch" -j3 libgif.a \
    CC="$(xcrun --find clang) -arch $arch" \
    CFLAGS="-O2 -mmacosx-version-min=13.0 -isysroot \"$sdk\""
done
lipo -create "$scratch/png-arm64/libpng16.a" "$scratch/png-x86_64/libpng16.a" -output "$prefix/lib/libpng16.a"
lipo -create "$scratch/gif-arm64/libgif.a" "$scratch/gif-x86_64/libgif.a" -output "$prefix/lib/libgif.a"
cmp "$scratch/png-arm64/pnglibconf.h" "$scratch/png-x86_64/pnglibconf.h"
cp "$scratch/libpng-1.6.59/png.h" "$scratch/libpng-1.6.59/pngconf.h" "$scratch/png-arm64/pnglibconf.h" "$scratch/gif-arm64/gif_lib.h" "$prefix/include/"
cp "$scratch/libpng-1.6.59/LICENSE" "$prefix/licenses/libpng.txt"
cp "$scratch/gif-arm64/COPYING" "$prefix/licenses/giflib.txt"
lipo -verify_arch arm64 x86_64 "$prefix/lib/libpng16.a"
lipo -verify_arch arm64 x86_64 "$prefix/lib/libgif.a"
