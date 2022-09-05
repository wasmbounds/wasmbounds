
group "default" {
    targets = ["wasmbounds-runtime-base"]
}

target "wasmbounds-runtime-base" {
    dockerfile = "Dockerfile.wasmbounds-runtime-base"
    platforms = ["linux/riscv64"]
    output = ["type=oci,dest=wasmbounds-runtime-base.riscv.tar"]
}

target "wasmbounds-toolchain-base" {
    inherits = ["wasmbounds-runtime-base"]
    dockerfile = "Dockerfile.wasmbounds-toolchain-base"
    output = ["type=oci,dest=wasmbounds-toolchain-base.riscv.tar"]
    contexts = {
        wasmbounds-runtime-base = "target:wasmbounds-runtime-base"
    }
}

target "wasmbounds-runners" {
    inherits = ["wasmbounds-runtime-base"]
    dockerfile = "Dockerfile.wasmbounds-runners"
    output = ["type=oci,dest=wasmbounds-runners.riscv.tar"]
    contexts = {
        wasmbounds-runtime-base = "target:wasmbounds-runtime-base"
        wasmbounds-toolchain-base = "target:wasmbounds-toolchain-base"
    }
}
