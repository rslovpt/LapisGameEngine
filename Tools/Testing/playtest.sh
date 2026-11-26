#!/usr/bin/env bash
# Automated play-test of a Lapis project on Linux: starts a virtual X server,
# runs LapisPlayer in a real window, drives it with synthesized keyboard and
# mouse input (XTEST), captures screenshots, and checks the log.
#
#   Tools/Testing/playtest.sh <build/bin dir> <project dir> <output dir>
set -u
BIN=${1:-build/bin}
PROJECT=${2:-Projects/Sandbox}
OUT=${3:-/tmp/lapis_playtest}
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/player.log

DISPLAY_NUM=:97
Xvfb $DISPLAY_NUM -screen 0 1024x600x24 -nolisten tcp >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
export DISPLAY=$DISPLAY_NUM
sleep 1

"$BIN/LapisPlayer" --project "$PROJECT" --width 960 --height 540 --novsync > "$OUT/player.log" 2>&1 &
PLAYER=$!

# Wait for the game to come up.
for i in $(seq 1 60); do
    grep -q "Welcome to the Lapis Sandbox" "$OUT/player.log" 2>/dev/null && break
    sleep 0.5
done
sleep 2
X="python3 $HERE/xinput.py"
shot() { import -window root "$OUT/$1.png" 2>/dev/null; }

shot 01_start
$X move 480 270 , click 1 , sleep 1.5 , tap Space , sleep 2
shot 02_balls_thrown
$X tap B , sleep 3
shot 03_crate_dropped
$X key W 1.5 , drag 3 220 40 1.0 , sleep 1
shot 04_moved_and_looked
$X tap F1 , sleep 1.5
shot 05_collider_view
$X tap F1 , tap R , sleep 1 , tap Escape

for i in $(seq 1 40); do kill -0 $PLAYER 2>/dev/null || break; sleep 0.5; done
if kill -0 $PLAYER 2>/dev/null; then echo "FAIL: player did not exit on Escape"; kill $PLAYER; exit 1; fi
wait $PLAYER
CODE=$?

LOG="$OUT/player.log"
sed -i 's/\x1b\[[0-9;]*m//g' "$LOG"
fail=0
check() { if grep -q "$1" "$LOG"; then echo "PASS: $2"; else echo "FAIL: $2"; fail=1; fi; }
check "Threw ball #1"           "left click throws a ball"
check "Threw ball #2"           "space throws a ball"
check "Dropped a crate"         "B drops a crate"
check "Reset: removed"          "R resets"
check "Escape pressed"          "Escape quits through the script"
if grep -E "\[(ERROR|FATAL)\]" "$LOG"; then echo "FAIL: errors in log"; fail=1; else echo "PASS: no errors in log"; fi
[ $CODE -eq 0 ] && echo "PASS: exit code 0" || { echo "FAIL: exit code $CODE"; fail=1; }
echo "Screenshots in $OUT"
exit $fail
