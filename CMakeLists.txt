cmake_minimum_required(VERSION 3.22)
project(wasmbounds-runner C CXX)

# Enable colorized compiler output
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics)
endif ()

# Warnings, wrapping signed overflow behavior, meaningful stacktraces
add_compile_options(-fwrapv -Wall -fno-omit-frame-pointer)

# CPU tuning
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    add_compile_options(-march=skylake -mtune=skylake)
    add_definitions(-DBUILD_TARGET_X86_64)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    add_definitions(-DBUILD_TARGET_AARCH64)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    add_definitions(-DBUILD_TARGET_RISCV64)
    set(EXCLUDE_WASMTIME TRUE)
else ()
    message(FATAL_ERROR "Unsupported CPU architecture '${CMAKE_SYSTEM_PROCESSOR}'")
endif ()

if (CFG_LLVM_DIR)
    set (LLVM_DIR "${CFG_LLVM_DIR}" CACHE PATH "" FORCE)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXE_LINKER_FLAGS "-pthread")
set(CMAKE_SKIP_RPATH OFF)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Global include directory for the hooks for all the different WASM VMs
include_directories(runner-src/vm-library)

# Wasmtime
if (NOT EXCLUDE_WASMTIME)
    set(WASMTIME_LIBFILE "${CMAKE_CURRENT_SOURCE_DIR}/wasmtime/target/release/libwasmtime.a")
    add_custom_command(
        OUTPUT "${WASMTIME_LIBFILE}"
        COMMAND cargo ARGS build --release --package wasmtime-c-api
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/wasmtime"
        DEPFILE "${CMAKE_CURRENT_SOURCE_DIR}/wasmtime/target/release/libwasmtime.d"
        VERBATIM
    )
    add_custom_target(wasmtime_target DEPENDS "${WASMTIME_LIBFILE}")
    add_library(wasmtime STATIC IMPORTED GLOBAL)
    add_dependencies(wasmtime wasmtime_target)
    set_target_properties(wasmtime
        PROPERTIES
        IMPORTED_LOCATION "${WASMTIME_LIBFILE}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/wasmtime/crates/c-api/include;${CMAKE_CURRENT_SOURCE_DIR}/wasmtime/crates/c-api/wasm-c-api/include"
    )
    add_library(wasmtime::wasmtime ALIAS wasmtime)
endif()

# WAVM
set(WAVM_ENABLE_STATIC_LINKING ON CACHE BOOL "" FORCE)
add_subdirectory(WAVM EXCLUDE_FROM_ALL)

# wasm3
unset(ENV{WASI_CC})
add_subdirectory(wasm3-0.5.0 EXCLUDE_FROM_ALL)
add_library(wasm3_lib INTERFACE)
target_link_libraries(wasm3_lib INTERFACE uvwasi_a uv_a m3)
add_library(wasm3::wasm3 ALIAS wasm3_lib)

