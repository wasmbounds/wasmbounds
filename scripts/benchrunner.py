#!/usr/bin/env python3

import os
import sys
import subprocess
import socket as sk
import multiprocessing
from sys import stderr, stdout
from os import path
from pathlib import Path
import argparse
import datetime


WASMBOUNDS_DIR = Path(path.normpath(path.join(path.dirname(__file__), "..")))
POLYBENCH_DIR = WASMBOUNDS_DIR / 'polybench-c-4.2'
BINARIES_DIR = WASMBOUNDS_DIR / 'binaries'


def sock_recvline(s: sk.socket):
    data = b''
    while not data.endswith(b'\n'):
        data += s.recv(8192)
    return data


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark runner script")
    parser.add_argument('-n', '--dry-run',
                        help='Do not run the benchmarks', action='store_true')
    parser.add_argument('-T', '--try-run',
                        help='Only run the noop benchmark, only 2 threads', action='store_true')
    parser.add_argument('-1', '--one-run',
                        help='Only run 1 bounds checking configuration, 1 thread', action='store_true')
    parser.add_argument('-4', '--four-run',
                        help='Only run 1 bounds checking configuration, 4 thread', action='store_true')
    parser.add_argument('-m', '--monitor-host',
                        help='Statmon socket for cpu monitoring - host/ip')
    parser.add_argument('-p', '--monitor-port', default=8125, type=int,
                        help='Statmon socket for cpu monitoring - port number')
    parser.add_argument('-s', '--min-seconds', default=10, type=int,
                        help='Minimum seconds to run each benchmark instance for')
    parser.add_argument('-R', '--restart-from', default=0, type=int,
                        help='Restart from given run# if the previous run aborted early')
    parser.add_argument('-r', '--min-runs', default=10, type=int,
                        help='Minimum number of times to run each benchmark instance for (per thread)')
    parser.add_argument('-x', '--runners',
                        help='Path to runners', type=Path, default='/opt/wasmbounds/bin/' if path.exists('/opt/wasmbounds') else (WASMBOUNDS_DIR / 'runner-build' / 'default' / 'Release'))
    parser.add_argument('-o', '--output-dir', type=Path,
                        help='Path to place the output log in')
    parser.add_argument('-f', '--filter', type=str, default='',
                        help='Benchmark filter')
    parser.add_argument('-g', '--runner-filter', type=str, default='',
                        help='Runner filter')
    parser.add_argument('-S', '--suites', type=str, default='noopbench,polybenchc,spec',
                        help='Benchmark suite[s] to run [noopbench,polybenchc,spec]')
    args = parser.parse_args()

    dry_run: bool = bool(args.dry_run)
    one_run: bool = bool(args.one_run)
    four_run: bool = bool(args.four_run)
    try_run: bool = bool(args.try_run)
    monitor_host: str = args.monitor_host
    monitor_port: str = args.monitor_port
    runners_path: Path = args.runners
    min_seconds = int(args.min_seconds)
    min_runs = int(args.min_runs)
    restart_from = int(args.restart_from)
    output_dir: Path = args.output_dir
    suites = set(args.suites.split(','))
    bfilter: str = args.filter
    rfilter: str = args.runner_filter

    if not output_dir.is_dir():
        parser.print_help()
        raise RuntimeError('Output directory is not a directory')

    monitor_sock = None
    if monitor_host and len(monitor_host) > 0:
        monitor_sock = sk.create_connection(
            (monitor_host, monitor_port), timeout=1.0)
        print(
            f'Connected to monitoring at {monitor_sock.getsockname()}', file=stderr)

    if try_run:
        min_seconds = 0
        min_runs = 2

    mydate = datetime.datetime.now().strftime('%F_%H%M%S')
    runtype = 'regular'
    if dry_run:
        runtype = 'dry'
    elif try_run:
        runtype = 'try'
    hostname = sk.gethostname()
    if 'HOST_HOSTNAME' in os.environ:
        hostname = os.environ['HOST_HOSTNAME']
    output_path = output_dir / f'benchrunner-{hostname}-{runtype}-{mydate}.log'
    output_file = open(output_path, mode='w')
    print(f'Outputting to {output_path}', file=stderr)

    polybench_benchmark_srcs = (POLYBENCH_DIR / 'utilities' /
                                'benchmark_list').read_text().strip().splitlines()
    all_benchmarks = []
    if 'noopbench' in suites:
        all_benchmarks += ['noopbench']
    if not try_run and 'polybenchc' in suites:
        all_benchmarks += [(POLYBENCH_DIR / cpath).resolve().stem
                           for cpath in polybench_benchmark_srcs]
    if not try_run and 'spec' in suites:
        # !directory!exename!ar!gs
        all_benchmarks.append('!rundirs/505.mcf_r!mcf_r!inp.in')
        all_benchmarks.append(
            '!rundirs/508.namd_r!namd_r!--input!apoa1.input!--output!apoa1.ref.output!--iterations!65')
        all_benchmarks.append(
            '!rundirs/519.lbm_r!lbm_r!3000!reference.dat!0!0!100_100_130_ldc.of')
        all_benchmarks.append(
            '!rundirs/525.x264_r!x264_r!--pass!1!--stats!x264_stats.log!--bitrate!1000!--frames!1000!-o!BuckBunny_New.264!BuckBunny.264!1280x720')
        all_benchmarks.append('!rundirs/531.deepsjeng_r!deepsjeng_r!ref.txt')
        all_benchmarks.append('!rundirs/541.leela_r!leela_r!ref.sgf')
        all_benchmarks.append('!rundirs/544.nab_r!nab_r!1am0!1122214447!122')
        all_benchmarks.append(
            '!rundirs/557.xz_r!xz_r!input.combined.xz!250!a841f68f38572a49d86226b7ff5baeb31bd19dc637a922a972b2e6d1257a890f6a544ecab967c313e370478c74f760eb229d4eef8a8d2836d233d3e9dd1430bf!40401484!41217675!7')
    if not try_run and 'spec.test' in suites:
        # !directory!exename!ar!gs
        all_benchmarks.append('!rundirs/505.mcf_r.test!mcf_r!inp.in')
        all_benchmarks.append(
            '!rundirs/508.namd_r.test!namd_r!--input!apoa1.input!--output!apoa1.test.output!--iterations!1')
        all_benchmarks.append(
            '!rundirs/519.lbm_r.test!lbm_r!20!reference.dat!0!1!100_100_130_cf_a.of')
        all_benchmarks.append(
            '!rundirs/525.x264_r.test!x264_r!--dumpyuv!50!--frames!156!-o!BuckBunny_New.264!BuckBunny.yuv!1280x720')
        all_benchmarks.append('!rundirs/531.deepsjeng_r.test!deepsjeng_r!test.txt')
        all_benchmarks.append('!rundirs/541.leela_r.test!leela_r!test.sgf')
        all_benchmarks.append('!rundirs/544.nab_r.test!nab_r!hkrdenq!1930344093!1000')
        all_benchmarks.append(
            '!rundirs/557.xz_r.test!xz_r!cpu2006docs.tar.xz!4!055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae!1548636!1555348!0')
    if not try_run and 'spec.train' in suites:
        # !directory!exename!ar!gs
        all_benchmarks.append('!rundirs/505.mcf_r.train!mcf_r!inp.in')
        all_benchmarks.append(
            '!rundirs/508.namd_r.train!namd_r!--input!apoa1.input!--iterations!7!--output!apoa1.train.output')
        all_benchmarks.append(
            '!rundirs/519.lbm_r.train!lbm_r!300!reference.dat!0!1!100_100_130_cf_b.of')
        all_benchmarks.append(
            '!rundirs/525.x264_r.train!x264_r!--dumpyuv!50!--frames!142!-o!BuckBunny_New.264!BuckBunny.yuv!1280x720')
        all_benchmarks.append('!rundirs/531.deepsjeng_r.train!deepsjeng_r!train.txt')
        all_benchmarks.append('!rundirs/541.leela_r.train!leela_r!train.sgf')
        all_benchmarks.append('!rundirs/544.nab_r.train!nab_r!gcn4dna!1850041461!300')
        all_benchmarks.append(
            '!rundirs/557.xz_r.train!xz_r!input.combined.xz!40!a841f68f38572a49d86226b7ff5baeb31bd19dc637a922a972b2e6d1257a890f6a544ecab967c313e370478c74f760eb229d4eef8a8d2836d233d3e9dd1430bf!6356684!-1!8')


    if not path.isfile(runners_path / 'wbrunner_native'):
        raise RuntimeError('Invalid runners path')

    def run_bench(bname, type_ext='native.gcc', threads=1, bounds='none'):
        bext = type_ext[type_ext.index('.'):]
        runner_name = type_ext[:type_ext.index('.')]
        args = [str(runners_path / f'wbrunner_{runner_name}'),
                '--quiet',
                '--threads', str(threads),
                '--bounds-checks', bounds,
                '--min-seconds', str(min_seconds),
                '--min-runs', str(min_runs)
                ]
        if bname.startswith('!'):
            bparts = bname.split('!')
            os.chdir(WASMBOUNDS_DIR / bparts[1])
            args.append(f'{bparts[2]}{bext}')
            args.append('--')
            if len(bparts) > 3:
                args += bparts[3:]
        else:
            args.append(str(BINARIES_DIR / f'{bname}{bext}'))
        if not dry_run:
            if monitor_sock:
                monitor_sock.sendall(b'[\n')
            result = subprocess.run(args, stdout=subprocess.PIPE)
            # result.check_returncode()
            bout = result.stdout.strip().decode('utf-8')
            if monitor_sock:
                monitor_sock.sendall(b']\n')
                mdata = sock_recvline(monitor_sock)
                print(mdata.strip().decode('utf-8'), ',',
                      sep='', end='', file=output_file)
            print(bout, flush=True, file=output_file)
        else:
            print(' '.join(args), file=output_file)

    numcpus = multiprocessing.cpu_count()
    cpunums = []
    if try_run:
        cpunums = [2]
    else:
        while numcpus >= 1:
            cpunums.append(numcpus)
            numcpus //= 4
        if cpunums[-1] != 1:
            cpunums.append(1)
        cpunums.reverse()
    numcpus = multiprocessing.cpu_count()
    if one_run:
        cpunums = [1]
    if four_run:
        cpunums = [4]

    rid = -1
    if restart_from == 0:
        print('benchid,runid,benchname,runner,nthreads,bounds,data', file=output_file)
    for nth, bench in enumerate(all_benchmarks):
        if len(bfilter) > 0 and bfilter not in bench:
            continue
        for nthreads in cpunums:
            for runner in ['native.gcc', 'native.clang', 'wavm.wasm', 'wasmtime.wasm', 'wasm3.wasm', 'nodejs.wasm']:
                if len(rfilter) > 0 and rfilter not in runner:
                    continue
                bounds_set = ['none', 'trap', 'clamp', 'mdiscard', 'uffd']
                if '.wasm' not in runner:
                    bounds_set = ['none']
                elif one_run:
                    bounds_set = ['mdiscard']
                elif runner == 'wasm3.wasm':
                    bounds_set = ['mdiscard']
                    # Skip very slow SPEC benchmarks under the interpreter
                    if '!rundirs/5' in bench:
                        continue
                for bounds in bounds_set:
                    rid += 1
                    if rid < restart_from:
                        continue
                    time = datetime.datetime.now().strftime("%F %H:%M:%S")
                    benchname = bench if not bench.startswith(
                        '!') else bench.split('!')[1]
                    print(
                        f'Bench {nth+1:3}/{len(all_benchmarks)} Run# {rid:4} {benchname} {runner} Threads:{nthreads} Bounds:{bounds} ({time})', file=stderr)
                    print(
                        f'{nth},{rid},{benchname},{runner},{nthreads},{bounds},', end='', file=output_file)
                    run_bench(bench, runner, nthreads, bounds)

    if monitor_sock:
        monitor_sock.sendall(b'q\n\n')
        monitor_sock.shutdown(sk.SHUT_RDWR)
        monitor_sock.close()

    pass


if __name__ == '__main__':
    os.chdir(WASMBOUNDS_DIR)
    main()
