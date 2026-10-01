#!/bin/bash
# Writes release files to built/ and compiler intermediates to out/.

set -e
export PATH="/c/msys64/clang64/bin:/c/msys64/usr/bin:$PATH"
cd "$(dirname "$0")"

CFLAGS="--target=i386-unknown-none-elf -ffreestanding -fno-builtin \
 -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables \
 -mno-sse -mno-mmx -g -O2 -Wall -Wextra -Isrc"

CFLAGS="$CFLAGS -DOS_BUILD_DATE=\"$(date +%Y-%m-%d)\""

mkdir -p out built/kexts
exec 9>out/.build.lock
flock 9

JOBS=${JOBS:-$(nproc)}

job_list=() job_running=0 job_fail=0 job_next=0
job_reap() {
    wait -n || job_fail=1
    job_running=$((job_running - 1))
    while (( job_next < ${#job_list[@]} )) && [ -e "out/.job.${job_list[job_next]}.done" ]; do
        cat "out/.job.${job_list[job_next]}.log"
        job_next=$((job_next + 1))
    done
}
spawn() {
    if (( job_fail )); then return; fi
    ( "$1" "$2" >"out/.job.$2.log" 2>&1 && rc=0 || rc=$?
      : >"out/.job.$2.done"; exit $rc ) 9>&- &
    job_list+=("$2")
    job_running=$((job_running + 1))
    if (( job_running >= JOBS )); then job_reap; fi
}
drain() {
    local fail
    while (( job_running > 0 )); do job_reap; done
    fail=$job_fail
    rm -f out/.job.*
    job_list=() job_fail=0 job_next=0
    return $fail
}

osver=$(sed -n 's/.*#define OS_VER  *"\([^"]*\)".*/\1/p' src/os.h)
if ! [[ "$osver" =~ ^[0-9]+\.[0-9]+\.[0-9]+(_[0-9]+)?$ ]]; then
    echo "src/os.h must define a valid OS_VER" >&2
    exit 1
fi
kapi=$(sed -n 's/^#define KAPI_VERSION  *\([0-9]*\).*/\1/p' src/kapi.h)


buildnum=$(( $(cat build.num 2>/dev/null || echo 0) + 1 ))
CFLAGS="$CFLAGS -DOS_BUILD_NUM=\"$buildnum\""
echo "== flopnix $osver, kapi $kapi, build $buildnum"

echo "== checking..."
echo "   -shell command usage"
# Check that every shell command has usage text.
missing=""
for c in $(grep -oE 'strn?cmp\(cmd, "[a-z]+ ?"' src/apps.c |
           grep -oE '"[a-z]+ ?"' | tr -d '" ' | sort -u); do
    grep -q "sc_eq(c, \"$c\")" src/shcmd.inc || missing="$missing $c"
done
if [ -n "$missing" ]; then
    echo "src/apps.c dispatches these commands with no sh_usage() entry:$missing" >&2
    echo "(add them to src/shcmd.inc, or they report 'command not found')" >&2
    exit 1
fi

echo "   -net.c nic presence tests"
if grep -nE 'if \(!?io\)' kexts/net.c | grep -qv 'nic_kind'; then
    echo "kexts/net.c tests \`io\` as NIC presence - use net_up() instead:" >&2
    grep -nE 'if \(!?io\)' kexts/net.c | grep -v 'nic_kind' >&2
    exit 1
fi

echo "   -app catalog"
python tools/appcatalog.py || exit 1

echo "== assembling..."
echo "   boot/boot.asm"
nasm -f bin boot/boot.asm -o out/boot.bin
echo "   boot/stub.asm"
nasm -f elf32 ${STUBDEF:-} -DKERNEL_API="$kapi" -DKERNEL_VERSION="\"$osver\"" boot/stub.asm -o out/stub.o
for s in setjmp switch emergency; do echo "   src/$s.S"; done
clang --target=i386-unknown-none-elf -c src/setjmp.S -o out/setjmp.o
clang --target=i386-unknown-none-elf -c src/switch.S -o out/switch.o
clang --target=i386-unknown-none-elf -c src/emergency.S -o out/emergency_entry.o

echo "== compiling kernel..."

cc_kernel() {
    local f=$1 SZ=
    case $f in fs|fdc|cpuacct|kernel|uhci|paging|apps|debug|emergency|cpu|mtrr|ring3|config|kupdate|fault|kext|uisvc|services) SZ=-Oz;; hw|heap|thread) SZ=-Os;; gui) SZ=-Oz;; esac
    clang $CFLAGS $SZ -c src/$f.c -o out/$f.o || return 1
    echo "   $f.o: $(stat -c%s out/$f.o) bytes ${SZ:--O2}"
}
for f in emergency util hw cpu mtrr paging ring3 fdc fs uhci services uisvc config gfx gui apps kext heap kupdate debug fault thread cpuacct kernel; do
    spawn cc_kernel $f
done
drain

echo "== compiling kexts..."
rm -f built/kexts/*.kx

# Extensions must have no unresolved symbols.
if command -v llvm-nm >/dev/null 2>&1; then
    undefs() { llvm-nm --undefined-only "$1" 2>/dev/null; }
elif command -v llvm-objdump >/dev/null 2>&1; then
    undefs() { llvm-objdump -t "$1" 2>/dev/null | awk '/\*UND\*/{print $NF}'; }
else
    echo "neither llvm-nm nor llvm-objdump found - cannot run the E41 gate" >&2
    exit 1
fi

