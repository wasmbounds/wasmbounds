#include <pthread.h>
#include "runner.h"
#include "vm-library/wasmbounds_rr.hpp"
#include <chrono>

std::unique_ptr<IResizableRegionAllocator> resizableRegionAllocator = nullptr;

char *const *globalEnvp = nullptr;

void printUsage(std::string_view argv0) {
  if (argv0.empty()) {
    argv0 = "wbrunner_";
  }
  RunnerOptions defaultOpts;
  fmt::print(
      stderr,
      "Usage: {}{} [OPTIONS] benchmarkExecutable.wasm [-- wasmArg1 wasmArg2]\n",
      argv0, argv0.empty() ? WB_RUNNER_TYPE : std::string_view{});
  fmt::print(stderr, " --quiet|-q  - Don't print progress messages\n");
  fmt::print(stderr, " --loop|-L  - Run forever\n");
  fmt::print(stderr, " --threads|-t {}  - Specify number of threads\n",
             defaultOpts.numThreads);
  fmt::print(stderr,
             " --bounds-checks|-b  none|trap|clamp|[mprotect]|mdiscard|uffd - "
             "Specify bounds checking mechanism\n");
  fmt::print(stderr,
             " --min-seconds|-s {}  - Specify minimum number of seconds to run "
             "the benchmarks for. 0 = no warmup\n",
             defaultOpts.minSeconds);
  fmt::print(stderr,
             " --min-runs|-r {}  - Specify minimum number of runs per thread\n",
             defaultOpts.minRunsPerThread);
}

RunnerOptions parseArgv(const std::span<const std::string_view> args) {
  RunnerOptions opts{};
  bool errorsFound{false};
  std::span argView = args.subspan(1);
  while (!argView.empty()) {
    const std::string_view arg = argView.front();
    argView = argView.subspan(1);
    if (arg.starts_with('-')) {
      if (arg == "--threads" || arg == "-t") {
        const std::string_view val = argView.front();
        argView = argView.subspan(1);
        auto [ptr,
              ec]{std::from_chars(val.begin(), val.end(), opts.numThreads)};
        if (ec != std::errc() || opts.numThreads < 1) {
          fmt::print(stderr, "Invalid thread count {}\n", val);
          errorsFound = true;
        }
      } else if (arg == "--quiet" || arg == "-q") {
        opts.quiet = true;
      } else if (arg == "--loop" || arg == "-L") {
        opts.loop = true;
      } else if (arg == "--bounds-checks" || arg == "-b") {
        const std::string_view val = argView.front();
        argView = argView.subspan(1);
        if (val == "none") {
          opts.boundsChecks = BoundsChecks::none;
        } else if (val == "trap") {
          opts.boundsChecks = BoundsChecks::trap;
        } else if (val == "clamp") {
          opts.boundsChecks = BoundsChecks::clamp;
        } else if (val == "mprotect") {
          opts.boundsChecks = BoundsChecks::mprotect;
        } else if (val == "mdiscard") {
          opts.boundsChecks = BoundsChecks::mdiscard;
        } else if (val == "uffd") {
          opts.boundsChecks = BoundsChecks::uffd;
        } else {
          fmt::print(stderr, "Invalid bounds checking mechanism {}\n", val);
          errorsFound = true;
        }
      } else if (arg == "--min-runs" || arg == "-r") {
        const std::string_view val = argView.front();
        argView = argView.subspan(1);
        auto [ptr, ec]{
            std::from_chars(val.begin(), val.end(), opts.minRunsPerThread)};
        if (ec != std::errc() || opts.minRunsPerThread < 1) {
          fmt::print(stderr, "Invalid minimum runs {}\n", val);
          errorsFound = true;
        }
      } else if (arg == "--min-seconds" || arg == "-s") {
        const std::string_view val = argView.front();
        argView = argView.subspan(1);
        auto [ptr,
              ec]{std::from_chars(val.begin(), val.end(), opts.minSeconds)};
        if (ec != std::errc() || opts.minSeconds < 0) {
          fmt::print(stderr, "Invalid minimum seconds {}\n", val);
          errorsFound = true;
        }
      } else if (arg == "--strace") {
        opts.strace = true;
      } else if (arg == "--") {
        while (argView.size() > 0) {
          const std::string_view val = argView.front();
          argView = argView.subspan(1);
          opts.wasmArgv.emplace_back(val);
        }
      } else {
        fmt::print(stderr, "Unrecognised flag {}\n", arg);
        errorsFound = true;
      }
    } else if (opts.benchmarkExecutable.empty()) {
      opts.benchmarkExecutable = arg;
      opts.wasmArgv.emplace_back(arg);
    } else {
      fmt::print(stderr, "Unrecognised flag {}\n", arg);
      errorsFound = true;
    }
  }
  if (opts.benchmarkExecutable.empty()) {
    fmt::print(stderr, "Benchmark executable not specified\n");
    errorsFound = true;
  }
  if (errorsFound) {
    printUsage(args[0]);
    ::exit(1);
  }
  return opts;
}

