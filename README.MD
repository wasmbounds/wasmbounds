# Leaps and bounds: Analysing WebAssembly’s performance with a focus on bounds checking - Source code and experimental data bundle

This is the code&data bundle accompanying the IISWC2022 paper "Leaps and bounds: Analysing WebAssembly’s performance with a focus on bounds checking" by Raven Szewczyk, Kim Stonehouse, Antonio Barbalace and Tom Spink.

A list of the specific code and data files in the bundle follows, with instructions for reproducing the results of the paper afterwards.

# Contents of the bundle

1. `graphs/plots`: Plots used in the paper
2. `graphs/wasmbounds.Rmd`, `graphs/knitall.R`: R source code used to generate the plots
3. `runs/kone.csv`, `runs/sole.csv`, `runs/riscv.csv`: Raw experimental data used to generate the plots in the CSV (comma-separated value) format (kone = x86_64 machine, sole = armv8 machine, riscv = Nezha D1 RISC-V development board)
   - These are gzipped in the GitHub version of this archive due to file size limitations, make sure to unzip them before using any of the scripts
4. `patches-{polybenchc,speccpu2017}.patch`: Our patches to the Polybench/C and SPECcpu2017 benchmark suites to make them compatible with WebAssembly
5. `patches-{nodev8,wasm3,wasmtime,wavm}.patch`: Our patches to the WebAssembly runtimes evaluated in the paper adding support for alternative bounds checking methods and integrating them with our benchmarking harness
6. `runner-src/*`, `CMakeLists.txt`, `CMakePresets.json`: C++ Source code for the benchmarking harness, including the UserfaultFD memory arena manager
7. `statmon/*`: Rust source code for the system resource usage and CPU performance counter monitoring tool used to generate the data for the paper
8. `scripts/*`, `*.sh`: Utility scripts for compiling the benchmark suites, harness and running benchmarks
9. `Dockerfile.*`, `Bakefile-*.hcl`: Docker container definitions for reproducible benchmark running conditions
10. `cpp-libs/fmt`, `node-v18.2.0`, `polybench-c-4.2`, `wasm3-0.5.0`, `wasmtime`, `WAVM` - already patched open source third party libraries and WebAssembly runtimes used in the paper
11. Not included: `spec-cpu` - an installation of the SPECcpu2017 benchmark suite, not included due to licensing restrictions but patches are provided for users who hold a license

# Reproducing experimental results
## Format of the CSV experimental data

The machines used for experiments in the CSV files provided in `runs/*.csv` are:
 - kone: x86_64 Intel Xeon Gold 6230R, with 16 hardware threads enabled, 768 GiB of system memory
 - sole: Armv8/AArch64 Cavium ThunderX2 CN9980 v2.2 configured to have 16 hardware threads, 256 GiB of system memory
 - riscv: Nezha D1 1GB development board, with the XuanTie C906 CPU, single core and hardware thread

The first line of the csv file is the header giving column names to the rest of the rows of data. For example:
```
benchid,runid,benchname,runner,nthreads,bounds,local_load_avg1,local_load_avg1_min,local_load_avg1_max, ... , mus_0,mus_1,mus_2,mus_3
```
The key fields are: 0,0,correlation,native.gcc,1,none,
 - `benchid` (e.g. 4): sequential number of the benchmark
 - `runid` (e.g. 123): sequential number of the configuration (unique)
 - `benchname` (e.g. `correlation`): name of the benchmark, SPEC benchmarks begin with `rundirs/` while Polybench/C benchmarks don't have a `/` in their name
 - `runner` (e.g. `native.gcc`): name of the runtime used (nodejs/native/wavm/etc.), and compiler/executable format (gcc/clang/wasm)
 - `nthreads` (e.g. 4): number of threads of the test ran in parallel
 - `bounds` (e.g. `trap`): bounds checking method used in the WebAssembly runtime
 - `local_{VARIABLE}_{STAT FUNCTION}` - values of performance counters and system metrics collected by the system monitoring tool, aggregated over the duration of the benchmark using different functions (min/max/average/sum/count/etc.)
 - `mus_{number}` - microseconds taken to run the benchmark, in numbered data samples

Each following row consists of fields corresponding to the columns defined above, some rows have fewer columns if fewer data samples were collected (longer running benchmarks get executed fewer times).

