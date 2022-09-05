# WebAssembly Bounds Checking performance analysis

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

## WASI C++ exception hack to get things to compile

```c++
#ifdef __wasi__
extern "C" {
  int __cxa_exception_buffer_storage[256];
  void* __cxa_allocate_exception(size_t s) {
    return __cxa_exception_buffer_storage;
  }
  void* __cxa_free_exception(void* e) {}

  void __cxa_throw(void* e, void* t, void(*d)(void*)) {
    fprintf(stderr, "Exception thrown: %p %d %d\n", e, __cxa_exception_buffer_storage[0], __cxa_exception_buffer_storage[1]);
    exit(1);
  }
}
#endif
```
