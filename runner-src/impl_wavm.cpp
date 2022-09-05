#include "runner.h"

#include "WAVM/IR/FeatureSpec.h"
#include "WAVM/IR/Module.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Value.h"
#include "WAVM/Platform/File.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Linker.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/VFS/SandboxFS.h"
#include "WAVM/WASI/WASI.h"
#include "WAVM/WASM/WASM.h"

namespace WASM = WAVM::WASM;
using WAVM::Runtime::Compartment;
using WAVM::Runtime::Context;
using WAVM::Runtime::Function;
using WAVM::Runtime::GCPointer;
using WAVM::Runtime::Instance;
using WAVM::Runtime::Memory;
using WAVM::Runtime::Module;

const std::string_view WB_RUNNER_TYPE = WB_RUNNER_TYPE_STR;

std::once_flag initOnce;

struct DataImpl {
  WAVM::Runtime::ModuleRef module;
  GCPointer<Compartment> compartment;
  GCPointer<Context> context;
  std::shared_ptr<WAVM::VFS::FileSystem> sandboxFs;
  std::shared_ptr<WAVM::WASI::Process> process;
  GCPointer<Instance> instance;
  Memory *memory;
  std::vector<uint8_t> snapshot;
  WAVM::IR::FunctionType startFnType;
  Function *startFn;
};

RunnerImpl::RunnerImpl(const RunnerOptions &opts)
    : data(std::make_unique<DataImpl>()) {
  std::call_once(initOnce, [&]() {
    WAVM::IR::BoundsCheckingMechanism wbmc =
        WAVM::IR::BoundsCheckingMechanism::mprotect;
    switch (opts.boundsChecks) {
    case BoundsChecks::none:
      wbmc = WAVM::IR::BoundsCheckingMechanism::none;
      break;
    case BoundsChecks::clamp:
      wbmc = WAVM::IR::BoundsCheckingMechanism::clamp;
      break;
    case BoundsChecks::trap:
      wbmc = WAVM::IR::BoundsCheckingMechanism::trap;
      break;
    case BoundsChecks::mprotect:
      [[fallthrough]];
    case BoundsChecks::mdiscard:
      [[fallthrough]];
    case BoundsChecks::uffd:
      wbmc = WAVM::IR::BoundsCheckingMechanism::mprotect;
      break;
    default:
      fmt::print(stderr,
                 "Invalid bounds checking mechanism for WAVM benchmark\n");
      std::terminate();
      break;
    }
    WAVM::IR::boundsCheckingMechanism = wbmc;
  });

  std::vector<uint8_t> wasmBytes = readFileToBytes(opts.benchmarkExecutable);
  WASM::LoadError loadError;
  if (!WAVM::Runtime::loadBinaryModule(
          wasmBytes.data(), wasmBytes.size(), data->module,
          WAVM::IR::FeatureLevel::mature, &loadError)) {
    fmt::print(stderr, "Couldn't load WASM module from {}: {}\n",
               opts.benchmarkExecutable, loadError.message);
    std::terminate();
  }
  data->compartment = WAVM::Runtime::createCompartment();
  data->context = WAVM::Runtime::createContext(data->compartment);

  if (opts.strace) {
    WAVM::WASI::setSyscallTraceLevel(WAVM::WASI::SyscallTraceLevel::syscallsWithCallstacks);
  }

  std::vector<std::string> wasiArgs = opts.wasmArgv;
  data->sandboxFs =
      WAVM::VFS::makeSandboxFS(&WAVM::Platform::getHostFS(),
                               WAVM::Platform::getCurrentWorkingDirectory());

  data->process = WAVM::WASI::createProcess(
      data->compartment, std::move(wasiArgs), {}, data->sandboxFs.get(),
      WAVM::Platform::getStdFD(WAVM::Platform::StdDevice::in),
      // was out, reserve out for benchmark data
      WAVM::Platform::getStdFD(WAVM::Platform::StdDevice::err),
      WAVM::Platform::getStdFD(WAVM::Platform::StdDevice::err));

  WAVM::Runtime::LinkResult linkResult =
      WAVM::Runtime::linkModule(WAVM::Runtime::getModuleIR(data->module),
                                WAVM::WASI::getProcessResolver(*data->process));
  if (!linkResult.success) {
    fmt::print(stderr, "Failed to link '{}':\n", opts.benchmarkExecutable);
    for (const auto &missingImport : linkResult.missingImports) {
      fmt::print(stderr,
                 "Failed to resolve import: type={} module={} export={}\n",
                 asString(missingImport.type), missingImport.moduleName,
                 missingImport.exportName);
    }
    std::terminate();
  }
  data->instance = WAVM::Runtime::instantiateModule(
      data->compartment, data->module, std::move(linkResult.resolvedImports),
      std::string(opts.benchmarkExecutable));
  if (Memory *memory = WAVM::Runtime::asMemoryNullable(
          WAVM::Runtime::getInstanceExport(data->instance, "memory"))) {
    WAVM::WASI::setProcessMemory(*data->process, memory);
    data->memory = memory;
  } else {
    fmt::print(stderr, "Failed to find memory export in '{}'.\n",
               opts.benchmarkExecutable);
    std::terminate();
  }

  data->startFnType = WAVM::IR::FunctionType();
  data->startFn = WAVM::Runtime::getTypedInstanceExport(
      data->instance, "_start", data->startFnType);

  const uint8_t *memoryBase = WAVM::Runtime::getMemoryBaseAddress(data->memory);
  const size_t memoryBytes = WAVM::Runtime::getMemoryNumPages(data->memory) *
                             WAVM::IR::numBytesPerPage;
  data->snapshot.assign(memoryBase, memoryBase + memoryBytes);
}

RunnerImpl::~RunnerImpl() {
  data->process.reset();
  data->memory = nullptr;
  data->instance = nullptr;
  data->context = nullptr;
  if (!WAVM::Runtime::tryCollectCompartment(std::move(data->compartment))) {
    fmt::print(stderr, "Warning: failed to fully GC WAVM compartment\n");
  }
}

void RunnerImpl::prepareRun(const RunnerOptions &opts) {
  const size_t memoryBytes = WAVM::Runtime::getMemoryNumPages(data->memory) *
                             WAVM::IR::numBytesPerPage;
  if (memoryBytes > data->snapshot.size()) {
    int64_t bytesToShrink = memoryBytes - data->snapshot.size();
    WAVM::Runtime::shrinkMemory(data->memory,
                                bytesToShrink / WAVM::IR::numBytesPerPage);
  } else if (memoryBytes < data->snapshot.size()) {
    int64_t bytesToGrow = data->snapshot.size() - memoryBytes;
    WAVM::Runtime::growMemory(data->memory,
                              bytesToGrow / WAVM::IR::numBytesPerPage);
  }
  uint8_t *memoryBase = WAVM::Runtime::getMemoryBaseAddress(data->memory);
  std::copy(data->snapshot.cbegin(), data->snapshot.cend(), memoryBase);
}

void RunnerImpl::runOnce(const RunnerOptions &opts) {
  const int32_t exitCode = WAVM::WASI::catchExit([&]() -> int32_t {
    WAVM::Runtime::invokeFunction(data->context, data->startFn,
                                  data->startFnType);
    return 0;
  });
  if (exitCode != 0) {
    fmt::print(stderr, "Warning: process exited with non-zero exit code {}\n",
               exitCode);
  }
}
