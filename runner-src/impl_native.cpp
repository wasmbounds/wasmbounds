#include "runner.h"

#include <spawn.h>
#include <sys/wait.h>

const std::string_view WB_RUNNER_TYPE = WB_RUNNER_TYPE_STR;

std::once_flag initOnce;

struct DataImpl {
  static inline int exeFd = -1;
  std::vector<const char *> childArgv;
};

RunnerImpl::RunnerImpl(const RunnerOptions &opts)
    : data(std::make_unique<DataImpl>()) {
  if (opts.boundsChecks != BoundsChecks::none) {
    fmt::print(stderr, "Invalid bounds checking mechanism for the native "
                       "runner, only 'none' is supported");
    std::terminate();
  }

  std::call_once(initOnce, [&](){
    DataImpl::exeFd = open(opts.benchmarkExecutable.c_str(), O_RDONLY | O_CLOEXEC);
    if (DataImpl::exeFd < 0) {
      perror("Couldn't open the benchmark executable file");
      std::terminate();
    }
  });

  data->childArgv = {};
  for (const auto &arg : opts.wasmArgv) {
    data->childArgv.push_back(arg.c_str());
  }
  data->childArgv.push_back(nullptr);
}

RunnerImpl::~RunnerImpl() {
}

void RunnerImpl::prepareRun(const RunnerOptions &opts) {}

void RunnerImpl::runOnce(const RunnerOptions &opts) {
  pid_t childPid{};
  char *const *childEnvp = globalEnvp;
  char *const *childArgv = const_cast<char *const *>(data->childArgv.data());
  pid_t vfResult = vfork();
  if (vfResult == 0) {
    // in child
    ::dup2(STDERR_FILENO, STDOUT_FILENO);
    ::fexecve(DataImpl::exeFd, childArgv, childEnvp);
    ::_Exit(1);
  } else if (vfResult > 0) {
    childPid = vfResult;
  } else {
    perror("Couldn't spawn child");
    std::terminate();
  }
  int childStatus{};
  int result = waitpid(childPid, &childStatus, 0);
  if (result == -1) {
    perror("Couldn't await child benchmark");
    std::terminate();
  }
  if (!WIFEXITED(childStatus)) {
    perror("Child benchmark terminated abnormally");
    std::terminate();
  }
  if (int status = WEXITSTATUS(childStatus); status != 0) {
    fmt::print(stderr, "Child benchmark terminated with code {}", status);
    std::terminate();
  }
}
