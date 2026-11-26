#!/usr/bin/env bash
# Automated test of the built-in code editor: select the Gem in the Sandbox,
# open its script from the Inspector, type, save (Ctrl+S), then save a syntax
# error and check that the error is reported.
#   Tools/Testing/codeeditortest.sh <build/bin dir> <output dir>
set -u
BIN=$(cd "${1:-build/bin}" && pwd)
OUT=${2:-/tmp/lapis_codetest}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/Tools/Testing"
rm -rf "$OUT"; mkdir -p "$OUT/install/bin"
cp "$BIN"/LapisEditor "$BIN"/LapisPlayer "$OUT/install/bin/"
cp -r "$ROOT/Projects" "$ROOT/Templates" "$OUT/install/"
SCRIPT="$OUT/install/Projects/Sandbox/Scripts/Spinner.luau"

Xvfb :95 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
export DISPLAY=:95 LAPIS_NO_SHELL_OPEN=1
sleep 1
"$OUT/install/bin/LapisEditor" --project "$OUT/install/Projects/Sandbox" --width 1280 --height 800 > "$OUT/editor.log" 2>&1 &
ED=$!
for i in $(seq 1 60); do grep -q "Editor: opened project" "$OUT/editor.log" 2>/dev/null && break; sleep 0.5; done
sleep 2
X="python3 $HERE/xinput.py"
shot() { import -window root "$OUT/$1.png" 2>/dev/null; }

# Hierarchy row 6 = Gem; then Inspector "Edit script".
$X move 60 234 , click 1 , sleep 1
shot 01_gem_selected
$X move 1030 350 , click 1 , sleep 1.5
shot 02_script_open
$X type -- edited by the code editor test , tap Enter , combo Control s , sleep 1
shot 03_saved
$X type local = , combo Control s , sleep 1
shot 04_error
kill $ED 2>/dev/null; wait $ED 2>/dev/null

fail=0
head -1 "$SCRIPT" | grep -q "^-- edited by the code editor test" && echo "PASS: typed text saved to Spinner.luau" || { echo "FAIL: file not edited"; head -3 "$SCRIPT"; fail=1; }
grep -q "^local = " "$SCRIPT" && echo "PASS: second save written" || { echo "FAIL: second save missing"; fail=1; }
echo "Screenshots: $OUT"
exit $fail
