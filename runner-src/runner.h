#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <charconv>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern const std::string_view WB_RUNNER_TYPE;
extern char *const *globalEnvp;

enum class BoundsChecks { none, trap, clamp, mprotect, mdiscard, uffd };

struct RunnerOptions {
  // Commandline options
  std::string benchmarkExecutable;
  uint32_t numThreads = std::thread::hardware_concurrency();
  BoundsChecks boundsChecks = BoundsChecks::mprotect;
  int64_t minSeconds = 5;
  int64_t minRunsPerThread = 10;
  bool quiet = false;
  bool loop = false;
  std::vector<std::string> wasmArgv;
  bool strace = false;
};

struct DataImpl;

// One instance is created per worker thread
struct RunnerImpl final {
  RunnerImpl() = delete;
  RunnerImpl(const RunnerOptions &opts);
  ~RunnerImpl();
  RunnerImpl(const RunnerImpl &) = delete;
  RunnerImpl &operator=(const RunnerImpl &) = delete;
  int threadId = 0;
  std::unique_ptr<DataImpl> data;

  void prepareRun(const RunnerOptions &opts);
  void runOnce(const RunnerOptions &opts);
};

void setMyAffinity(int cpu);

inline std::vector<uint8_t> readFileToBytes(const std::string &path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    throw std::runtime_error("Couldn't open file " + path);
  }
  struct stat statbuf;
  int staterr = fstat(fd, &statbuf);
  if (staterr < 0) {
    throw std::runtime_error("Couldn't stat file " + path);
  }
  size_t fsize = statbuf.st_size;
  posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
  std::vector<uint8_t> result;
  result.resize(fsize);
  size_t cpos = 0;
  while (cpos < fsize) {
    ssize_t rc = read(fd, result.data(), fsize - cpos);
    if (rc < 0) {
      perror("Couldn't read file");
      throw std::runtime_error("Couldn't read file " + path);
    } else {
      cpos += rc;
    }
  }
  close(fd);
  return result;
}