# Limit the emergency handler to its independent recovery helpers.
for sym in $(undefs out/emergency.o); do
    case "$sym" in thr_self|emergency_enter|kupd_critical|timer_alive|panic_monitor) ;;
        *) echo "emergency.o: unsafe recovery dependency: $sym" >&2; exit 1;;
    esac
done
echo "   emergency.o"
cc_kext() {
    local n=$1 SZ= undef
    case $n in debug|charmap|game2048|baseconv|archive|textweb|breakout|update|imgview) SZ=-Os;; image) SZ=-Oz;; esac
    clang $CFLAGS $SZ -c "kexts/$n.c" -o "out/$n.kxo" || return 1
    llvm-objcopy --strip-debug "out/$n.kxo" "built/kexts/$n.kx" || return 1

    undef=$(undefs "out/$n.kxo")
    if [ -n "$undef" ]; then
        echo "   $n.kx: E41 - unresolved symbols (would fail to load):" >&2
        echo "$undef" | sed 's/^/       /' >&2
        return 1
    fi
    echo "   $n.kx: $(stat -c%s built/kexts/$n.kx) bytes ${SZ:--O2}"
}
for n in debug fat net remote dialogs sysinfo notes diskhealth opl2 midi gdi g3d image imgview desktop edit files settings paint calc clock calendar memmap memedit minesweeper reversi snake pong tetris shell crashsim taskmgr serialmon faultlog kextview clipview bench charmap game2048 baseconv archive textweb breakout update; do
    spawn cc_kext $n
done
drain

echo "== linking out/kernel.elf..."
ld.lld -m elf_i386 -T linker.ld -nostdlib -o out/kernel.elf \
    out/stub.o out/emergency.o out/emergency_entry.o out/util.o out/hw.o out/cpu.o out/mtrr.o out/paging.o out/ring3.o out/fdc.o out/fs.o out/uhci.o \
    out/services.o out/uisvc.o out/config.o out/gfx.o out/gui.o out/apps.o \
    out/kext.o out/heap.o out/kupdate.o out/debug.o out/fault.o out/setjmp.o out/switch.o \
    out/thread.o out/cpuacct.o out/kernel.o
echo "== flattening to built/flopnix.ku..."
llvm-objcopy -O binary -j .stub -j .text -j .rodata -j .data out/kernel.elf built/flopnix.ku

# Stamp the sector count and the checksum read by the boot stub.
python - <<'EOF'
import struct
d = bytearray(open('built/flopnix.ku', 'rb').read())
if len(d) % 512:
    d += b'\0' * (512 - len(d) % 512)
struct.pack_into('<H', d, 4, 0)
struct.pack_into('<H', d, 6, len(d) // 512)
s = 0
for i in range(0, len(d), 2):
    w = d[i] | (d[i + 1] << 8)
    s = (((s << 1) | (s >> 15)) + w) & 0xFFFF
struct.pack_into('<H', d, 4, s)
open('built/flopnix.ku', 'wb').write(d)
print("== image checksum %04x (%d sectors)" % (s, len(d) // 512))
EOF

KSIZE=$(stat -c%s built/flopnix.ku)
SECT=$(( (KSIZE + 511) / 512 ))
if [ $SECT -gt 255 ]; then
    echo "kernel too big!: $KSIZE bytes overlaps config sector at LBA 256"; exit 1
fi
echo "== kernel is.. $KSIZE bytes! ($SECT sectors)"
echo "== updating metadata.."
python tools/update_meta.py

echo "== patching boot sector.. kernel is $SECT sectors!"
printf "$(printf '\\x%02x\\x%02x' $((SECT & 0xFF)) $((SECT >> 8)))" \
    | dd of=out/boot.bin bs=1 seek=506 conv=notrunc status=none

echo "== creating built/flopnix.img... "
dd if=/dev/zero of=built/flopnix.img bs=512 count=2880 status=none
echo "   boot sector at lba 0"
dd if=out/boot.bin of=built/flopnix.img conv=notrunc status=none
echo "   kernel at lba 1"
dd if=built/flopnix.ku of=built/flopnix.img bs=512 seek=1 conv=notrunc status=none

echo "== installing extensions... (sys/)"
python tools/fscp.py built/flopnix.img --into sys/ built/kexts/*.kx >/dev/null
echo "   $(ls built/kexts/*.kx | xargs -n1 basename | tr '\n' ' ')"

want=$(ls built/kexts/*.kx | wc -l)
got=$(python tools/fscp.py built/flopnix.img --list | grep -c 'sys/.*[.]kx')
if [ "$want" != "$got" ]; then
    echo "BUILD FAILED: installed $want extensions, image lists $got" >&2
    python tools/fscp.py built/flopnix.img --list | sed 's|.*sys/||;s| .*||' | sort > out/.fx_got
    ls built/kexts/*.kx | xargs -n1 basename | sort > out/.fx_want
    echo "missing: $(comm -23 out/.fx_want out/.fx_got | tr '\n' ' ')" >&2
    exit 1
fi
echo "   verified!! $got extensions on image"

echo "== installing config... (sys/config/)"
python tools/fscp.py built/flopnix.img --into sys/config/ config/*.cfg >/dev/null || exit 1
echo "   $(ls config/*.cfg | xargs -n1 basename | tr '\n' ' ')"

echo "$buildnum" > build.num
echo "== done! - build $buildnum"
echo "   built/flopnix.img  $(stat -c%s built/flopnix.img) bytes  (full OS image)"
echo "   built/flopnix.ku   $(stat -c%s built/flopnix.ku) bytes   (kernel update)"
echo "   built/kexts/       production extensions"
