#!/usr/bin/env python3

import os
import sys
import subprocess
from os import path
from pathlib import Path
import argparse
import platform

WASMBOUNDS_DIR = Path(path.normpath(path.join(path.dirname(__file__), "..")))
POLYBENCH_DIR = WASMBOUNDS_DIR / 'polybench-c-4.2'


def main():
    parser = argparse.ArgumentParser(
        description="PolybenchC build helper script")
    parser.add_argument('-c', '--cc', help='C compiler path',
                        type=str, default='clang')
    parser.add_argument('-f', '--cflags', help='C compiler flags',
                        type=str, action='append', default=[])
    parser.add_argument('-o', '--out-dir', help='Output directory',
                        type=Path, default=WASMBOUNDS_DIR / 'binaries')
    parser.add_argument('-n', '--dry-run',
                        help='Do not invoke the compiler', action='store_true')
    parser.add_argument('-d', '--dest-extension',
                        help='File extension for the produced binaries', type=str, default='')
    args = parser.parse_args()

    all_benchmarks = (POLYBENCH_DIR / 'utilities' /
                      'benchmark_list').read_text().strip().splitlines()

    all_benchmarks.append('../noopbench.c')
    all_benchmarks.append('../mallocbench.c')

    cc: str = args.cc
    cflags = args.cflags
    out_dir: Path = args.out_dir
    dry_run: bool = bool(args.dry_run)
    dest_ext: str = args.dest_extension

    print(f"Target directory: {out_dir}")
    if not dry_run:
        os.makedirs(out_dir, exist_ok=True)

    if '.wasm' in dest_ext:
        cflags.append('--target=wasm32-wasi')
        # cflags += ['-nodefaultlibs', '-Wl,-lc']
        if Path('/opt/wasi-sdk/share/wasi-sysroot').is_dir():
            cflags.append('--sysroot=/opt/wasi-sdk/share/wasi-sysroot')

    utilfile = str(POLYBENCH_DIR / 'utilities' / 'polybench.c')
    march = 'native'
    if ('clang' in cc) and ('.wasm' not in dest_ext):
        if platform.machine() == 'aarch64':
            march = 'armv8'
            if 'Ubuntu SMP' in platform.version():
                cflags.append('-mcpu=thunderx2t99')
            else:
                cflags.append('-mcpu=cortex-a53')
    if platform.machine() == 'riscv64' and ('.wasm' not in dest_ext):
        march = 'rv64gc'
    for nth, srcfile in enumerate(all_benchmarks):
        srcfile = (POLYBENCH_DIR / srcfile).resolve()
        srcfstem = srcfile.stem
        dstfile = out_dir / (srcfstem + dest_ext)
        print(
            f"Compiling {nth+1:2}/{len(all_benchmarks)} {srcfstem} with {cc} to {dstfile}")
        if not dry_run:
            subprocess.run([cc, '-o', dstfile, srcfile, utilfile,
                            '-std=c11', '-lm', '-O3', f'-march={march}',
                            '-D_POSIX_C_SOURCE=200809L',
                            '-DPOLYBENCH_NO_FLUSH_CACHE=1',
                            '-DPOLYBENCH_USE_RESTRICT=1',
                            '-DPOLYBENCH_USE_C99_PROTO=1',
                            '-DMEDIUM_DATASET=1',
                            '-I' + str(POLYBENCH_DIR / 'utilities')
                            ] + cflags)

    pass


if __name__ == '__main__':
    main()
