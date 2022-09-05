#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))

cd "${THIS_DIR}"

export DOCKER_BUILDKIT=1

docker build -f Dockerfile.wasmbounds-runtime-base . -t wasmbounds-runtime-base &&
docker build -f Dockerfile.wasmbounds-toolchain-base . -t wasmbounds-toolchain-base &&
docker build -f Dockerfile.wasmbounds-runners . -t wasmbounds-runners
