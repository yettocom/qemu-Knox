#!/bin/bash
# Build the bench module against one DDK KMI.
#   tools/buildbench.sh [kmi]
set -euo pipefail
KMI="${1:-android13-5.15}"
docker run --rm -v /mnt/d/root/qemu-knox/bench:/bench -e KMI="$KMI" 	"ghcr.io/ylarod/ddk-min:$KMI" bash -lc '
set -e
K=/opt/ddk/kdir/$KMI
make -C $K M=/bench ARCH=arm64 LLVM=1 LLVM_IAS=1 modules -j16 2>&1 | tail -20
ls -l /bench/knox_bench.ko
'
