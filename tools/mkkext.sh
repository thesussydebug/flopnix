#!/bin/bash
# Builds one extension and optionally installs it into an image.

set -e

HERE="$(cd "$(dirname "$0")" && echo "$PWD")"
ROOT="$(dirname "$HERE")"
export PATH="/c/msys64/clang64/bin:/c/msys64/usr/bin:$PATH"

SRC="$1"
IMG="$2"
[ -z "$SRC" ] && { echo "usage: mkkext.sh <source.c> [flopnix.img]"; exit 1; }
[ -f "$SRC" ] || { echo "no such file: $SRC"; exit 1; }

NAME="$(basename "$SRC" .c)"
OUT="$(dirname "$SRC")/$NAME.kx"
OPT=-O2
if [ "$NAME" = selftest_small ]; then OPT=-Os; fi

clang --target=i386-unknown-none-elf -ffreestanding -fno-builtin \
 -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables \
 -mno-sse -mno-mmx "$OPT" -Wall -Wextra -I"$ROOT/src" \
 -c "$SRC" -o "$NAME.kxo.tmp"

undef=$(llvm-objdump -t "$NAME.kxo.tmp" | awk '/\*UND\*/{print $NF}')
if [ -n "$undef" ]; then
    echo "$NAME: unresolved symbols (E41): $undef" >&2
    rm -f "$NAME.kxo.tmp"
    exit 1
fi
llvm-objcopy --strip-debug "$NAME.kxo.tmp" "$OUT"
rm -f "$NAME.kxo.tmp"
echo "built $OUT ($(stat -c%s "$OUT") bytes)"

if [ -n "$IMG" ]; then
    [ -f "$IMG" ] || { echo "no image: $IMG"; exit 1; }
    python "$HERE/fscp.py" "$IMG" "$OUT"
fi
