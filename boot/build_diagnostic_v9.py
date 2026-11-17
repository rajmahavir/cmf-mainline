#!/usr/bin/env python3
"""Build diagnostic-v9: v8 kernel + PATH fix so the ACM shell actually starts.

Output: /home/raj/mt6878-mainline/out/diagnostic-v7-review/
Reuses the v6 kernel build tree (copy, reflink where possible) and only relinks
the Image with a new embedded initramfs. Does not touch v6 artifacts.
"""
import gzip
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path('/home/raj/mt6878-mainline')
OUT = ROOT / 'out/diagnostic-v9-review'
SOURCE = ROOT / 'linux'
PREV = ROOT / 'out/diagnostic-v8-review'
INIT_SRC = ROOT / 'tools/qemu-system-review/v9-init.sh'
PRIVATE = Path('/home/raj/cmfkali-v2-build/host-tools')
PACKAGING = Path('/home/raj/cmfkali-v2-build/packaging')

BB_SHA = '4d60ab3f5a59ebb2ca863f2f514e6924401b581e9b64f602665c008177626651'
DTB_SHA = 'ce217bc0c30e0ba59700086bb75372cb84c096629f9c9558529bb618ce04c895'

CMDLINE = ('console=tty0 earlycon keep_bootcon clk_ignore_unused pd_ignore_unused '
           'panic=15 printk.devkmsg=on loglevel=8')


