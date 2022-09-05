#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))

cd "${THIS_DIR}"

export DOCKER_BUILDKIT=1

#docker buildx build -f Dockerfile.wasmbounds-runtime-base -o type=oci,dest=wasmbounds-runtime-base.riscv.tar --platform=linux/riscv64 . || exit 1
docker buildx build -f Dockerfile.wasmbounds-toolchain-base -o type=oci,dest=wasmbounds-toolchain-base.riscv.tar --platform=linux/riscv64 --build-context wasmbounds-runtime-base=docker-image://./wasmbounds-runtime-base.riscv.tar . || exit 1
# docker build -f Dockerfile.wasmbounds-toolchain-base . -t wasmbounds-toolchain-base &&
# docker build -f Dockerfile.wasmbounds-runners . -t wasmbounds-runners
