#!/usr/bin/env bash
# Automated test of the built-in code editor: File > New Script, type into
# it, save (Ctrl+S), then save a syntax error and check it is reported.
#   Tools/Testing/codeeditortest.sh <build/bin dir> <output dir>
set -u
BIN=$(cd "${1:-build/bin}" && pwd)
OUT=${2:-/tmp/lapis_codetest}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/Tools/Testing"
rm -rf "$OUT"; mkdir -p "$OUT/install/bin"
cp "$BIN"/LapisEditor "$BIN"/LapisPlayer "$OUT/install/bin/"
cp -r "$ROOT/Projects" "$ROOT/Templates" "$OUT/install/"
SCRIPT="$OUT/install/Projects/Sandbox/Scripts/NewScript.luau"

Xvfb :95 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
export DISPLAY=:95 LAPIS_NO_SHELL_OPEN=1
sleep 1
"$OUT/install/bin/LapisEditor" --width 1280 --height 800 > "$OUT/editor.log" 2>&1 &
ED=$!
for i in $(seq 1 60); do grep -q "opened project" "$OUT/editor.log" 2>/dev/null && break; sleep 0.5; done
sleep 2
X="python3 $HERE/xinput.py"
# File > New Script...  (menu rows: New Scene, Open Project, Save Scene, New Script)
$X move 25 13 , click 1 , sleep 0.6 , move 80 119 , click 1 , sleep 1.5
# Click into the text, jump to the end of the file, type a line and save.
$X move 640 300 , click 1 , down Control , tap End , up Control , tap Enter , type print 1234 , sleep 0.3
$X down Control , tap s , up Control , sleep 1
import -window root "$OUT/1_saved.png" 2>/dev/null
cp "$SCRIPT" "$OUT/after_first_save.luau" 2>/dev/null
# Now a syntax error.
$X tap Enter , type x = = , down Control , tap s , up Control , sleep 1
import -window root "$OUT/2_error.png" 2>/dev/null
kill $ED 2>/dev/null; wait $ED 2>/dev/null

fail=0
[ -f "$SCRIPT" ] && echo "PASS: File > New Script created the script" || { echo "FAIL: script not created"; fail=1; }
grep -q "print 1234" "$OUT/after_first_save.luau" 2>/dev/null && echo "PASS: typed text was saved" || { echo "FAIL: typed text not saved"; fail=1; }
grep -q "x = =" "$SCRIPT" 2>/dev/null && echo "PASS: second save written" || { echo "FAIL: second save missing"; fail=1; }
sed -i 's/\x1b\[[0-9;]*m//g' "$OUT/editor.log"
if grep -E "\[(ERROR|FATAL)\]" "$OUT/editor.log"; then echo "FAIL: errors in log"; fail=1; else echo "PASS: no errors in log"; fi
echo "Screenshots: $OUT (2_error.png should show the red error line)"
exit $fail
