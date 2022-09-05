#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
cd "${THIS_DIR}/../spec-cpu"
WB_DIR="${THIS_DIR}"/..
source shrc

REAL_OWNER=$(stat -c "%u:%g" .)

for BENCHMARK in 505.mcf_r 508.namd_r 519.lbm_r 525.x264_r 541.leela_r 544.nab_r 557.xz_r 531.deepsjeng_r
do
    echo "Creating rundirs for ${BENCHMARK}"
    # Clean up old build, exe and run directories to make failures obvious
    rm -rf "${WB_DIR}"/spec-cpu/benchspec/CPU/"$BENCHMARK"/run
    rm -rf "${WB_DIR}"/spec-cpu/benchspec/CPU/"$BENCHMARK"/exe
    rm -rf "${WB_DIR}"/spec-cpu/benchspec/CPU/"$BENCHMARK"/build

    TARGETSPEC=""
    if [[ "${BENCHMARK}" == "511.povray_r" ]]; then
        TARGETSPEC="TARGET=povray_r"
    elif [[ "${BENCHMARK}" == "525.x264_r" ]]; then
        TARGETSPEC="TARGET=x264_r"
    elif [[ "${BENCHMARK}" == "538.imagick_r" ]]; then
        TARGETSPEC="TARGET=imagick_r"
    fi

    for SIZE in test train refrate
    do
        rm -rf "${WB_DIR}"/rundirs/"$BENCHMARK"."$SIZE"

        # This dry run will set up the build and run directories and copy in the relevant files. We don't care about the actual output.
        runcpu --dry-run --action runsetup --config docker --size "$SIZE" --copies 1 --noreportable --iterations 1 "$BENCHMARK" > runcpu.out

        # Create run script in run directory
        cd "${WB_DIR}"/spec-cpu/benchspec/CPU/"$BENCHMARK"/run/run_base_"$SIZE"_docker-m64.0000

        echo '#!/bin/bash' > specinvoke.sh

        # GRrrrr
        if [ "$BENCHMARK" == "502.gcc_r" ]
        then
            BINARY=cpugcc_r
        else
            BINARY=$(echo "$BENCHMARK" | sed 's/.*\.//')
        fi

        specinvoke -n | grep "$BINARY" | sed "s/\.\.\/run_base_"$SIZE"_docker-m64.0000\/"$BINARY"_base.docker-m64/"$BINARY"/" >> specinvoke.sh
        chmod +x specinvoke.sh

        # Collect specdiff commands into a script for checking the ouput
        echo '#!/bin/bash' > specdiff.sh
        specinvoke -n compare.cmd | grep specdiff | \
            sed "s/\/root\/wasmbounds\/spec-cpu\/bin\/specperl \/root\/wasmbounds\/spec-cpu\/bin\/harness\/specdiff -m -l 10/diff/" | \
            sed "s/ > .*//" | \
            sed "s/--abstol .e-.. //" | \
            sed "s/--floatcompare //" | \
            sed "s/--nonansupport //" >> specdiff.sh
        chmod +x specdiff.sh

        # Move entire run directory to wasmbounds/rundirs/ for easy access
        cp -r "${WB_DIR}"/spec-cpu/benchspec/CPU/"$BENCHMARK"/run/run_base_"$SIZE"_docker-m64.0000 "${WB_DIR}"/rundirs/"$BENCHMARK"."$SIZE"
    done

    # Create compile script in build directory
    cd "${WB_DIR}"/spec-cpu/benchspec/CPU/"$BENCHMARK"/build/build_base_docker-m64.0000/

    echo "#!/bin/bash" > compile.sh
    echo "CC=\"\$1\"" >> compile.sh
    echo "CCX=\"\$2\"" >> compile.sh
    echo -e "FLAGS=\"\$3\"\n" >> compile.sh

    specmake --output-sync -n clean $TARGETSPEC >> compile.sh
    specmake --output-sync -n build $TARGETSPEC | \
        sed "s/\/usr\/bin\/gcc/\"\$CC\"/" | \
        sed "s/\/usr\/bin\/g++/\"\$CCX\"/" | \
        sed "s/-g -O3 -march=native -fno-unsafe-math-optimizations -fno-tree-loop-vectorize/\$FLAGS/" | \
        sed "s/ -m64//" | \
        "${THIS_DIR}"/spec_compilesh_parallelize.py >> compile.sh
    chmod +x compile.sh
done

# Prepare ffmpeg input
if [[ -f "${WB_DIR}/rundirs/BuckBunny.yuv" ]]; then
    echo Copying x264 input
else
    rm -f "${WB_DIR}/rundirs/BuckBunny.yuv"
    ffmpeg -i "${WB_DIR}/rundirs/525.x264_r.train/BuckBunny.264" "${WB_DIR}/rundirs/BuckBunny.yuv"
fi
cp --reflink=auto -a "${WB_DIR}/rundirs/BuckBunny.yuv" "${WB_DIR}/rundirs/525.x264_r.test/BuckBunny.yuv"
cp --reflink=auto -a "${WB_DIR}/rundirs/BuckBunny.yuv" "${WB_DIR}/rundirs/525.x264_r.train/BuckBunny.yuv"
cp --reflink=auto -a "${WB_DIR}/rundirs/BuckBunny.yuv" "${WB_DIR}/rundirs/525.x264_r.refrate/BuckBunny.yuv"

chown -R "${REAL_OWNER}" "${WB_DIR}"/rundirs
