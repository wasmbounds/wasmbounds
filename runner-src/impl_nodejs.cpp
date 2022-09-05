#define NODE_WANT_INTERNALS 1
#include "runner.h"

#include <env-inl.h>
#include <node.h>
#include <node_internals.h>
#include <node_native_module_env.h>
#include <node_platform.h>
#include <node_union_bytes.h>
#include <node_v8.h>
#include <uv.h>

#include "vm-library/wasmbounds_rr.hpp"

#include <v8-internal.h>

#include "impl_nodejs.js.h"

using node::CommonEnvironmentSetup;
using node::Environment;
using node::MultiIsolatePlatform;
using node::UnionBytes;
using v8::Context;
using v8::Function;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Locker;
using v8::MaybeLocal;
using v8::String;
using v8::V8;
using v8::Value;

namespace native_module = node::native_module;

const std::string_view WB_RUNNER_TYPE = WB_RUNNER_TYPE_STR;
const std::string_view JS_CODE((const char *)impl_nodejs_js,
                               (size_t)impl_nodejs_js_len);

std::once_flag initOnce;

struct DataImpl {
  inline static std::vector<uint8_t> wasmBytes{};
  inline static std::vector<std::string> nodeArgv;
  inline static std::vector<std::string> execArgv;
  inline static std::unique_ptr<MultiIsolatePlatform> platform{nullptr};
  v8::Eternal<Function> preExecFunction;
  v8::Eternal<Function> execFunction;
  uv_loop_t uvLoop;
  std::unique_ptr<CommonEnvironmentSetup> setup;
};

RunnerImpl::RunnerImpl(const RunnerOptions &opts)
    : data(std::make_unique<DataImpl>()) {
  std::call_once(initOnce, [&]() {
    // WAVM::IR::BoundsCheckingMechanism wbmc =
    //     WAVM::IR::BoundsCheckingMechanism::mprotect;
    switch (opts.boundsChecks) {
    case BoundsChecks::none:
      // wbmc = WAVM::IR::BoundsCheckingMechanism::none;
      v8::internal::v8BoundsCheckingCode = v8::internal::BoundsCheckingCode::none;
      break;
    case BoundsChecks::clamp:
      // wbmc = WAVM::IR::BoundsCheckingMechanism::clamp;
      v8::internal::v8BoundsCheckingCode = v8::internal::BoundsCheckingCode::alwaysClamp;
      break;
    case BoundsChecks::trap:
      // wbmc = WAVM::IR::BoundsCheckingMechanism::trap;
      v8::internal::v8BoundsCheckingCode = v8::internal::BoundsCheckingCode::alwaysTrap;
      break;
    case BoundsChecks::mprotect:
      [[fallthrough]];
    case BoundsChecks::mdiscard:
      [[fallthrough]];
    case BoundsChecks::uffd:
      // wbmc = WAVM::IR::BoundsCheckingMechanism::mprotect;
      v8::internal::v8BoundsCheckingCode = v8::internal::BoundsCheckingCode::trapIfNeeded;
      break;
    default:
      fmt::print(stderr,
                 "Invalid bounds checking mechanism for WAVM benchmark\n");
      std::terminate();
      break;
    }
    resizableRegionAllocator = wrapInReusingRra(std::move(resizableRegionAllocator));
    v8::internal::v8RRA = resizableRegionAllocator.get();
    // WAVM::IR::boundsCheckingMechanism = wbmc;
    DataImpl::wasmBytes = readFileToBytes(opts.benchmarkExecutable);
    std::string argv0 =
        opts.wasmArgv.empty() ? "wbrunner_nodejs" : opts.wasmArgv.front();
    char *argvp[] = {argv0.data(), nullptr};
    uv_setup_args(1, argvp);
    DataImpl::nodeArgv = opts.wasmArgv;
    DataImpl::nodeArgv.insert(DataImpl::nodeArgv.cbegin(), "--");
    DataImpl::nodeArgv.insert(DataImpl::nodeArgv.cbegin(), "--expose-gc");
    DataImpl::nodeArgv.insert(DataImpl::nodeArgv.cbegin(), "--no-liftoff");
    DataImpl::nodeArgv.insert(DataImpl::nodeArgv.cbegin(), "--no-wasm-tier-up");
    DataImpl::nodeArgv.insert(DataImpl::nodeArgv.cbegin(), "--no-warnings");
    DataImpl::nodeArgv.insert(DataImpl::nodeArgv.cbegin(),
                              "--experimental-wasi-unstable-preview1");
    std::vector<std::string> errors;
    int exit_code = node::InitializeNodeWithArgs(&DataImpl::nodeArgv,
                                                 &DataImpl::execArgv, &errors);
    for (const auto &err : errors) {
      fmt::print(stderr, "Node.js initialization error: {}", err);
    }
    if (exit_code != 0) {
      std::exit(exit_code);
    }

    DataImpl::platform = MultiIsolatePlatform::Create(opts.numThreads);
    V8::InitializePlatform(DataImpl::platform.get());
    V8::Initialize();
  });

  uv_loop_init(&data->uvLoop);

  std::vector<std::string> errors;
  data->setup =
      CommonEnvironmentSetup::Create(DataImpl::platform.get(), &errors,
                                     DataImpl::nodeArgv, DataImpl::execArgv);
  Isolate *isolate = data->setup->isolate();
  Environment *env = data->setup->env();
  {
    Locker locker{isolate};
    Isolate::Scope scope{isolate};
    HandleScope hScope{isolate};
    auto ctx{data->setup->context()};
    Context::Scope ctxScope{ctx};
    Local<Value> loadenv_ret =
        node::LoadEnvironment(
            env,
            [&](const node::StartExecutionCallbackInfo &seci)
                -> MaybeLocal<Value> {
              setMyAffinity(this->threadId);
              using namespace node;
              using namespace node::native_module;
              v8::EscapableHandleScope scope(env->isolate());
              Local<String> str =
                  String::NewFromUtf8(isolate, JS_CODE.data(),
                                      v8::NewStringType::kNormal,
                                      JS_CODE.size())
                      .ToLocalChecked();
              auto main_utf16 =
                  std::make_unique<v8::String::Value>(isolate, str);
              std::string name = fmt::format("wbrunner_{}", env->thread_id());
              native_module::NativeModuleEnv::Add(
                  name.c_str(), UnionBytes(**main_utf16, main_utf16->length()));
              env->set_main_utf16(std::move(main_utf16));
              std::vector<Local<String>> params = {env->process_string(),
                                                   env->require_string()};
              std::vector<Local<Value>> args = {env->process_object(),
                                                env->native_module_require()};
              MaybeLocal<Function> maybe_fn = NativeModuleEnv::LookupAndCompile(
                  env->context(), name.c_str(), &params, env);
              Local<Function> fn;
              if (!maybe_fn.ToLocal(&fn)) {
                return MaybeLocal<Value>();
              }
              MaybeLocal<Value> result =
                  fn->Call(env->context(), v8::Undefined(env->isolate()),
                           args.size(), args.data());
              if (result.IsEmpty()) {
                env->async_hooks()->clear_async_id_stack();
              }
              return scope.EscapeMaybe(result);
            })
            .ToLocalChecked();
    if (loadenv_ret.IsEmpty()) {
      throw std::runtime_error("JS bootstrap initialization failed");
    }
    if (node::SpinEventLoop(env).FromMaybe(1) != 0) {
      throw std::runtime_error("JS bootstrap execution failed");
    }
    auto execFns = Local<v8::Array>::Cast(loadenv_ret);
    if (execFns.IsEmpty() || execFns->Length() < 2) {
      throw std::runtime_error("JS bootstrap didn't return an array of 2");
    }
    auto preExecFn = execFns->Get(ctx, 0).ToLocalChecked().As<Function>();
    data->preExecFunction = v8::Eternal<Function>(env->isolate(), preExecFn);
    auto execFn = execFns->Get(ctx, 1).ToLocalChecked().As<Function>();
    data->execFunction = v8::Eternal<Function>(env->isolate(), execFn);
  }
}

