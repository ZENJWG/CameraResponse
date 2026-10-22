#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
CXX="${CXX:-c++}"
OUT="${TMPDIR:-/tmp}/camera_response_core_tests_$$"
trap 'rm -f "$OUT"' EXIT INT TERM
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic response_core_tests.cpp -o "$OUT"
"$OUT"
