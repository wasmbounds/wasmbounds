#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))

exec docker run --rm -it \
    --name 'wasmbounds-toolchain' --hostname 'wasmbounds-toolchain' \
    --mount type=bind,source="${THIS_DIR}",target=/opt/wasmbounds-src \
    --mount type=bind,source="${THIS_DIR}/runner-build-rv",target=/opt/wasmbounds-src/runner-build \
    --privileged --security-opt seccomp=unconfined \
    --workdir="/opt/wasmbounds-src" \
    'wasmbounds-toolchain-base-riscv:latest'