RunnerImpl::~RunnerImpl() {
  //
}

void RunnerImpl::prepareRun(const RunnerOptions &opts) {
  // const size_t memoryBytes = WAVM::Runtime::getMemoryNumPages(data->memory) *
  //                            WAVM::IR::numBytesPerPage;
  // if (memoryBytes > data->snapshot.size()) {
  //   int64_t bytesToShrink = memoryBytes - data->snapshot.size();
  //   WAVM::Runtime::shrinkMemory(data->memory,
  //                               bytesToShrink / WAVM::IR::numBytesPerPage);
  // } else if (memoryBytes < data->snapshot.size()) {
  //   int64_t bytesToGrow = data->snapshot.size() - memoryBytes;
  //   WAVM::Runtime::growMemory(data->memory,
  //                             bytesToGrow / WAVM::IR::numBytesPerPage);
  // }
  // uint8_t *memoryBase = WAVM::Runtime::getMemoryBaseAddress(data->memory);
  // std::copy(data->snapshot.cbegin(), data->snapshot.cend(), memoryBase);
  auto *isolate = data->setup->isolate();
  auto *env = data->setup->env();
  Locker locker{isolate};
  Isolate::Scope scope{isolate};
  HandleScope hScope{isolate};
  Context::Scope ctxScope{data->setup->context()};
  Local<Function> execFn = data->preExecFunction.Get(isolate);
  auto execResult =
      execFn->Call(env->context(), v8::Undefined(env->isolate()), 0, {});
  if (execResult.IsEmpty()) {
    throw std::runtime_error("JS pre-exec callback failed");
  }
}

void RunnerImpl::runOnce(const RunnerOptions &opts) {
  // const int32_t exitCode = WAVM::WASI::catchExit([&]() -> int32_t {
  //   WAVM::Runtime::invokeFunction(data->context, data->startFn,
  //                                 data->startFnType);
  //   return 0;
  // });
  // if (exitCode != 0) {
  //   fmt::print(stderr, "Warning: process exited with non-zero exit code
  //   {}\n",
  //              exitCode);
  // }
  auto *isolate = data->setup->isolate();
  auto *env = data->setup->env();
  Locker locker{isolate};
  Isolate::Scope scope{isolate};
  HandleScope hScope{isolate};
  Context::Scope ctxScope{data->setup->context()};
  Local<Function> execFn = data->execFunction.Get(isolate);
  auto execResult =
      execFn->Call(env->context(), v8::Undefined(env->isolate()), 0, {});
  if (execResult.IsEmpty()) {
    throw std::runtime_error("JS exec callback failed");
  }
}
