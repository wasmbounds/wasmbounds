#include "runner.h"

#include <source_location>
#include <wasi.h>
#include <wasmtime.h>

enum class WasmtimeBoundsTranslationMode : int32_t {
  ProtectedMemory = 0,
  None = 1,
  Clamp = 2,
  Trap = 3
};

extern "C" void set_wasmtime_bounds_translation_mode(int32_t mode);

const std::string_view WB_RUNNER_TYPE = WB_RUNNER_TYPE_STR;

std::once_flag initOnce;

struct DataImpl {
  inline static wasm_engine_t *engine = nullptr;
  inline static wasmtime_module_t *module = nullptr;
  wasmtime_store_t *store = nullptr;
  wasmtime_linker_t *linker = nullptr;
  wasmtime_instance_t instance{};
  wasmtime_func_t startFn{};
  wasmtime_memory_t memory;
  std::vector<uint8_t> snapshot;

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

void verify_error(wasmtime_error_t *err) {
  if (err != nullptr) {
    wasm_name_t name;
    wasmtime_error_message(err, &name);
    fmt::print("Wasmtime error encountered: {}\n",
               std::string_view(name.data, name.size));
    wasm_byte_vec_delete(&name);
    wasmtime_error_delete(err);
    throw std::runtime_error("Wasmtime error");
  }
}

RunnerImpl::RunnerImpl(const RunnerOptions &opts)
    : data(std::make_unique<DataImpl>()) {
  std::call_once(initOnce, [&]() {
    switch (opts.boundsChecks) {
    case BoundsChecks::none:
      set_wasmtime_bounds_translation_mode(
          (int32_t)WasmtimeBoundsTranslationMode::None);
    case BoundsChecks::clamp:
      set_wasmtime_bounds_translation_mode(
          (int32_t)WasmtimeBoundsTranslationMode::Clamp);
      break;
    case BoundsChecks::trap:
      set_wasmtime_bounds_translation_mode(
          (int32_t)WasmtimeBoundsTranslationMode::Trap);
      break;
    case BoundsChecks::mprotect:
      [[fallthrough]];
    case BoundsChecks::mdiscard:
      [[fallthrough]];
    case BoundsChecks::uffd:
      set_wasmtime_bounds_translation_mode(
          (int32_t)WasmtimeBoundsTranslationMode::ProtectedMemory);
      break;
    default:
      fmt::print(stderr,
                 "Invalid bounds checking mechanism for WAVM benchmark\n");
      std::terminate();
      break;
    }
    // todo

    wasm_config_t *config = wasm_config_new();
    verify_not_null(config);
    DataImpl::engine = wasm_engine_new_with_config(config);
    verify_not_null(DataImpl::engine);

    std::vector<uint8_t> wasmBytes = readFileToBytes(opts.benchmarkExecutable);
    verify_error(wasmtime_module_new(DataImpl::engine, wasmBytes.data(),
                                     wasmBytes.size(), &DataImpl::module));
    verify_not_null(DataImpl::module);
  });

  data->store = wasmtime_store_new(data->engine, (void *)this, nullptr);
  verify_not_null(data->store);

  wasmtime_context_t *context = wasmtime_store_context(data->store);

  data->linker = wasmtime_linker_new(data->engine);
  verify_error(wasmtime_linker_define_wasi(data->linker));

  wasi_config_t *wasiConfig = wasi_config_new();
  // const char *wasiArgv[2] = {opts.benchmarkExecutable.c_str(), nullptr};
  std::vector<const char *> wasiArgv;
  wasiArgv.reserve(opts.wasmArgv.size());
  for (auto &arg : opts.wasmArgv) {
    wasiArgv.push_back(arg.c_str());
  }
  wasiArgv.push_back(nullptr);
  wasi_config_set_argv(wasiConfig, wasiArgv.size() - 1, wasiArgv.data());
  wasi_config_set_env(wasiConfig, 0, {}, {});
  wasi_config_inherit_env(wasiConfig);
  wasi_config_inherit_stdin(wasiConfig);
  wasi_config_set_stdout_file(wasiConfig, "/dev/stderr");
  wasi_config_inherit_stderr(wasiConfig);
  wasi_config_preopen_dir(wasiConfig, ".", "/");
  verify_error(wasmtime_context_set_wasi(context, wasiConfig));

  verify_error(
      wasmtime_linker_module(data->linker, context, "", 0, data->module));

  wasm_trap_t *trap = nullptr;
  verify_error(wasmtime_linker_instantiate(data->linker, context, data->module,
                                           &data->instance, &trap));
  if (trap) {
    throw std::runtime_error("Trap caught instantiating module");
  }

  wasmtime_extern_t item;
  std::string_view startFnName = "_start";
  if (!wasmtime_instance_export_get(context, &data->instance,
                                    startFnName.data(), startFnName.size(),
                                    &item)) {
    throw std::runtime_error("Couldn't get _start fn export");
  }
  if (item.kind != WASMTIME_EXTERN_FUNC) {
    throw std::runtime_error("_start is not a function");
  }
  data->startFn = item.of.func;

  std::string_view memName = "memory";
  if (!wasmtime_instance_export_get(context, &data->instance, memName.data(),
                                    memName.size(), &item)) {
    throw std::runtime_error("Couldn't get memory export");
  }
  if (item.kind != WASMTIME_EXTERN_MEMORY) {
    throw std::runtime_error("'memory' export is not a memory");
  }
  data->memory = item.of.memory;

  const uint8_t *memoryBase = wasmtime_memory_data(context, &data->memory);
  const size_t memoryBytes = wasmtime_memory_data_size(context, &data->memory);
  data->snapshot.assign(memoryBase, memoryBase + memoryBytes);
}

RunnerImpl::~RunnerImpl() {}

constexpr size_t WASM_BYTES_PER_PAGE = 65536;

void RunnerImpl::prepareRun(const RunnerOptions &opts) {
  wasmtime_context_t *context = wasmtime_store_context(data->store);
  const size_t memoryBytes = wasmtime_memory_data_size(context, &data->memory);
  if (memoryBytes > data->snapshot.size()) {
    int64_t bytesToShrink = memoryBytes - data->snapshot.size();
    // WAVM::Runtime::shrinkMemory(data->memory,
    //                             bytesToShrink / WAVM::IR::numBytesPerPage);
    uint64_t oldSize{};
    verify_error(wasmtime_memory_shrink(
        context, &data->memory, bytesToShrink / WASM_BYTES_PER_PAGE, &oldSize));
  } else if (memoryBytes < data->snapshot.size()) {
    int64_t bytesToGrow = data->snapshot.size() - memoryBytes;
    uint64_t oldSize{};
    verify_error(wasmtime_memory_grow(
        context, &data->memory, bytesToGrow / WASM_BYTES_PER_PAGE, &oldSize));
  }
  uint8_t *memoryBase = wasmtime_memory_data(context, &data->memory);
  std::copy(data->snapshot.cbegin(), data->snapshot.cend(), memoryBase);
}

void RunnerImpl::runOnce(const RunnerOptions &opts) {
  wasmtime_context_t *context = wasmtime_store_context(data->store);
  wasm_trap_t *trap = nullptr;
  verify_error(wasmtime_func_call(context, &data->startFn, nullptr, 0, nullptr,
                                  0, &trap));
  if (trap != nullptr) {
    int status = 0;
    wasmtime_trap_code_t code = 0;
    if (wasmtime_trap_exit_status(trap, &status)) {
      if (status != 0) {
        fmt::print(stderr,
                   "Warning: process exited with non-zero exit code {}\n",
                   status);
      }
    } else if (wasmtime_trap_code(trap, &code)) {
      fmt::print("Wasmtime trap code caught: {}\n", code);
      throw std::runtime_error("Wasmtime trap code");
    } else {
      fmt::print(
          "Wasmtime trap message caught, attempting retrieval from ptr: {}\n",
          (void *)trap);
      wasm_message_t tmsg;
      wasm_trap_message(trap, &tmsg);
      fmt::print(stderr, "Wasmtime main() trap caught: {}\n",
                 std::string_view(tmsg.data, tmsg.size));
      wasm_byte_vec_delete(&tmsg);
      throw std::runtime_error(
          "Wasmtime trap caught executing main function\n");
    }
  }
}
