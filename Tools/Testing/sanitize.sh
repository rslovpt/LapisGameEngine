#!/usr/bin/env bash
# Memory-safety check: builds everything with AddressSanitizer +
# LeakSanitizer + UBSan, then runs the unit tests and the GUI test scripts
# (editor, code editor, play test). Any leak, use-after-free, out-of-bounds
# access or undefined behaviour is written to <out>/asan.* and fails the run.
#   Tools/Testing/sanitize.sh [build dir] [out dir]
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD=${1:-$ROOT/build-asan}
OUT=${2:-/tmp/lapis_sanitize}
rm -rf "$OUT"; mkdir -p "$OUT"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLAPIS_SANITIZE=ON >/dev/null || exit 1
cmake --build "$BUILD" -j"$(nproc)" >"$OUT/build.txt" 2>&1 || { echo "FAIL: build (see $OUT/build.txt)"; exit 1; }
export ASAN_OPTIONS=detect_leaks=1:log_path=$OUT/asan
export UBSAN_OPTIONS=print_stacktrace=1:log_path=$OUT/asan
fail=0
"$BUILD/bin/LapisTests" >"$OUT/tests.txt" 2>&1 || { echo "FAIL: unit tests"; fail=1; }
bash "$ROOT/Tools/Testing/editortest.sh" "$BUILD/bin" "$OUT/editor" >"$OUT/editortest.txt" 2>&1 || fail=1
bash "$ROOT/Tools/Testing/codeeditortest.sh" "$BUILD/bin" >"$OUT/codeeditortest.txt" 2>&1 || fail=1
bash "$ROOT/Tools/Testing/playtest.sh" "$BUILD/bin" >"$OUT/playtest.txt" 2>&1 || fail=1
grep -h "FAIL" "$OUT"/*test*.txt
if ls "$OUT"/asan.* >/dev/null 2>&1; then
    echo "FAIL: sanitizer reports:"; grep -h "ERROR\|SUMMARY\|runtime error" "$OUT"/asan.* | head -20; fail=1
else
    echo "PASS: no leaks / memory errors / undefined behaviour reported"
fi
exit $fail
