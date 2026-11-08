#!/bin/bash
# Build the initramfs.  tools/mkinitramfs.sh <tag> ["module params"]
set -euo pipefail
TAG="${1:-5.15}"
PARAMS="${2:-target=__arm64_sys_newuname offset=0 skip=1}"
ROOT=/mnt/d/root/qemu-knox
CC=/usr/bin/aarch64-linux-gnu-gcc

cd "$ROOT"
rm -rf rootfs
mkdir -p rootfs/proc rootfs/sys rootfs/dev rootfs/lib rootfs/tmp

"$CC" -O2 -static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin \
	-march=armv8-a -o rootfs/init init/init.c
cp bench/knox_bench.ko rootfs/lib/knox_bench.ko
printf '%s' "$PARAMS" > rootfs/bench.conf

cd rootfs
find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 > "$ROOT/build/initramfs-$TAG.cpio.gz"
cd "$ROOT"
ls -l "build/initramfs-$TAG.cpio.gz" rootfs/init rootfs/lib/knox_bench.ko
