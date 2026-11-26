#!/usr/bin/env bash
# Compile-checks every Windows translation unit (engine + apps + tests) with
# clang targeting x86_64-w64-windows against real Win32 headers. Catches API
# misuse, typos and missing includes in Windows-only code without a Windows PC.
#   Tools/Testing/windows_check.sh [sysroot]
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SYSROOT=${1:-/tmp/winsdk}
[ -d "$SYSROOT/include" ] || "$ROOT/Tools/Testing/setup_windows_check.sh" "$SYSROOT"
CLANG_RES=$(clang++ -print-resource-dir)
L="$ROOT/ThirdParty/luau"
FLAGS=(--target=x86_64-w64-windows-gnu -std=c++17 -fsyntax-only -nostdinc -nostdinc++
       -isystem "$SYSROOT/c++/v1" -isystem "$SYSROOT/include" -isystem "$CLANG_RES/include"
       -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS
       -Wall -Wextra -Wno-unused-parameter
       -I"$ROOT/Engine/Source" -I"$ROOT/Tests" -I"$L/VM/include" -I"$L/Compiler/include" -I"$L/Common/include" -I"$L/Ast/include")
fail=0; n=0
while IFS= read -r f; do
    n=$((n+1))
    out=$(clang++ "${FLAGS[@]}" "$f" 2>&1 | grep -E "error|warning")
    if [ -n "$out" ]; then echo "== ${f#$ROOT/}"; echo "$out" | head -20; fail=1; fi
done < <(find "$ROOT/Engine/Source" "$ROOT/Apps" "$ROOT/Tests" -name '*.cpp' -not -path '*/Linux/*')
[ $fail -eq 0 ] && echo "Windows compile check: $n files OK" || echo "Windows compile check: FAILED"
exit $fail
