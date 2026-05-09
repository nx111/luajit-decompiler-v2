## LuaJIT Decompiler v2

*LuaJIT Decompiler v2* is a replacement tool for the old and now mostly defunct python decompiler.  
The project fixes all of the bugs and quirks the python decompiler had while also offering  
full support for gotos and stripped bytecode including locals and upvalues.

## Usage

Build with CMake:

```sh
cmake -S . -B build
cmake --build build --config Release
```

Android cross-build example:

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-23
cmake --build build-android --config Release
```

Windows x64 cross-build from WSL with MinGW64:

```sh
sudo apt install mingw-w64
./build_mingw64_wsl.sh
```

Equivalent manual CMake command:

```sh
cmake -S . -B build-mingw64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw64-wsl.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw64 -- -j2
```

Run the program in a shell. Use `-?` to show usage and options:

```sh
luajit-decompiler-v2 INPUT_PATH
```

All successfully decompiled `.lua` files are placed by default into the `output` folder
under the current working directory.

Feel free to [report any issues](https://github.com/marsinator358/luajit-decompiler-v2/issues/new) you have.

## TODO

* bytecode big endian support
* improved decompilation logic for conditional assignments

---

This project uses an boolean expression decompilation algorithm that is based on this paper:  
[www.cse.iitd.ac.in/~sak/reports/isec2016-paper.pdf](https://www.cse.iitd.ac.in/~sak/reports/isec2016-paper.pdf)
