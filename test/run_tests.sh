#!/bin/bash
# Runs every GPU test with a hard time limit, so a broken test can never leave
# a Godot window hanging around. Extra arguments go to Godot, e.g.
#
#   test/run_tests.sh
#   test/run_tests.sh --rendering-driver opengl3
#   test/run_tests.sh --rendering-method mobile
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GODOT="${GODOT:-/Applications/Godot.app/Contents/MacOS/Godot}"
LIMIT="${LIMIT:-90}"
failed=0
for t in render_test input_test demo_pages_test turkish_test images_test; do
  echo "=== $t ==="
  # perl alarm: portable timeout (macOS has no coreutils timeout).
  perl -e 'alarm shift; exec @ARGV' "$LIMIT" \
    "$GODOT" "$@" --path "$ROOT" --quit-after 3000 -s "res://test/$t.gd" 2>&1 \
    | grep -v '^\[[0-9]*\] \|^Godot Engine\|^$' | grep 'PASS\|FAIL\|done\|ERROR\|differ'
  status=${PIPESTATUS[0]}
  if [ "$status" -ne 0 ]; then echo "  -> exit $status"; failed=$((failed+1)); fi
done
pkill -f "$GODOT" 2>/dev/null
echo "suites failed: $failed"
exit $failed
