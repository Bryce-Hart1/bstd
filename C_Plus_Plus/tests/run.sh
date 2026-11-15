#!/bin/sh
# Builds and runs every tests/**/*Test.cpp, each as its own program.
#   tests/run.sh                         # c++ with -std=c++17
#   CXX=g++-16 STD=c++20 tests/run.sh -O2 -fsanitize=address,undefined
# Extra arguments go to the compiler. Exits non-zero if any test fails to build or fails a check.
cd "$(dirname "$0")/.." || exit 1
CXX="${CXX:-c++}"
STD="${STD:-c++17}"
out="${TMPDIR:-/tmp}/bstd-tests"
mkdir -p "$out"

status=0
for src in $(find tests -name '*Test.cpp' | sort); do
    bin="$out/$(basename "$src" .cpp)"
    printf '%-32s ' "$src"
    if ! "$CXX" -std="$STD" -Wall -Wextra -Wpedantic "$@" "$src" -o "$bin"; then
        echo "BUILD FAILED"
        status=1
        continue
    fi
    "$bin" || status=1
done
exit $status
