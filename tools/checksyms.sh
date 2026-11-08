#!/bin/bash
# Which of the symbols our module needs does this DDK kernel actually export?
#   tools/checksyms.sh [kmi]
set -uo pipefail
KMI="${1:-android13-5.15}"
docker run --rm -e KMI="$KMI" "ghcr.io/ylarod/ddk-min:$KMI" bash -lc '
K=/opt/ddk/kdir/$KMI
NM=$(ls /opt/ddk/clang/*/bin/llvm-nm | head -1)
PAT="[[:space:]](__set_fixmap|copy_to_kernel_nofault|init_mm|kernel_text_address|task_work_add|saved_boot_config|set_memory_nx|module_alloc|set_memory_ro|set_memory_x)[[:space:]]"
echo "=== Module.symvers hits:"
grep -cE "$PAT" $K/Module.symvers || true
grep -E "$PAT" $K/Module.symvers | head -12 || true
echo "=== __ksymtab_* in vmlinux:"
$NM $K/vmlinux 2>/dev/null | grep -E "__ksymtab_(__set_fixmap|init_mm|saved_boot_config|kernel_text_address)" || echo NONE
echo "=== plain symbols in vmlinux:"
$NM $K/vmlinux 2>/dev/null | grep -E " (__set_fixmap|init_mm|saved_boot_config|kernel_text_address)$" || echo NONE
'
