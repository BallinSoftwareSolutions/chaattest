#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
compiler=${MAXIFYING_CXX:-${GAMETUNE_CXX:-/workspace/mingw/usr/bin/x86_64-w64-mingw32-g++-posix}}
resource_compiler=${MAXIFYING_RC:-${GAMETUNE_RC:-${compiler%g++*}windres}}
mkdir -p downloads build
"$resource_compiler" --preprocessor="$compiler" --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED -I src src/resources.rc -o build/resources.o
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX src/main.cpp src/engine.cpp src/ui.cpp src/bloat.cpp build/resources.o -o downloads/Maxifying.exe -municode -mwindows -static -ladvapi32 -lpowrprof -lpsapi -lshell32 -luser32 -lgdi32 -lole32 -lcomdlg32 -ldwmapi -liphlpapi -lws2_32 -ld2d1 -ldwrite -luuid -lwinmm
cp downloads/Maxifying.exe downloads/GameTune.exe
cd downloads
sha256sum Maxifying.exe GameTune.exe > SHA256SUMS.txt
