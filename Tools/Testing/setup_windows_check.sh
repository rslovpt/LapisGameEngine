#!/usr/bin/env bash
# Builds a Windows header "sysroot" for compile-checking the Windows code on
# Linux: mingw-w64 headers (Win32 API) + libc++ 18 headers (C++ std library).
# Needs git + network once. Result: $SYSROOT (default /tmp/winsdk).
set -e
SYSROOT=${1:-/tmp/winsdk}
WORK=$(mktemp -d)
git clone -q --depth 1 --filter=blob:none --sparse https://github.com/mirror/mingw-w64 "$WORK/mingw"
(cd "$WORK/mingw" && git sparse-checkout set mingw-w64-headers/include mingw-w64-headers/crt mingw-w64-libraries/winpthreads/include)
git clone -q --depth 1 --branch llvmorg-18.1.8 --filter=blob:none --sparse https://github.com/llvm/llvm-project "$WORK/llvm"
(cd "$WORK/llvm" && git sparse-checkout set libcxx/include libcxx/vendor)

rm -rf "$SYSROOT"; mkdir -p "$SYSROOT/include" "$SYSROOT/c++/v1"
H="$WORK/mingw/mingw-w64-headers"
cp -r "$H/crt/"* "$SYSROOT/include/"
cp -r --update=none "$H/include/"* "$SYSROOT/include/"
cp --update=none "$WORK/mingw/mingw-w64-libraries/winpthreads/include/"* "$SYSROOT/include/"
sed 's/@DEFAULT_MSVCRT_VERSION@/0xE00/; s/@DEFAULT_WIN32_WINNT@/0xA00/; s/@[A-Z_0-9]*@/0/g' "$H/crt/_mingw.h.in" > "$SYSROOT/include/_mingw.h"
mkdir -p "$SYSROOT/include/sdks"
sed 's/@[A-Z_0-9]*@/0/g' "$H/crt/sdks/_mingw_ddk.h.in" > "$SYSROOT/include/sdks/_mingw_ddk.h"

cp -r "$WORK/llvm/libcxx/include/"* "$SYSROOT/c++/v1/"
cp "$WORK/llvm/libcxx/vendor/llvm/default_assertion_handler.in" "$SYSROOT/c++/v1/__assertion_handler"
cat > "$SYSROOT/c++/v1/__config_site" <<'CFG'
#ifndef _LIBCPP___CONFIG_SITE
#define _LIBCPP___CONFIG_SITE
#define _LIBCPP_ABI_VERSION 1
#define _LIBCPP_ABI_NAMESPACE __1
#define _LIBCPP_HAS_THREAD_API_WIN32
#define _LIBCPP_HAS_NO_VENDOR_AVAILABILITY_ANNOTATIONS
#define _LIBCPP_HAS_NO_TIME_ZONE_DATABASE
#define _LIBCPP_HAS_NO_STD_MODULES
#define _LIBCPP_PSTL_CPU_BACKEND_SERIAL
#define _LIBCPP_HARDENING_MODE_DEFAULT _LIBCPP_HARDENING_MODE_NONE
#endif
CFG
rm -rf "$WORK"
echo "Windows sysroot ready: $SYSROOT"