## Required hardware

 - Generating plots with R requires about 10 GiB free system memory while parsing the input CSV files
 - The Polybench/C benchmarks have been successfuly run on the RISC-V system with 1GiB of memory (singlethreaded), while SPECcpu2017 requires up to 2GiB per thread run
 - Plotting software and the WASM compiler run on the x86_64 architecture
 - The benchmarks and WebAssembly runtimes run on x86_64, Armv8 (AArch64) and RISC-V 64GC architectures (some WebAssembly runtimes don't successfully run on RISC-V)

## Required software

The required software for reproducing the results is as follows:

 - A recent Linux distribution is assumed as the operating system, e.g. Ubuntu 22.04 LTS
 - For the plotting: [R 4.2.1](https://www.r-project.org/), with the [tidyverse](https://www.tidyverse.org/) and other libraries, specifically: `tidyverse`, `rio`, `scales`, `xtable`, `ggbreak`, `patchwork`, `rmarkdown`, `xfun` all available via the R package repository.
 - For system monitoring (optional, recommended for full results): [Rust compiler v1.62.0, Cargo](https://www.rust-lang.org/), easiest to install using [rustup](https://rustup.rs/)
 - For compiling and running benchmarks via containers: [Docker](https://docs.docker.com/engine/) - Dockerfiles are provided which will fetch, compile and run all necessary dependencies
 - For compiling and running benchmarks on bare metal (to avoid skewing performance numbers by the overhead of containers):
   - For Ubuntu 22.04-specific installation instructions see the commands in the Dockerfiles included in the repository
   - [Python 3.10](https://www.python.org/)
   - [Docker 20.10](https://docs.docker.com/engine/install/)
   - [Linux kernel, version at least 5.7](https://kernel.org/)
   - [GCC 11.2](https://gcc.gnu.org/) for native benchmark compilation
   - [LLVM 11 + Clang SDK](https://llvm.org/) for the WAVM runtime
   - [Clang 13](https://llvm.org/) for native benchmark compilation
   - [WASI SDK 15](https://github.com/WebAssembly/wasi-sdk/releases)
   - [CMake 3.22](https://cmake.org/download/)
   - [Boost 1.79.0](https://www.boost.org/), [Abseil C++ 20220623.1](https://github.com/abseil/abseil-cpp/releases)

## Generating the plots used in the paper from the provided csv files

With R and the required packages installed via the `install.packages("...")` command, run the following in the system shell (sh/bash/zsh):

```sh
cd graphs
Rscript ./knitall.R
```

The plots will be generated as `graphs/plots/*.pdf` files, the naming convention for them is `wasmbounds_[MACHINE]_[VARIABLE]_[CONFIGURATION].pdf`.
The `knitall.R` script "knits" the `wasmbounds.Rmd` file in the same folder three times, switching out the `machine` parameter.
The Rmd file reads its input data from `../runs/MACHINE.csv` relative to the working directory.

The `knitall.R` script will also generate three pdfs named `graphs/wasmbounds-[MACHINE].pdf`, these collect all the graphs used and any R warnings or errors that may occur when generating them.
Due to the differences in configuration between the risc-v board and sole/kone, and different performance counters exposed on each of the CPUs some warnings and blank plots are expected -
the scripts blindly try to generate them for all configurations (1/4/16 threads, all performance counters) even though not all of them are present in all machine files (e.g. multithreaded configurations are missing from the risc-v runs).

## Compiling and running `statmon`, the system monitoring tool

In a shell run the following:
```bash
cd statmon
cargo build --release
# Root permission might not be needed if unprivileged access to performance counters is enabled via the relevant sysctls in Linux
sudo ./target/release/statmon --port 8125 --host-prefix local --netdev lo
```

This compiles and runs the daemon, listening on port 8125, monitoring the CPU and kernel performance counters and the `lo` device (localhost loopback) network traffic.

## Importing pre-built toolchain and benchmark containers

To load the OCI images into the local Docker installation for a specific architecture use the following commands:
```bash
docker load -i wasmbounds-runtime-base.ARCHITECTURE.tar
docker load -i wasmbounds-toolchain-base.ARCHITECTURE.tar
docker load -i wasmbounds-runners.ARCHITECTURE.tar
```

## Building the benchmark binaries using the Docker container

Before running the benchmarks, they need to be compiled with optimizations for the local machine. To do this, run the following commands:
```bash
./build_binaries.sh polybenchc
# If SPECcpu2017 was installed into spec-cpu and patched using the provided patch file:
./build_binaries.sh spec
```

The WASI SDK is only provided for x86-64 architectures, so the WebAssembly binaries (in `binaries/*.wasm` and `rundirs/` for SPECcpu2017) have to be copied over from a x86-64 machine if building on another architecture.

## Running the benchmarks using the Docker container

### Trial runs
First, do a dry run to make sure the benchmarking scripts work:
```bash
# Use --suites polybenchc,spec.train if SPECcpu2017 is installed
./benchrunner.sh --monitor-host 127.0.0.1 --monitor-port 8125 --output-dir runs --dry-run --suites polybenchc
```

Confirm that a dry run file appears in the `runs/` directory, named `benchrunner-HOSTNAME-dry-DATE-TIME.log`.

Then, we will do a quick run of each benchmark with no repetitions or warmup, to make sure every single one loads and executes correctly:
```bash
# Use --suites polybenchc,spec.train if SPECcpu2017 is installed
./benchrunner.sh --monitor-host 127.0.0.1 --monitor-port 8125 --output-dir runs --one-run --min-seconds 0 --min-runs 1 --suites polybenchc
```

### Full run
To run the full suite with warm-up and multiple runs as done in the paper:
```bash
# Use --suites polybenchc,spec.train if SPECcpu2017 is installed.
# Polybench/C benchmarks run very quickly, while SPECcpu2017 take a lot of time, therefore
# the min seconds and min runs parameters are the main controls for the runtime of each of the suites respectively.
./benchrunner.sh --monitor-host 127.0.0.1 --monitor-port 8125 --output-dir runs --one-run --min-seconds 20 --min-runs 10 --suites polybenchc
```
Take note of the date and time the suite was run, and make sure to keep the corresponding `runs/[...].log` file safe.

### Preparing the csv file for R plotting code
To convert the full logfile into the abbreviated csv file used for plotting, use the following command:
```
./scripts/log2csv.py benchrunner-HOST-regular-DATE-TIME.log ./runs/myrun.csv
```
The short `knitall.R` script contains an array of machines plots are made for, add `myrun` to it to generate plots from your experiment run(s).

### Details on benchrunner parameters
The usage information can be printed by running `./benchrunner.sh --help`, or `./scripts/benchrunner.py --help` if the required native dependencies are installed. A copy of the options list is provided here for convenience:
```
usage: benchrunner.py [-h] [-n] [-T] [-1] [-4] [-m MONITOR_HOST]
                      [-p MONITOR_PORT] [-s MIN_SECONDS] [-R RESTART_FROM]
                      [-r MIN_RUNS] [-x RUNNERS] [-o OUTPUT_DIR] [-f FILTER]
                      [-g RUNNER_FILTER] [-S SUITES]

Benchmark runner script

options:
  -h, --help            show this help message and exit
  -n, --dry-run         Do not run the benchmarks
  -T, --try-run         Only run the noop benchmark, only 2 threads
  -1, --one-run         Only run 1 bounds checking configuration, 1 thread
  -4, --four-run        Only run 1 bounds checking configuration, 4 thread
  -m MONITOR_HOST, --monitor-host MONITOR_HOST
                        Statmon socket for cpu monitoring - host/ip
  -p MONITOR_PORT, --monitor-port MONITOR_PORT
                        Statmon socket for cpu monitoring - port number
  -s MIN_SECONDS, --min-seconds MIN_SECONDS
                        Minimum seconds to run each benchmark instance for
  -R RESTART_FROM, --restart-from RESTART_FROM
                        Restart from given run# if the previous run aborted
                        early
  -r MIN_RUNS, --min-runs MIN_RUNS
                        Minimum number of times to run each benchmark instance
                        for (per thread)
  -x RUNNERS, --runners RUNNERS
                        Path to compiled runner binaries
  -o OUTPUT_DIR, --output-dir OUTPUT_DIR
                        Path to place the output log in
  -f FILTER, --filter FILTER
                        Benchmark filter (only use benchmarks containing this
                        substring in their name)
  -g RUNNER_FILTER, --runner-filter RUNNER_FILTER
                        Runner filter (only use runners containing this
                        substring in their name)
  -S SUITES, --suites SUITES
                        Benchmark suite[s] to run
                        [noopbench,polybenchc,spec.train,spec.test,spec]
```

The statmon socket parameters are optional, if not provided the cpu and system performance counter measurements will not be performed.

# Inspecting, modifying and extending the benchmark suite
## Structure of the benchmark source code

Each of the WebAssembly (Wasm) runtimes and benchmark suites lives in a separate folder named after the upstream project.
As much as it was possible, we made minimum modifications to the runtimes and instead put most of the shared code in the `runner-src/` directory.

Each Wasm runtime has a corresponding benchmark runner executable, built from the `runner-src/impl_NAME.cpp` C++ source code, the build is defined in the `CMakeLists.txt` file at the root of the bundle.

`runner-src/runner.cpp` has the main benchmarking loop, which calls the setup and run functions for each runner. The runner interface `RunnerImpl` is defined in `runner-src/runner.h` file, it provides 3 main functions: the constructor `RunnerImpl(const RunnerOptions &opts)` initializing the object, called once per thread; `void prepareRun(const RunnerOptions &opts)` which is executed before each execution step untimed; and `void runOnce(const RunnerOptions &opts)` which is executed at each step, timed by the runner loop.

To implement bounds checking methods like `mprotect` and `uffd` control over the memory allocation is needed, so we implemented a small library `runner-src/vm-library` which provides header files for C and C++ at `wasmbounds_rr.h[pp]`, defining memory allocation, resizing and deallocation stubs allowing to use the same implementation across all tested WebAssembly runtimes.
The runtimes were patched to call into this library instead of their own platform abstraction layer for managing WebAssembly memory objects for the purpose of evaluating the impact of different bounds checking methods in our paper.

The difference between upstream projects and our patched versions can be seen either in the .patch files in our bundle, or as the difference between commit `56643dd094577b70d373009205e04df920eeba38` and the latest commit in the GitHub repository.

## Building the containers locally

For local development, the containers can be built and loaded into the local Docker instance using the following script:
```bash
./build_containers.sh
```

Note: If errors like `/usr/sbin/dpkg-split: No such file or directory` appear, see the steps in <https://github.com/docker/buildx/issues/495#issuecomment-761562905>.

To build the OCI images like provided in this artifact bundle, first set up cross-architecture docker buildx on your machine according to the documentation at <https://docs.docker.com/build/buildx/multiplatform-images/>.
Then, each set of images can be built from its Bakefile like this:
```
docker buildx bake -f Bakefile-ARCH.hcl
```

## Compiling the runners

In the toolchain container:
```bash
# To enter the container
./toolchain_shell.sh

mkdir -p runner-build/docker
cmake --preset docker
cmake --build --preset debug-docker
cmake --build --preset release-docker
```

Locally (bare metal):
First, copy CMakePresets.json into CMakeUserPresets.json, rename the provided presets and change the CFG_LLVM_DIR variable to point to the LLVM11 local installation's `lib/cmake/llvm` folder (e.g. `/usr/lib/llvm-11/lib/cmake/llvm`).
For more documentation on the syntax of CMakePresets.json, see <https://cmake.org/cmake/help/v3.22/manual/cmake-presets.7.html>.
Then:
```bash
mkdir -p runner-build/default
cmake --preset my-default
cmake --build --preset my-debug
cmake --build --preset my-release
```

## Running a single Polybench/C benchmark manually using the docker container

```bash
docker run --rm --privileged --mount type=bind,source="$(pwd)/binaries",target=/opt/wasmbounds/binaries wasmbounds-runners:latest /opt/wasmbounds/bin/wbrunner_native -t 4 -b none -s 1 -r 2 ../binaries/2mm.gcc
docker run --rm --privileged --mount type=bind,source="$(pwd)/binaries",target=/opt/wasmbounds/binaries wasmbounds-runners:latest /opt/wasmbounds/bin/wbrunner_wavm -t 1 -s 0 -r 1 -b none ../binaries/lu.wasm
```
