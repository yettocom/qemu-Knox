#!/bin/bash
# Boot the extracted Samsung Image under QEMU and capture the console.
#   BOOT_TIMEOUT=600 CPU=max MEM=2G SMP=4 APPEND="..." ./run.sh
set -uo pipefail
ROOT=/mnt/d/root/qemu-knox
QEMU=${QEMU:-qemu-system-aarch64}
export PATH=/usr/sbin:/usr/bin:/bin:$PATH
CPU=${CPU:-max}
MEM=${MEM:-2G}
SMP=${SMP:-4}
APPEND=${APPEND:-"console=ttyAMA0 earlycon=pl011,0x09000000 panic=0 nokaslr"}
LOG="$ROOT/logs/boot-$(date +%Y%m%d-%H%M%S).log"

mkdir -p "$ROOT/logs"
echo "[run] cpu=$CPU mem=$MEM smp=$SMP append=\"$APPEND\""
echo "[run] log=$LOG"

timeout "${BOOT_TIMEOUT:-600}" "$QEMU" \
	-M virt -cpu "$CPU" -m "$MEM" -smp "$SMP" \
	-nographic -no-reboot \
	-kernel "$ROOT/${KERNEL:-image/Image}" \
	-initrd "$ROOT/${INITRD:-build/initramfs.cpio.gz}" \
	-append "$APPEND" 2>&1 | tee "$LOG"

rc=${PIPESTATUS[0]}
echo "[run] qemu rc=$rc (124 = timeout)"
grep -c . "$LOG" | sed 's/^/[run] log lines: /'
