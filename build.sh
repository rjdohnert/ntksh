#!/usr/bin/env sh
# Build script for ntksh (Windows NT KornShell)
# Compatible with POSIX sh, ntksh, and bash

set -e

if [ -d "src/core" ]; then
    ROOT_DIR="$(pwd)"
elif [ -d "../src/core" ]; then
    ROOT_DIR="$(cd .. && pwd)"
else
    ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
    if [ ! -d "$ROOT_DIR/src" ] && [ -d "$ROOT_DIR/../src" ]; then
        ROOT_DIR="$(cd "$ROOT_DIR/.." && pwd)"
    fi
fi
BIN_DIR="$ROOT_DIR/bin"
SRC_DIR="$ROOT_DIR/src"

mkdir -p "$BIN_DIR"

LIBS="-ladvapi32 -lshell32 -luser32 -lpsapi -liphlpapi -lws2_32 -lsetupapi -lcfgmgr32 -lrstrtmgr -lwinhttp"

TARGET_TMP="$BIN_DIR/ksh_build_tmp.exe"
TARGET_EXE="$BIN_DIR/ksh.exe"
ROOT_EXE="$ROOT_DIR/ksh.exe"

rm -f "$TARGET_TMP" "$BIN_DIR/ksh.old" "$ROOT_DIR/ksh.old" 2>/dev/null || true

if [ "$1" = "amalgamate" ] || [ "$1" = "--amalgamate" ] || [ "$1" = "-amalgamate" ]; then
    echo "[BUILD] Compiling from single-file amalgamation src/ksh.c..."
    CFLAGS="-O2 -Wall -Wextra -fms-extensions -municode"
    clang $CFLAGS -o "$TARGET_TMP" "$SRC_DIR/ksh.c" $LIBS
else
    echo "[BUILD] Compiling modular ntksh sources..."
    CFLAGS="-O2 -Wall -Wextra -fms-extensions -municode -I$ROOT_DIR/include"
    clang $CFLAGS -o "$TARGET_TMP" "$SRC_DIR/main.c" "$SRC_DIR/core/"*.c "$SRC_DIR/batch/"*.c "$SRC_DIR/builtins/"*.c "$SRC_DIR/help/"*.c $LIBS
fi

if [ -f "$TARGET_EXE" ]; then
    mv -f "$TARGET_EXE" "$BIN_DIR/ksh.old" 2>/dev/null || true
fi
mv -f "$TARGET_TMP" "$TARGET_EXE"

if [ -f "$ROOT_EXE" ]; then
    mv -f "$ROOT_EXE" "$ROOT_DIR/ksh.old" 2>/dev/null || true
fi
cp -f "$TARGET_EXE" "$ROOT_EXE"

rm -f "$BIN_DIR/ksh.old" "$ROOT_DIR/ksh.old" 2>/dev/null || true
echo "[SUCCESS] Binary created: bin/ksh.exe and ksh.exe"
