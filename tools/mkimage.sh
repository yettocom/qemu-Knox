#!/bin/bash
# Turn the DDK's GKI vmlinux into a flat arm64 Image QEMU can boot.
#   tools/mkimage.sh <kmi> <tag>      e.g. tools/mkimage.sh android13-5.15 5.15
set -euo pipefail
KMI="$1"
TAG="$2"
docker run --rm -v /mnt/d/root/qemu-knox/image:/out "ghcr.io/ylarod/ddk-min:$KMI" bash -lc "
set -e
OBJCOPY=\$(ls /opt/ddk/clang/*/bin/llvm-objcopy | head -1)
echo \"objcopy: \$OBJCOPY\"
\$OBJCOPY -O binary /opt/ddk/kdir/$KMI/vmlinux /out/Image-$TAG
ls -l /out/Image-$TAG
python3 - <<'EOF'
d = open('/out/Image-$TAG', 'rb').read(64)
print('head  ', d[:8].hex())
print('magic ', d[56:60])
EOF
"
