#!/usr/bin/env bash
# Automated editor test on Linux: lays out a Windows-like install
# (Binaries/, Projects/, Templates/), opens LapisEditor in a real X11
# window and clicks through the menus with synthesized mouse/keyboard input:
#   File > Templates > First Person Character, Play, walk, Stop, Export Game.
#   Tools/Testing/editortest.sh <build/bin dir> <output dir>
set -u
BIN=$(cd "${1:-build/bin}" && pwd)
OUT=${2:-/tmp/lapis_editortest}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/Tools/Testing"
rm -rf "$OUT"; mkdir -p "$OUT/install/Binaries"
cp "$BIN"/LapisEditor "$BIN"/LapisPlayer "$OUT/install/Binaries/"
cp -r "$ROOT/Projects" "$ROOT/Templates" "$OUT/install/"
INSTALL="$OUT/install"

Xvfb :96 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
export DISPLAY=:96 LAPIS_NO_SHELL_OPEN=1
sleep 1
"$INSTALL/Binaries/LapisEditor" --width 1280 --height 800 > "$OUT/editor.log" 2>&1 &
ED=$!
for i in $(seq 1 60); do grep -q "Editor: opened project" "$OUT/editor.log" 2>/dev/null && break; sleep 0.5; done
sleep 2
X="python3 $HERE/xinput.py"
shot() { import -window root "$OUT/$1.png" 2>/dev/null; }

shot 01_editor_sandbox
$X move 25 13 , click 1 , sleep 0.8 , move 80 121 , sleep 0.8 , move 300 121 , sleep 0.8
shot 02_file_templates_menu
$X click 1 , sleep 3
shot 03_template_created
$X tap Enter , sleep 1 , move 640 400 , click 1 , sleep 1
shot 04_selected
$X tap F5 , sleep 3 , key W 1.5 , key D 0.6 , tap Space , sleep 1.5
shot 05_playing_first_person
$X tap Escape , sleep 0.5 , tap Escape , sleep 1.5
shot 06_stopped
$X move 25 13 , click 1 , sleep 0.8
shot 07_file_menu
# File > Export Game...  (items: New, Open, Save, sep, Templates, sep, Settings, Export)
$X move 80 172 , sleep 0.5 , click 1 , sleep 4
shot 08_exported
$X tap Enter , sleep 0.5 , key Alt 0.1
kill $ED 2>/dev/null; wait $ED 2>/dev/null

LOG="$OUT/editor.log"; sed -i 's/\x1b\[[0-9;]*m//g' "$LOG"
fail=0
check() { if grep -q "$1" "$LOG"; then echo "PASS: $2"; else echo "FAIL: $2"; fail=1; fi; }
check "opened project 'Sandbox'"      "editor opens the last/default project"
check "opened project 'FirstPerson'"  "File > Templates > First Person Character creates + opens a project"
check "Editor: Play"                  "F5 starts play mode"
check "Editor: Stop"                  "Esc stops play mode"
check "Packaged 'FirstPerson'"        "File > Export Game packages the game"
[ -x "$INSTALL/Export/FirstPerson/FirstPerson" ] && echo "PASS: exported executable exists" || { echo "FAIL: exported executable missing"; fail=1; }
if grep -E "\[(ERROR|FATAL)\]" "$LOG"; then echo "FAIL: errors in log"; fail=1; else echo "PASS: no errors in log"; fi
echo "Screenshots: $OUT"
exit $fail
