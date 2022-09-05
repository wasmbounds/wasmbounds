#!/bin/bash
COMPILER="$1"

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
SPEC_DIR="${THIS_DIR}/../spec-cpu"

if [ "$COMPILER" == "wasm" ]
then
  CC="/opt/wasi-sdk/bin/clang"
  CCX="/opt/wasi-sdk/bin/clang++"
  FLAGS="-g -O3 --target=wasm32-wasi --sysroot=/opt/wasi-sdk/share/wasi-sysroot -m32 -march=native -fno-unsafe-math-optimizations -fno-tree-vectorize -w"
elif [ "$COMPILER" == "clang" ]
then
  CC="/usr/bin/clang"
  CCX="/usr/bin/clang++"
  FLAGS="-g -O3 -fno-unsafe-math-optimizations -fno-tree-vectorize -w"
  if lscpu | grep -q 'ThunderX2'; then
    FLAGS="$FLAGS -march=armv8 -mcpu=thunderx2t99"
  else
    FLAGS="$FLAGS -march=native"
  fi
elif [ "$COMPILER" == "gcc" ]
then
  CC="/usr/bin/gcc"
  CCX="/usr/bin/g++"
  FLAGS="-g -O3 -march=native -fno-unsafe-math-optimizations -fno-tree-loop-vectorize -w"
else
  echo "Specify compiler"
  exit 1
fi

for BENCHMARK in 505.mcf_r 508.namd_r 519.lbm_r 525.x264_r 541.leela_r 544.nab_r 557.xz_r 531.deepsjeng_r
do
    cd "$SPEC_DIR"/benchspec/CPU/"$BENCHMARK"/build/build_base_docker-m64.0000/

    if [ "$BENCHMARK" == "502.gcc_r" ]
    then
        BINARY=cpugcc_r
    else
        BINARY=$(echo "$BENCHMARK" | sed 's/.*\.//')
    fi

    echo "Compiling ${BENCHMARK} to" "${THIS_DIR}"/../rundirs/"$BENCHMARK".SIZE/"$BINARY"."$COMPILER"
    ./compile.sh "$CC" "$CCX" "$FLAGS" || continue

    for SIZE in test train
    do
      cp "$BINARY" "${THIS_DIR}"/../rundirs/"$BENCHMARK"."$SIZE"/"$BINARY"."$COMPILER"
    done
done
