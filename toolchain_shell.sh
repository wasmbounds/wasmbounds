#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))

exec docker run --rm -it \
    --name 'wasmbounds-toolchain' --hostname 'wasmbounds-toolchain' \
    --mount type=bind,source="${THIS_DIR}",target=/root/wasmbounds \
    --workdir="/root/wasmbounds" \
    'wasmbounds-toolchain-base:latest'
