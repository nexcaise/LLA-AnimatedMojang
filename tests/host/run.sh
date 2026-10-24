#!/usr/bin/env bash
# Host-side tests (Linux + Mesa llvmpipe, no phone needed). Requires g++, libEGL/libGLESv2 (mesa), python3.
#   ./tests/host/run.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export AM_ROOT="$(cd "$HERE/../.." && pwd)"
export LIBGL_ALWAYS_SOFTWARE=1
OUT="$(mktemp -d)"; INC="-I$AM_ROOT/include -I$AM_ROOT/src"; GL="-l:libEGL.so.1 -l:libGLESv2.so.2 -lpthread"
CXX="g++ -std=c++20 -O2 $INC"
$CXX "$HERE/test_cpu.cpp"    "$AM_ROOT/src/scene/ImageDecode.cpp" -o "$OUT/cpu"    && echo "== mesh builder / title atlas" && "$OUT/cpu"
$CXX "$HERE/test_scan.cpp"   "$AM_ROOT/src/runtime/Hook.cpp"      -o "$OUT/scan"   && echo "== signature scanner"          && "$OUT/scan"
$CXX "$HERE/test_robust.cpp" "$AM_ROOT/src/scene/CaveGlesRenderer.cpp" "$AM_ROOT/src/scene/ImageDecode.cpp" $GL -o "$OUT/robust" \
                                                                         && echo "== renderer robustness"        && "$OUT/robust" | grep -E "PASS|FAIL|ALL|SOME"
