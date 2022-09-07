# Leaps and bounds: Analysing WebAssembly’s performance with a focus on bounds checking - Source code and experimental data bundle

This is the code&data bundle accompanying the IISWC2022 paper "Leaps and bounds: Analysing WebAssembly’s performance with a focus on bounds checking" by Raven Szewczyk, Kim Stonehouse, Antonio Barbalce and Tom Spink.

A list of the specific code and data files in the bundle follows, with instructions for reproducing the results of the paper afterwards.

## Contents of the bundle

1. `graphs/plots`: Plots used in the paper
2. `graphs/wasmbounds.Rmd`, `graphs/knitall.R`: R source code used to generate the plots
3. `runs/kone.csv`, `runs/sole.csv`, `runs/riscv.csv`: Raw experimental data used to generate the plots in the CSV (comma-separated value) format (kone = x86_64 machine, sole = armv8 machine, riscv = Nezha D1 RISC-V development board)
4. `patches-{polybenchc,speccpu2017}.patch`: Our patches to the Polybench/C and SPECcpu2017 benchmark suites to make them compatible with WebAssembly
5. `patches-{nodev8,wasm3,wasmtime,wavm}.patch`: Our patches to the WebAssembly runtimes evaluated in the paper adding support for alternative bounds checking methods and integrating them with our benchmarking harness
6. `runner-src/*`, `CMakeLists.txt`, `CMakePresets.json`: C++ Source code for the benchmarking harness, including the UserfaultFD memory arena manager
7. `statmon/*`: Rust source code for the system resource usage and CPU performance counter monitoring tool used to generate the data for the paper
8. `scripts/*`, `*.sh`: Utility scripts for compiling the benchmark suites, harness and running benchmarks
9. `Dockerfile.*`, `Bakefile-*.hcl`: Docker container definitions for reproducible benchmark running conditions
10. `cpp-libs/fmt`, `node-v18.2.0`, `polybench-c-4.2`, `wasm3-0.5.0`, `wasmtime`, `WAVM` - already patched open source third party libraries and WebAssembly runtimes used in the paper
11. Not included: `spec-cpu` - an installation of the SPECcpu2017 benchmark suite, not included due to licensing restrictions but patches are provided for users who hold a license

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
 - For compiling and running benchmarks bare metal (to avoid skewing performance numbers by the overhead of containers):
   - For Ubuntu 22.04-specific installation instructions see the commands in the Dockerfiles included in the repository
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

## Building toolchain container

```bash
./build_containers.sh
```

## Building runner

Locally:
```bash
mkdir -p runner-build/default
cmake --preset default
cmake --build --preset debug
cmake --build --preset release
```

In container:
```bash
mkdir -p runner-build/docker
cmake --preset docker
cmake --build --preset debug-docker
cmake --build --preset release-docker
```

You can create your own local cmake configuration if the default one doesn't work by making a CMakeUserPresets.json file (same format as CMakePresets.json).

## Building all benchmarks

```bash
./build_binaries.sh # This runs everything through the toolchain docker container
# Build only one benchmark suite:
./build_binaries.sh polybenchc
./build_binaries.sh spec
```

It will output all binaries to the `binaries` folder. Native builds are called `bench.gcc` and `bench.clang`, while WASM modules are called `bench.wasm`.

## Running a single benchmark manually

```bash
docker run --rm --privileged --mount type=bind,source="$(pwd)/binaries",target=/opt/wasmbounds/binaries wasmbounds-runners:latest /opt/wasmbounds/bin/wbrunner_native -t 4 -b none -s 1 -r 2 ../binaries/2mm.gcc
docker run --rm --privileged --mount type=bind,source="$(pwd)/binaries",target=/opt/wasmbounds/binaries wasmbounds-runners:latest /opt/wasmbounds/bin/wbrunner_wavm -t 1 -s 0 -r 1 -b none ../binaries/lu.wasm
```
