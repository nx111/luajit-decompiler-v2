#!/bin/bash
set -e
# Windows/x86_64 cross-build from WSL with MinGW64

BUILD_DIR=${BUILD_DIR:-build-mingw64}
JOBS=${JOBS:-2}
rm -rf "$BUILD_DIR"

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw64-wsl.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -- -j"$JOBS"
x86_64-w64-mingw32-strip "$BUILD_DIR/luajit-decompiler-v2.exe"