#!/usr/bin/env bash
# Runs every template and example project in the player for a few hundred
# frames with --fail-on-script-error. Any Luau error, crash or engine error
# in the log fails the test.
#   Tools/Testing/templatetest.sh [build/bin dir]
set -u
BIN=$(cd "${1:-build/bin}" && pwd)
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=/tmp/lapis_templatetest; rm -rf "$OUT"; mkdir -p "$OUT"
Xvfb :87 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
export DISPLAY=:87
sleep 1
fail=0
for dir in "$ROOT"/Templates/* "$ROOT"/Projects/* "$ROOT"/Projects/StressTests/*; do
    [ -f "$dir/project.lapis" ] || continue
    name=$(basename "$dir")
    cp -r "$dir" "$OUT/$name"
    timeout 300 "$BIN/LapisPlayer" --project "$OUT/$name" --width 640 --height 360 --frames 240 --fail-on-script-error \
        --screenshot "$OUT/$name.bmp" > "$OUT/$name.log" 2>&1
    code=$?
    sed -i 's/\x1b\[[0-9;]*m//g' "$OUT/$name.log"
    if [ $code -ne 0 ] || grep -qE "\[(ERROR|FATAL)\]" "$OUT/$name.log"; then
        echo "FAIL: $name (exit $code)"; grep -E "\[(ERROR|FATAL)\]" "$OUT/$name.log" | head -5; fail=1
    else
        echo "PASS: $name runs without errors"
    fi
done
exit $fail
