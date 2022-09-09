
group "default" {
    targets = ["wasmbounds-runtime-base", "wasmbounds-toolchain-base", "wasmbounds-runners"]
}

target "wasmbounds-runtime-base" {
    dockerfile = "Dockerfile.wasmbounds-runtime-base"
    tags = ["wasmbounds-runtime-base:latest"]
    platforms = ["linux/amd64"]
    output = ["type=oci,dest=wasmbounds-runtime-base.x86-64.tar"]
}

target "wasmbounds-toolchain-base" {
    inherits = ["wasmbounds-runtime-base"]
    tags = ["wasmbounds-toolchain-base:latest"]
    dockerfile = "Dockerfile.wasmbounds-toolchain-base"
    output = ["type=oci,dest=wasmbounds-toolchain-base.x86-64.tar"]
    contexts = {
        wasmbounds-runtime-base = "target:wasmbounds-runtime-base"
    }
}

target "wasmbounds-runners" {
    inherits = ["wasmbounds-runtime-base"]
    tags = ["wasmbounds-runners:latest"]
    dockerfile = "Dockerfile.wasmbounds-runners"
    output = ["type=oci,dest=wasmbounds-runners.x86-64.tar"]
    contexts = {
        wasmbounds-runtime-base = "target:wasmbounds-runtime-base"
        wasmbounds-toolchain-base = "target:wasmbounds-toolchain-base"
    }
}
