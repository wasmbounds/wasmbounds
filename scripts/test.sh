#!/bin/bash

cd spec-cpu
source shrc

# rm /root/wasmbounds/spec-cpu/benchspec/CPU/525.x264_r/src
runcpu --action runsetup --config test --size test --copies 1 --noreportable --iterations 1 554.roms_r

# rm /opt/wasi-sdk/share/wasi-sysroot/include/setjmp.h
# rm /opt/wasi-sdk/share/wasi-sysroot/include/wait.h


# cat /root/wasmbounds/spec-cpu/result/CPU2017.335.log | grep error

cat /root/wasmbounds/spec-cpu/benchspec/CPU/554.roms_r/build/build_base_docker-m64.0000/make.out # grep error
#| grep error

# cat /root/wasmbounds/spec-cpu/benchspec/CPU/525.x264_r/build/build_base_docker-m64.0000/make.x264_r.out | grep error


# Errors for fprate: 507.cactuBSSN_r no errors????, 510.parest_r, 511.povray_r(base; CE), 526.blender_r, 538.imagick_r, 544.nab_r(base; CE)
# Errors for intrate: 500.perlbench_r, 502.gcc_r, 520.omnetpp_r, 523.xalancbmk_r, 525.x264_r weird input generation, could possibly do, 541.leela_r 

# Fortran 
# intrate: 548.exchange2_r(base; CE)
# fprate: 549.fotonik3d_r(base; CE), 554.roms_r(base; CE), 527.cam4_r(base; CE),521.wrf_r(base; CE), 503.bwaves_r(base; CE)