# Node.js/V8
set(NODEJS_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/node-v18.2.0")
set(NODEJS_ROOT "${CMAKE_CURRENT_BINARY_DIR}/nodejs")

file(
    COPY "${NODEJS_SRC_ROOT}/"
    DESTINATION "${NODEJS_ROOT}/"
    USE_SOURCE_PERMISSIONS
    FOLLOW_SYMLINK_CHAIN
)

set(NODEJS_INSTALL_ROOT "${CMAKE_CURRENT_BINARY_DIR}/nodejs")
set(NODEJS_BUILD_ROOT "${NODEJS_ROOT}/out/Release")
file(MAKE_DIRECTORY "${NODEJS_BUILD_ROOT}")
file(MAKE_DIRECTORY "${NODEJS_BUILD_ROOT}/gen")
set(NODEJS_LIBFILE "${NODEJS_ROOT}/out/Release/lib/libnode.so.108")
set(NODEJS_SRCS "")
file(GLOB_RECURSE NODEJS_SRCS FOLLOW_SYMLINKS
    LIST_DIRECTORIES false
    "${NODEJS_ROOT}/*.h"
    "${NODEJS_ROOT}/*.c"
    "${NODEJS_ROOT}/*.cc"
    "${NODEJS_ROOT}/*.cpp"
)
add_custom_command(
    OUTPUT "${NODEJS_LIBFILE}"
    COMMAND ${CMAKE_COMMAND} -E env
        "CC=${CMAKE_C_COMPILER}"
        "CXX=${CMAKE_CXX_COMPILER}"
        "${NODEJS_ROOT}/configure" ARGS
        --ninja
        --prefix "${NODEJS_INSTALL_ROOT}"
        --without-dtrace --without-etw --without-npm --without-corepack --without-ssl
        --shared
        --with-intl none
    COMMAND ${CMAKE_COMMAND} -E env
        "CC=${CMAKE_C_COMPILER}"
        "CXX=${CMAKE_CXX_COMPILER}"
        "ninja" ARGS -C "${NODEJS_BUILD_ROOT}" libnode
    MAIN_DEPENDENCY "${NODEJS_ROOT}/configure.py"
    DEPENDS ${NODEJS_SRCS}
    WORKING_DIRECTORY "${NODEJS_ROOT}"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(nodejs_target DEPENDS "${NODEJS_LIBFILE}")
add_library(nodejs SHARED IMPORTED GLOBAL)
add_dependencies(nodejs nodejs_target)
set_target_properties(nodejs
    PROPERTIES
    IMPORTED_LOCATION "${NODEJS_LIBFILE}"
)
install(IMPORTED_RUNTIME_ARTIFACTS "nodejs"
    RUNTIME DESTINATION lib
)
target_compile_definitions(nodejs INTERFACE
    -DV8_DEPRECATION_WARNINGS
    -DV8_IMMINENT_DEPRECATION_WARNINGS
    -D_GLIBCXX_USE_CXX11_ABI=1
    -D__STDC_FORMAT_MACROS
    -DOPENSSL_THREADS
    -DNODE_PLATFORM="linux"
    -DNODE_WANT_INTERNALS=1
    -DV8_DEPRECATION_WARNINGS=1
    -DNODE_OPENSSL_SYSTEM_CERT_PATH=""
    -DHAVE_INSPECTOR=0
    -DNODE_SHARED_MODE
    -D__POSIX__
    -DNODE_USE_V8_PLATFORM=1
    -DHAVE_OPENSSL=0
    -D_LARGEFILE_SOURCE
    -D_FILE_OFFSET_BITS=64
    -DNGHTTP2_STATICLIB
)
target_include_directories(nodejs INTERFACE
    "${NODEJS_ROOT}/src"
    "${NODEJS_BUILD_ROOT}/gen"
    "${NODEJS_ROOT}/deps/googletest/include"
    "${NODEJS_ROOT}/deps/histogram/src"
    "${NODEJS_ROOT}/deps/uvwasi/include"
    "${NODEJS_ROOT}/deps/v8/include"
    "${NODEJS_ROOT}/deps/zlib"
    "${NODEJS_ROOT}/deps/llhttp/include"
    "${NODEJS_ROOT}/deps/cares/include"
    "${NODEJS_ROOT}/deps/uv/include"
    "${NODEJS_ROOT}/deps/nghttp2/lib/includes"
    "${NODEJS_ROOT}/deps/brotli/c/include"
)
add_library(nodejs::nodejs ALIAS nodejs)

set(CMAKE_CXX_STANDARD 20)
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_NOT_SUPPORTED_ERROR LANGUAGES C CXX)
if(IPO_SUPPORTED)
    message(STATUS "IPO (LTO) supported, enabling")
    if (NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif ()
else()
    message(WARNING "IPO (LTO) not supported: ${IPO_NOT_SUPPORTED_ERROR}")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS container)
find_package(absl REQUIRED COMPONENTS flat_hash_map)

add_subdirectory(cpp-libs/fmt EXCLUDE_FROM_ALL)

# Group dependencies together to avoid duplication
add_library(runner_deps INTERFACE)
target_link_libraries(runner_deps INTERFACE
    fmt::fmt
    Threads::Threads
    Boost::container
    absl::flat_hash_map
)


add_library(runner_main_obj OBJECT "runner-src/runner.cpp" "runner-src/vm-library/wasmbounds_rr.cpp")
target_compile_features(runner_main_obj PUBLIC cxx_std_20)
target_link_libraries(runner_main_obj PUBLIC runner_deps)

function(make_runner_target TARGET_NAME RUNNER_TYPE)
    add_executable("${TARGET_NAME}"  "runner-src/impl_${RUNNER_TYPE}.cpp" $<TARGET_OBJECTS:runner_main_obj>)
    target_compile_features("${TARGET_NAME}" PUBLIC cxx_std_20)
    target_compile_definitions("${TARGET_NAME}" PUBLIC "WB_RUNNER_TYPE_STR=\"${RUNNER_TYPE}\"")
    target_link_libraries("${TARGET_NAME}" PUBLIC runner_deps)
    install(TARGETS "${TARGET_NAME}"
        RUNTIME DESTINATION bin
    )
endfunction()

make_runner_target(wbrunner_native native)

make_runner_target(wbrunner_wavm wavm)
target_link_libraries(wbrunner_wavm PRIVATE libWAVM)

if(NOT EXCLUDE_WASMTIME)
    make_runner_target(wbrunner_wasmtime wasmtime)
    target_link_libraries(wbrunner_wasmtime PRIVATE wasmtime::wasmtime)
endif()

make_runner_target(wbrunner_wasm3 wasm3)
target_link_libraries(wbrunner_wasm3 PRIVATE wasm3::wasm3)

make_runner_target(wbrunner_nodejs nodejs)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/impl_nodejs.js.h"
    COMMAND "xxd" "-i" "impl_nodejs.js" "${CMAKE_CURRENT_BINARY_DIR}/impl_nodejs.js.h"
    MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/runner-src/impl_nodejs.js"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/runner-src/"
    VERBATIM
)
add_custom_target(impl_nodejs_h DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/impl_nodejs.js.h")
add_dependencies(wbrunner_nodejs impl_nodejs_h)
target_include_directories(wbrunner_nodejs PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(wbrunner_nodejs PRIVATE nodejs::nodejs)
set_target_properties(nodejs
    PROPERTIES
    SKIP_BUILD_RPATH OFF
    BUILD_RPATH_USE_ORIGIN ON
    BUILD_RPATH "${NODEJS_LIBFILE}"
)
