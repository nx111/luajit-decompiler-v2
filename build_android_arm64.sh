#!/bin/bash
set -e
# Android/arm64-v8a, Android 5.0+ (Lollipop)
NDK=${NDK:-/mnt/d/Mobile/sdk/linux/ndk/android-ndk-r21e}
rm -rf build-android

cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a 

cmake --build build-android -- -j2
