#include "runner.h"

#include <source_location>

#include <m3_api_libc.h>
#include <m3_api_wasi.h>
#include <wasm3.h>
#define LINK_WASI

#include <m3_env.h>

// enum class WasmtimeBoundsTranslationMode : int32_t {
//   ProtectedMemory = 0,
//   None = 1,
//   Clamp = 2,
//   Trap = 3
// };

// extern "C" void set_wasmtime_bounds_translation_mode(int32_t mode);

const std::string_view WB_RUNNER_TYPE = WB_RUNNER_TYPE_STR;

std::once_flag initOnce;

struct DataImpl {
  inline static std::vector<uint8_t> wasmBytes{};
  inline static std::vector<const char *> wasmArgv{};
  M3Environment *env = nullptr;
  M3Runtime *runtime = nullptr;
  M3Module *module = nullptr;
  M3Function *startFn = nullptr;
  std::vector<uint8_t> snapshot{};

  ~DataImpl() {
    //
  }
};

void verify_not_null_f(const void *ptr, std::string_view objstr,
                       std::string_view file, size_t line) {
  if (ptr == nullptr) {
    fmt::print(stderr, "{}:{} Null object {}\n", file, line, objstr);
    throw std::runtime_error("Null object encountered");
  }
}
#define verify_not_null(ptr)                                                   \
  ::verify_not_null_f((const void *)ptr, #ptr, __FILE__, __LINE__)

void verify_error(M3Result err) {
  if (err != m3Err_none) {

    fmt::print("Wasm3 error encountered: {}\n", std::string_view(err));
    throw std::runtime_error("Wasm3 error");
  }
}

RunnerImpl::RunnerImpl(const RunnerOptions &opts)
    : data(std::make_unique<DataImpl>()) {
  std::call_once(initOnce, [&]() {
    switch (opts.boundsChecks) {
    case BoundsChecks::none:
      // set_wasmtime_bounds_translation_mode(
      //     (int32_t)WasmtimeBoundsTranslationMode::None);
    case BoundsChecks::clamp:
      // set_wasmtime_bounds_translation_mode(
      //     (int32_t)WasmtimeBoundsTranslationMode::Clamp);
      break;
    case BoundsChecks::trap:
      // set_wasmtime_bounds_translation_mode(
      //     (int32_t)WasmtimeBoundsTranslationMode::Trap);
      break;
    case BoundsChecks::mprotect:
      [[fallthrough]];
    case BoundsChecks::mdiscard:
      [[fallthrough]];
    case BoundsChecks::uffd:
      // set_wasmtime_bounds_translation_mode(
      //     (int32_t)WasmtimeBoundsTranslationMode::ProtectedMemory);
      break;
    default:
      fmt::print(stderr,
                 "Invalid bounds checking mechanism for WAVM benchmark\n");
      std::terminate();
      break;
    }
    // todo

    DataImpl::wasmBytes = readFileToBytes(opts.benchmarkExecutable);

    // init all the structures
    data->env = m3_NewEnvironment();
    verify_not_null(data->env);

    data->runtime = m3_NewRuntime(data->env, 4 * 1024 * 1024, (void *)this);
    verify_not_null(data->runtime);

    verify_error(m3_ParseModule(data->env, &data->module,
                                DataImpl::wasmBytes.data(),
                                DataImpl::wasmBytes.size()));
    verify_not_null(data->module);
    verify_error(m3_LoadModule(data->runtime, data->module));
    m3_SetModuleName(data->module, opts.benchmarkExecutable.c_str());

    verify_error(m3_LinkLibC(data->module));
    verify_error(m3_LinkWASI(data->module));
    verify_error(m3_CompileModule(data->module));

    m3_FreeRuntime(data->runtime);
    m3_FreeEnvironment(data->env);
    data->module = nullptr;
    data->runtime = nullptr;
    data->env = nullptr;

    m3_wasi_context_t *wasi = m3_GetWasiContext();
    DataImpl::wasmArgv.clear();
    DataImpl::wasmArgv.reserve(opts.wasmArgv.size());
    for (const auto &s : opts.wasmArgv) {
      DataImpl::wasmArgv.push_back(s.c_str());
    }
    wasi->argc = opts.wasmArgv.size();
    wasi->argv = DataImpl::wasmArgv.data();
  });

  data->env = m3_NewEnvironment();
  verify_not_null(data->env);

  data->runtime = m3_NewRuntime(data->env, 4 * 1024 * 1024, (void *)this);
  verify_not_null(data->runtime);

  verify_error(m3_ParseModule(data->env, &data->module,
                              DataImpl::wasmBytes.data(),
                              DataImpl::wasmBytes.size()));
  verify_not_null(data->module);
  verify_error(m3_LoadModule(data->runtime, data->module));
  m3_SetModuleName(data->module, opts.benchmarkExecutable.c_str());

  verify_error(m3_LinkLibC(data->module));
  verify_error(m3_LinkWASI(data->module));

  verify_error(m3_CompileModule(data->module));

  if (data->module->mainFunction == -1) {
    verify_error(m3_FindFunction(&data->startFn, data->runtime, "_start"));
  } else {
    data->startFn = &data->module->functions[data->module->mainFunction];
  }
  verify_not_null(data->startFn);

  uint32_t memoryBytes = 0;
  const uint8_t *memoryBase = m3_GetMemory(data->runtime, &memoryBytes, 0);
  data->snapshot.assign(memoryBase, memoryBase + memoryBytes);
}

RunnerImpl::~RunnerImpl() {}

constexpr size_t WASM_BYTES_PER_PAGE = 65536;

void RunnerImpl::prepareRun(const RunnerOptions &opts) {
  const size_t memoryBytes = m3_GetMemorySize(data->runtime);
  if (memoryBytes != data->snapshot.size()) {
    ResizeMemory(data->runtime, memoryBytes / d_m3MemPageSize);
  }
  uint8_t *memoryBase = m3_GetMemory(data->runtime, nullptr, 0);
  std::copy(data->snapshot.cbegin(), data->snapshot.cend(), memoryBase);
}

void RunnerImpl::runOnce(const RunnerOptions &opts) {
  M3Result result = m3_CallArgv(data->startFn, 0, NULL);
  int32_t ec = m3_GetWasiContext()->exit_code;
  if (result == m3Err_trapExit) {
    if (ec != 0) {
      fmt::print(stderr, "Warning: process exited with non-zero exit code {}\n",
                 ec);
    }
  } else {
    verify_error(result);
  }
}
