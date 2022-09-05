#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))

exec docker run --rm -it \
    --privileged \
    --security-opt seccomp=unconfined \
    --network host \
    --name 'wasmbounds-toolchain' --hostname 'wasmbounds-toolchain' \
    --env HOST_HOSTNAME=$(hostname) \
    --mount type=bind,source="${THIS_DIR}",target=/root/wasmbounds \
    --workdir="/root/wasmbounds" \
    'wasmbounds-runners:latest' \
    '/root/wasmbounds/scripts/benchrunner.py' "$@"
