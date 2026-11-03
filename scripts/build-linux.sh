#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
compiler=${GAMETUNE_CXX:-/workspace/mingw/usr/bin/x86_64-w64-mingw32-g++-posix}
mkdir -p downloads
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX src/main.cpp -o downloads/GameTune.exe -municode -mwindows -static -ladvapi32 -lpowrprof -lshell32 -luser32 -lgdi32 -lole32
cd downloads
sha256sum GameTune.exe > SHA256SUMS.txt