def run(args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def main():
    resume = '--resume' in sys.argv
    if resume:
        if not (OUT / 'kernel').is_dir():
            raise SystemExit('--resume requires an existing kernel build tree')
        print('[resume] reusing kernel build tree', flush=True)
    else:
        if OUT.exists():
            raise SystemExit(f'{OUT} already exists; refusing to overwrite (use --resume)')
        OUT.mkdir(parents=True)
        build = OUT / 'kernel'
        print('[1/8] copying v7 kernel build tree', flush=True)
        run(['cp', '-a', '--reflink=auto', PREV / 'kernel', build])
        run(['cp', '-a', '--reflink=auto', PREV / 'thinlto-cache', OUT / 'thinlto-cache'])
    build = OUT / 'kernel'

    print('[2/8] assembling initramfs root', flush=True)
    fsroot = OUT / 'root'
    for name in ('bin', 'proc', 'sys', 'dev', 'tmp', 'run'):
        (fsroot / name).mkdir(parents=True, exist_ok=True)
    bb = fsroot / 'bin/busybox'
    shutil.copy2(PREV / 'root/bin/busybox', bb)
    bb.chmod(0o755)
    got = hashlib.sha256(bb.read_bytes()).hexdigest()
    assert got == BB_SHA, f'busybox hash {got} != {BB_SHA}'
    shutil.copy2(INIT_SRC, fsroot / 'init')
    (fsroot / 'init').chmod(0o755)

    spec = OUT / 'device-nodes.txt'
    spec.write_text(
        'dir /dev 0755 0 0\n'
        'nod /dev/console 0600 0 0 c 5 1\n'
        'nod /dev/null 0666 0 0 c 1 3\n'
        'nod /dev/kmsg 0600 0 0 c 1 11\n'
        'nod /dev/tty0 0600 0 0 c 4 0\n'
        'nod /dev/random 0666 0 0 c 1 8\n'
        'nod /dev/urandom 0666 0 0 c 1 9\n')

    print('[3/8] generating cpio', flush=True)
    archive = OUT / 'initramfs.cpio'
    run([SOURCE / 'usr/gen_initramfs.sh', '-o', archive, '-u', 'squash', '-g', 'squash',
         fsroot, spec], cwd=build)

    print('[4/8] patching config and rebuilding Image', flush=True)
    run([SOURCE / 'scripts/config', '--file', build / '.config',
         '--set-str', 'INITRAMFS_SOURCE', archive,
         '--set-str', 'CMDLINE', CMDLINE,
         '--enable', 'INITRAMFS_FORCE'])
    env = os.environ.copy()
    env['PATH'] = f'{PRIVATE}/perl/usr/bin:{PRIVATE}/make-4.3/bin:' + env['PATH']
    env['PERL5LIB'] = f'{PRIVATE}/perl/usr/lib/x86_64-linux-gnu/perl-base'
    base = [PRIVATE / 'make-4.3/bin/make', '-C', SOURCE, f'O={build}', 'ARCH=arm64', 'LLVM=1',
            f'PERL={PRIVATE}/perl/usr/bin/perl',
            f'LD=ld.lld --no-mmap-output-file --threads=1 --thinlto-jobs=1 '
            f'--thinlto-cache-dir={OUT}/thinlto-cache']
    run(base + ['olddefconfig'], env=env)
    jobs = os.environ.get('CMF_JOBS', '4')
    run(base + [f'-j{jobs}', 'Image'], env=env)

    print('[5/8] building fdt shim', flush=True)
    dtb = PREV / 'candidate.dtb'
    got = hashlib.sha256(dtb.read_bytes()).hexdigest()
    assert got == DTB_SHA, f'dtb hash {got} != {DTB_SHA}'
    shutil.copy2(dtb, OUT / 'candidate.dtb')

    image = build / 'arch/arm64/boot/Image'
    kernel = image.read_bytes()
    text_offset, effective, flags = struct.unpack_from('<QQQ', kernel, 8)
    assert kernel[56:60] == b'ARMd' and text_offset == 0 and len(kernel) <= effective
    wrapper_size = 0x200000 + effective
    asm = f'''.section .text
.global _start
_start:
    b _stub_entry
    .long 0
    .quad 0
    .quad {wrapper_size}
    .quad {flags}
    .quad 0, 0, 0
    .ascii "ARM\\x64"
    .long 0
_stub_entry:
    adr x0, .Ldtb
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    adr x4, .
    sub x4, x4, #0x50
    add x4, x4, #0x200, lsl #12
    br x4
.balign 4096
.Ldtb:
    .incbin "{OUT / 'candidate.dtb'}"
.org 0x200000
    .incbin "{image}"
'''
    (OUT / 'fdt_shim.S').write_text(asm)
    run(['clang', '--target=aarch64-linux-gnu', '-c', OUT / 'fdt_shim.S', '-o', OUT / 'fdt_shim.o'])
    (OUT / 'wrapper.ld').write_text(
        'SECTIONS { . = 0; .text : { *(.text) } /DISCARD/ : { *(.comment) *(.note*) } }\n')
    run(['ld.lld', '-T', OUT / 'wrapper.ld', '-e', '_start', OUT / 'fdt_shim.o',
         '-o', OUT / 'fdt_shim.elf'])
    run(['llvm-objcopy', '-O', 'binary', OUT / 'fdt_shim.elf', OUT / 'payload_raw.bin'])
    payload = (OUT / 'payload_raw.bin').read_bytes()
    assert payload[0x200000:] == kernel, 'embedded kernel mismatch'
    (OUT / 'payload.bin.gz').write_bytes(gzip.compress(payload, compresslevel=9, mtime=0))

    print('[6/8] packaging boot image', flush=True)
    boot = OUT / 'boot.img'
    run(['python3', PACKAGING / 'tools/mkbootimg/mkbootimg.py', '--header_version', '4',
         '--kernel', OUT / 'payload.bin.gz', '--output', boot])

    print('[7/8] signing with AVB', flush=True)
    avb = PACKAGING / 'tools/avb/avbtool.py'
    run(['python3', avb, 'add_hash_footer', '--image', boot, '--partition_name', 'boot',
         '--partition_size', '67108864', '--key', PACKAGING / 'keys/candidate-avb.pem',
         '--algorithm', 'SHA256_RSA2048', '--salt',
         'b16a0ade6b083b84412dbb52857e7116386b9ca2633d8fd4fab1a7d6372b2e62', '--flags', '0',
         '--prop', 'com.android.build.boot.os_version:14',
         '--prop', 'com.android.build.boot.fingerprint:alps/hal_mgvi_64_64only_ww_armv82/mgvi_64_64only_ww_armv82:14/UP1A.231005.007/2609281140:user/release-keys',
         '--prop', 'com.android.build.boot.security_patch:2026-04-05'])
    with (OUT / 'avb-verification.txt').open('w') as log:
        run(['python3', avb, 'verify_image', '--image', boot], stdout=log, stderr=subprocess.STDOUT)

    print('[8/8] manifest', flush=True)
    names = ['boot.img', 'payload_raw.bin', 'payload.bin.gz', 'candidate.dtb',
             'initramfs.cpio', 'fdt_shim.S', 'fdt_shim.elf', 'device-nodes.txt']
    manifest = {name: {'bytes': (OUT / name).stat().st_size,
                       'sha256': hashlib.sha256((OUT / name).read_bytes()).hexdigest()}
                for name in names}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
