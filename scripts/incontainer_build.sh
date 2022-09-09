#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
cd "${THIS_DIR}/.."

if [[ "$1" == "polybenchc" || "$1" == "" ]]; then
    echo 'Native PolyBenchC-clang'
    ./scripts/build_polybench.py -c clang -d '.clang'

    echo 'Native PolyBenchC-gcc'
    ./scripts/build_polybench.py -c gcc -d '.gcc'

    if [[ $(uname -m) == "x86_64" ]]; then
        echo 'WASM PolyBenchC'
        ./scripts/build_polybench.py -c clang -d '.wasm'
    fi
fi

if [[ "$1" == "spec" || "$1" == "" ]]; then
    # Set up the run directories and create the compile scripts. This doesn't need to be done every time, but nothing bad will happen if it is.
    ./scripts/setup_spec.sh

    echo 'Native SPEC-clang'
    ./scripts/compile_spec.sh clang

    echo 'Native SPEC-gcc'
    ./scripts/compile_spec.sh gcc

    if [[ $(uname -m) == "x86_64" ]]; then
        echo 'WASM SPEC'
        ./scripts/compile_spec.sh wasm
    fi
fi

echo 'Fixing file permissions'
chown -R $(stat -c "%u:%g" .) ./binaries

echo 'Build done'