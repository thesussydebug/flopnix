#!/bin/bash
# Builds one extension and optionally installs it into an image.

set -e
export PATH="/c/msys64/clang64/bin:/c/msys64/usr/bin:$PATH"

HERE="$(cd "$(dirname "$0")" && echo "$PWD")"
ROOT="$(dirname "$HERE")"

SRC="$1"
IMG="$2"
[ -z "$SRC" ] && { echo "usage: mkkext.sh <source.c> [flopnix.img]"; exit 1; }
[ -f "$SRC" ] || { echo "no such file: $SRC"; exit 1; }

NAME="$(basename "$SRC" .c)"
OUT="$(dirname "$SRC")/$NAME.kx"
OBJ="$(dirname "$SRC")/.$NAME.kxo.tmp"
trap 'rm -f "$OBJ"' EXIT
OPT=-O2
if [ "$NAME" = selftest_small ]; then OPT=-Os; fi

clang --target=i386-unknown-none-elf -ffreestanding -fno-builtin \
 -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables \
 -mno-sse -mno-mmx "$OPT" -Wall -Wextra -I"$ROOT/src" -I"$ROOT/kexts" \
 -c "$SRC" -o "$OBJ"

undef=$(llvm-objdump -t "$OBJ" | awk '/\*UND\*/{print $NF}')
if [ -n "$undef" ]; then
    echo "$NAME: unresolved symbols (E41): $undef" >&2
    rm -f "$OBJ"
    exit 1
fi
llvm-objcopy --strip-debug "$OBJ" "$OUT"
rm -f "$OBJ"
echo "built $OUT ($(stat -c%s "$OUT") bytes)"

if [ -n "$IMG" ]; then
    [ -f "$IMG" ] || { echo "no image: $IMG"; exit 1; }
    python "$HERE/fscp.py" "$IMG" "$OUT"
fi
