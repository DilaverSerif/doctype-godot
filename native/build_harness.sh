#!/bin/bash
# Builds and runs the native test harness (no Godot, no GPU): it drives the
# same C ABI the GDExtension does, asserts on the quad stream and rasterizes
# demo.html through the reference CPU rasterizer into build/out/demo.png.
#
#   native/build_harness.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LITEHTML="$ROOT/third_party/litehtml"
BUILD="$ROOT/build/harness"
mkdir -p "$BUILD/obj" "$BUILD/out"
INCLUDES=(-I"$LITEHTML/include" -I"$LITEHTML/include/litehtml" -I"$LITEHTML/src" -I"$LITEHTML/src/gumbo/include" -I"$LITEHTML/src/gumbo/include/gumbo" -I"$ROOT/third_party" -I"$ROOT/src" -I"$ROOT/tests" -I"$LITEHTML/containers/test")
CXX="${CXX:-clang++}"
CC="${CC:-clang}"
CXXFLAGS="-std=c++17 -O2 -DNDEBUG -w"
CFLAGS="-std=c99 -O2 -DNDEBUG -w"
compile() {
  local src="$1" obj="$2"
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    case "$src" in
      *.c) $CC $CFLAGS "${INCLUDES[@]}" -c "$src" -o "$obj" ;;
      *) $CXX $CXXFLAGS "${INCLUDES[@]}" -c "$src" -o "$obj" ;;
    esac
  fi
}
export -f compile; export CC CXX CFLAGS CXXFLAGS; export INCLUDES_STR="${INCLUDES[*]}"
jobs=0
for f in "$LITEHTML"/src/*.cpp "$LITEHTML"/src/gumbo/*.c "$ROOT"/src/*.cpp "$LITEHTML/containers/test/lodepng.cpp" "$ROOT/tests/harness.cpp"; do
  obj="$BUILD/obj/$(echo "$f" | sed 's|.*/third_party/litehtml/||; s|.*/native/||; s|/|_|g').o"
  ( if [ ! -f "$obj" ] || [ "$f" -nt "$obj" ]; then
      case "$f" in
        *.c) $CC $CFLAGS "${INCLUDES[@]}" -c "$f" -o "$obj" ;;
        *) $CXX $CXXFLAGS "${INCLUDES[@]}" -c "$f" -o "$obj" ;;
      esac
    fi ) &
  jobs=$((jobs+1)); if [ $jobs -ge 8 ]; then wait; jobs=0; fi
done
wait
$CXX $CXXFLAGS "$BUILD"/obj/*.o -o "$BUILD/lhu_harness"
echo "==> running harness"
LHU_ROOT="$ROOT" "$BUILD/lhu_harness" "$BUILD/out" | tail -15