void setMyAffinity(int thid) {
  cpu_set_t cpuset = {};
  CPU_ZERO(&cpuset);
  CPU_SET(thid, &cpuset);
  if (int err = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset); err != 0) {
    errno = err;
    perror("Couldn't set cpu affinity");
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char *const *const argv, char *const *const envp) {
  int originalStdoutFd = ::dup(STDOUT_FILENO);
  {
    int flags = fcntl(originalStdoutFd, F_GETFD);
    if (flags == -1) {
      perror("Getting stdout flags");
      exit(1);
    }
    flags |= FD_CLOEXEC;
    if (fcntl(originalStdoutFd, F_SETFD, flags) == -1) {
      perror("Setting stdout flags");
      exit(1);
    }
  }
  ::dup2(STDERR_FILENO, STDOUT_FILENO);
  FILE* originalStdout = ::fdopen(originalStdoutFd, "w");
  globalEnvp = envp;
  if (argc < 2) {
    printUsage(argv[0] ? argv[0] : "");
    return 1;
  }
  std::vector<std::string_view> args;
  args.reserve(argc);
  for (int argi = 0; argi < argc; argi++) {
    args.emplace_back(argv[argi]);
  }
  RunnerOptions opts = parseArgv(args);
  switch (opts.boundsChecks) {
  case BoundsChecks::none:
    [[fallthrough]];
  case BoundsChecks::trap:
    [[fallthrough]];
  case BoundsChecks::clamp:
    resizableRegionAllocator = makeRra(RraType::none);
    break;
  case BoundsChecks::mprotect:
    resizableRegionAllocator = makeRra(RraType::mprotect);
    break;
  case BoundsChecks::mdiscard:
    resizableRegionAllocator = makeRra(RraType::mdiscard);
    break;
  case BoundsChecks::uffd:
    resizableRegionAllocator = makeRra(RraType::uffd);
    break;
  }
  if (!opts.quiet)
    fmt::print(stderr, "Running runner {} with benchmark {} on {} threads\n",
               args[0], opts.benchmarkExecutable, opts.numThreads);

  std::barrier syncBarrier(opts.numThreads);
  std::once_flag printRunnersInited;
  std::mutex resultsMx;
  std::vector<int64_t> resultsUs;
  resultsUs.reserve(10240);
  std::atomic<int32_t> runningThreads{(int32_t)opts.numThreads};
  auto runnerFn = [&opts, &syncBarrier, &printRunnersInited, &resultsMx,
                   &resultsUs, &runningThreads](int thid) {
    RunnerImpl impl{opts};
    impl.threadId = thid;
    setMyAffinity(thid);
    auto tIterStart = std::chrono::high_resolution_clock::now();
    std::vector<int64_t> localResultsUs;
    localResultsUs.reserve(1024);
    syncBarrier.arrive_and_wait();
    if (!opts.quiet)
      std::call_once(printRunnersInited,
                     []() { fmt::print(stderr, "All runners initialized\n"); });
    int64_t minUs{opts.minSeconds * 1'000'000};
    auto tAllStart = std::chrono::high_resolution_clock::now();
    for (int64_t iter = (opts.minSeconds == 0) ? 1 : 0;
         opts.loop || iter < 100'000; iter++) {
      impl.prepareRun(opts);
      tIterStart = std::chrono::high_resolution_clock::now();
      impl.runOnce(opts);
      auto now = std::chrono::high_resolution_clock::now();
      int64_t usTaken = std::chrono::duration_cast<std::chrono::microseconds>(
                            now - tIterStart)
                            .count();
      if (!opts.loop && iter > 0) {
        localResultsUs.push_back(usTaken);
      }
      auto totalUsTaken =
          std::chrono::duration_cast<std::chrono::microseconds>(now - tAllStart)
              .count();
      if (iter <= 0 && totalUsTaken < 2000000) {
        iter--;
      } else if (iter <= 0) {
        tAllStart = now;
      } else if (!opts.loop && totalUsTaken >= minUs &&
                 iter >= opts.minRunsPerThread) {
        break;
      }
    }
    runningThreads.fetch_sub(1, std::memory_order_acq_rel);
    while (runningThreads.load(std::memory_order_acquire) > 0) {
      impl.prepareRun(opts);
      impl.runOnce(opts);
    }
    syncBarrier.arrive_and_wait();
    {
      std::scoped_lock _lock{resultsMx};
      resultsUs.insert(resultsUs.end(), localResultsUs.cbegin(),
                       localResultsUs.cend());
    }
  };

  int ncpus = std::thread::hardware_concurrency();
  std::vector<std::jthread> runnerThreads;
  runnerThreads.reserve(opts.numThreads);
  for (uint32_t i = 0; i < opts.numThreads - 1; i++) {
    auto runner = runnerFn; // force copy lambda
    runnerThreads.emplace_back(runner, i % ncpus);
  }
  runnerFn((opts.numThreads - 1) % ncpus);
  for (auto &thread : runnerThreads) {
    thread.join();
  }
  resultsMx.lock();
  if (!opts.quiet)
    fmt::print(stderr, "All runner threads terminated\n");

  fmt::print(originalStdout, "us");
  int64_t avg{0};
  for (int64_t us : resultsUs) {
    fmt::print(originalStdout, ",{}", us);
    avg += us;
  }
  fmt::print(originalStdout, "\n");
  avg /= std::max(size_t(1), resultsUs.size());
  fmt::print(stderr, "Average: {} us\n", avg);
  return 0;
}
