#!/bin/bash

set -e
export PATH="/c/msys64/clang64/bin:/c/msys64/usr/bin:$PATH"
cd "$(dirname "$0")"

skip="textweb $(grep -l 'APP_CAT_GAMES' kexts/*.c | xargs -n1 basename -s .c | tr '\n' ' ')"

bash build.sh

img=built/flopnix_minimal.img
keep=()
for f in built/kexts/*.kx; do
    case " $skip " in *" $(basename "$f" .kx) "*) ;; *) keep+=("$f");; esac
done

echo "== creating $img..."
sect=$(( $(stat -c%s built/flopnix.ku) / 512 ))
dd if=/dev/zero of=$img bs=512 count=2880 status=none
dd if=built/flopnix.img of=$img bs=512 count=$((sect + 1)) conv=notrunc status=none
python tools/fscp.py $img --into sys/ "${keep[@]}" >/dev/null
echo "   left out: $skip"

got=$(python tools/fscp.py $img --list | grep -c 'sys/.*[.]kx')
if [ "$got" != "${#keep[@]}" ]; then
    echo "BUILD FAILED: installed ${#keep[@]} extensions, image lists $got" >&2
    rm -f $img
    exit 1
fi
echo "   verified!! $got extensions on image"
echo "== done! - minimal"
echo "   $img  $(stat -c%s $img) bytes  (no games, no browser)"
