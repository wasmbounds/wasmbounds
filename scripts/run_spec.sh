#!/bin/bash
BENCHMARK="$1"
SIZE="$2"
EXTENSION="$3"
# RUNTIME="$4"

if [ "$EXTENSION" == "wasi" ]
then
  COMMAND="\/root\/wasmbounds\/WAVM\/build\/bin\/wavm run --mount-root \. "
  # Add more runtimes here
elif [ "$EXTENSION" == "clang" ] || [ "$EXTENSION" == "gcc" ]
then
  COMMAND="\.\/"
fi

if [ "$BENCHMARK" == "502.gcc_r" ]
then
    BINARY=cpugcc_r
else
    BINARY=$(echo "$BENCHMARK" | sed 's/.*\.//')
fi

cd ~/wasmbounds/rundirs/"$BENCHMARK"."$SIZE"
cat specinvoke.sh | sed "s/$BINARY/$COMMAND$BINARY\.$EXTENSION/" > run.sh

chmod +x run.sh
./run.sh
./specdiff.sh # No output if the result is correct
echo "Finished running "$BENCHMARK""
