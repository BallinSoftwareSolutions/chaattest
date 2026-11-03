#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
compiler=${MAXIFYING_CXX:-${GAMETUNE_CXX:-/workspace/mingw/usr/bin/x86_64-w64-mingw32-g++-posix}}
mkdir -p build
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -I src tests/engine_tests.cpp src/engine.cpp -o build/MaxifyingTests.exe -static -ladvapi32 -lpowrprof -lpsapi -lole32 -luser32
