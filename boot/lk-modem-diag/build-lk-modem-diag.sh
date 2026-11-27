#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "Usage: $0 KERNEL_SOURCE OUTPUT_DIRECTORY [--length-only|--prefix|--control]" >&2
    exit 2
fi

control_flag=
case "${3:-}" in
    "") diagnostic=lk-modem-diag.c ;;
    --length-only) diagnostic=lk-modem-length.c ;;
    --prefix) diagnostic=lk-modem-prefix.c ;;
    --control) diagnostic=lk-modem-diag.c; control_flag=-DCMF_MODEM_CONTROL ;;
    *) echo "Unknown option: $3" >&2; exit 2 ;;
esac

source_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
libfdt=$(CDPATH='' cd -- "$1/scripts/dtc/libfdt" && pwd)
mkdir -p -- "$2"
cd -- "$2"

for source in "$source_dir/lk-modem-diag.S" "$source_dir/$diagnostic" \
    "$source_dir/lk-modem-diag-string.c" \
    "$libfdt/fdt.c" "$libfdt/fdt_ro.c" "$libfdt/fdt_wip.c" "$libfdt/fdt_addresses.c"
do
    object=$(basename "$source")
    clang --target=aarch64-linux-gnu -march=armv8-a -mno-outline-atomics \
        -mgeneral-regs-only -Oz -ffreestanding -fno-stack-protector \
        -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -fdata-sections -ffunction-sections $control_flag -I "$source_dir/include" -I "$libfdt" \
        -c "$source" -o "$object.o"
done
ld.lld --gc-sections --no-undefined -T "$source_dir/lk-modem-diag.ld" \
    lk-modem-diag.S.o "$diagnostic.o" lk-modem-diag-string.c.o \
    fdt.c.o fdt_ro.c.o fdt_wip.c.o fdt_addresses.c.o -o lk-modem-diag.elf
llvm-objcopy -O binary lk-modem-diag.elf lk-modem-diag.bin
