
#include <cstdlib>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <utility>

#include "userfaultfd.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <absl/container/btree_set.h>
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>

struct UserfaultFd {
  int fd = -1;
  uffdio_api api = {};

  // Only allow moving, not copying - owns the fd
  inline UserfaultFd() {}
  inline ~UserfaultFd() { clear(); }
  UserfaultFd(const UserfaultFd &) = delete;
  inline UserfaultFd(UserfaultFd &&other) { this->operator=(std::move(other)); }
  UserfaultFd &operator=(const UserfaultFd &) = delete;
  inline UserfaultFd &operator=(UserfaultFd &&other) {
    fd = other.fd;
    api = other.api;
    other.fd = -1;
    other.api = {};
    return *this;
  }

  // Close fd if present
  inline void clear() {
    if (fd >= 0) {
      close(fd);
      fd = -1;
      api = {};
    }
  }

  // Release ownership and return the fd
  std::pair<int, uffdio_api> release();

  void create(int flags = 0, bool sigbus = false);

  // Thread-safe
  inline void checkFd() {
    if (fd < 0) {
      throw std::runtime_error("UFFD fd not initialized");
    }
  }

  // Write-protect mode requires at least Linux 5.7 kernel
  // Thread-safe
  void registerAddressRange(size_t startPtr, size_t length, bool modeMissing,
                            bool modeWriteProtect);

  // Thread-safe
  void unregisterAddressRange(size_t startPtr, size_t length);

  // Thread-safe
  std::optional<uffd_msg> readEvent();

  // Thread-safe
  void writeProtectPages(size_t startPtr, size_t length,
                         bool preventWrites = true, bool dontWake = false);

  // Thread-safe
  void zeroPages(size_t startPtr, size_t length, bool dontWake = false);

  // Thread-safe
  void copyPages(size_t targetStartPtr, size_t length, size_t sourceStartPtr,
                 bool writeProtect = false, bool dontWake = false);

  // Thread-safe
  void wakePages(size_t startPtr, size_t length);
};

std::pair<int, uffdio_api> UserfaultFd::release() {
  int oldFd = fd;
  uffdio_api oldApi = api;
  fd = -1;
  api = {};
  return std::make_pair(oldFd, oldApi);
}

void UserfaultFd::create(int flags, bool sigbus) {
  clear();
  int result = syscall(SYS_userfaultfd, flags);
  if (result < 0) {
    errno = -result;
    perror("Error creating userfaultfd");
    throw std::runtime_error("Couldn't create userfaultfd");
  }
  fd = result;
  api.api = UFFD_API;
  api.features = UFFD_FEATURE_THREAD_ID;
  if (sigbus) {
    api.features |= UFFD_FEATURE_SIGBUS;
  }
  api.ioctls = 0;
  result = ioctl(fd, UFFDIO_API, &api);
  if (result < 0) {
    throw std::runtime_error("Couldn't handshake userfaultfd api");
  }
}

void UserfaultFd::registerAddressRange(size_t startPtr, size_t length,
                                       bool modeMissing,
                                       bool modeWriteProtect) {
  checkFd();
  uffdio_register r = {};
  if (!(modeMissing || modeWriteProtect)) {
    throw std::invalid_argument(
        "UFFD register call must have at least one mode enabled");
  }
  if (modeMissing) {
    r.mode |= UFFDIO_REGISTER_MODE_MISSING;
  }
  if (modeWriteProtect) {
    if ((api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) == 0) {
      throw std::runtime_error("WriteProtect mode on UFFD not supported");
    }
    r.mode |= UFFDIO_REGISTER_MODE_WP;
  }
  r.range.start = startPtr;
  r.range.len = length;
  if (ioctl(fd, UFFDIO_REGISTER, &r) < 0) {
    perror("UFFDIO_REGISTER error");
    throw std::runtime_error("Couldn't register an address range with UFFD");
  }
}

void UserfaultFd::unregisterAddressRange(size_t startPtr, size_t length) {
  checkFd();
  uffdio_range r = {};
  r.start = startPtr;
  r.len = length;
  if (ioctl(fd, UFFDIO_UNREGISTER, &r) < 0) {
    perror("UFFDIO_UNREGISTER error");
    throw std::runtime_error("Couldn't unregister an address range from UFFD");
  }
}

std::optional<uffd_msg> UserfaultFd::readEvent() {
  checkFd();
  uffd_msg ev;
retry:
  int result = read(fd, (void *)&ev, sizeof(uffd_msg));
  if (result < 0) {
    if (errno == EAGAIN) {
      goto retry;
    } else if (errno == EWOULDBLOCK) {
      return std::nullopt;
    } else {
      perror("read from UFFD error");
      throw std::runtime_error("Error reading from the UFFD");
    }
  }
  return ev;
}
void UserfaultFd::writeProtectPages(size_t startPtr, size_t length,
                                    bool preventWrites, bool dontWake) {
  checkFd();
  if ((api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) == 0) {
    throw std::runtime_error("Write-protect pages not supported by "
                             "UFFD on this kernel version");
  }
  uffdio_writeprotect wp = {};
  if (preventWrites) {
    wp.mode |= UFFDIO_WRITEPROTECT_MODE_WP;
  }
  if (dontWake) {
    wp.mode |= UFFDIO_WRITEPROTECT_MODE_DONTWAKE;
  }
  wp.range.start = startPtr;
  wp.range.len = length;
retry:
  if (ioctl(fd, UFFDIO_WRITEPROTECT, &wp) < 0) {
    if (errno == EAGAIN) {
      goto retry;
    }
    if (errno == EEXIST) {
      return;
    }
    perror("UFFDIO_WRITEPROTECT error");
    throw std::runtime_error(
        "Couldn't write-protect-modify an address range through UFFD");
  }
}

void UserfaultFd::zeroPages(size_t startPtr, size_t length, bool dontWake) {
  checkFd();
  uffdio_zeropage zp = {};
  if (dontWake) {
    zp.mode |= UFFDIO_ZEROPAGE_MODE_DONTWAKE;
  }
  zp.range.start = startPtr;
  zp.range.len = length;
retry:
  if (ioctl(fd, UFFDIO_ZEROPAGE, &zp) < 0) {
    if (errno == EAGAIN) {
      goto retry;
    }
    if (errno == EEXIST) {
      return;
    }
    perror("UFFDIO_ZEROPAGE error");
    throw std::runtime_error(
        "Couldn't zero-page an address range through UFFD");
  }
}

void UserfaultFd::copyPages(size_t targetStartPtr, size_t length,
                            size_t sourceStartPtr, bool writeProtect,
                            bool dontWake) {
  checkFd();
  uffdio_copy cp = {};
  if (dontWake) {
    cp.mode |= UFFDIO_COPY_MODE_DONTWAKE;
  }
  if (writeProtect) {
    cp.mode |= UFFDIO_COPY_MODE_WP;
  }
  cp.src = sourceStartPtr;
  cp.len = length;
  cp.dst = targetStartPtr;
retry:
  if (ioctl(fd, UFFDIO_COPY, &cp) < 0) {
    if (errno == EAGAIN) {
      goto retry;
    }
    if (errno == EEXIST) {
      return;
    }
    perror("UFFDIO_COPY error");
    throw std::runtime_error("Couldn't copy an address range through UFFD");
  }
}

void UserfaultFd::wakePages(size_t startPtr, size_t length) {
  checkFd();
  uffdio_range wr = {};
  wr.start = startPtr;
  wr.len = length;
retry:
  if (ioctl(fd, UFFDIO_WAKE, &wr) < 0) {
    if (errno == EAGAIN) {
      goto retry;
    }
    perror("UFFDIO_WAKE error");
    throw std::runtime_error("Couldn't wake an address range through UFFD");
  }
}

struct UffdMemoryRange : public std::enable_shared_from_this<UffdMemoryRange> {
  std::byte *mapStart = nullptr;
  size_t mapBytes = 0;
  std::atomic<size_t> validBytes = 0;
  std::mutex mx;

  UffdMemoryRange() = delete;
  ~UffdMemoryRange() noexcept;
  explicit UffdMemoryRange(size_t pages, size_t alignmentLog2 = 0);
  UffdMemoryRange(const UffdMemoryRange &) = delete;
  UffdMemoryRange &operator=(const UffdMemoryRange &) = delete;
  inline UffdMemoryRange(UffdMemoryRange &&rhs) noexcept {
    *this = std::move(rhs);
  }
  inline UffdMemoryRange &operator=(UffdMemoryRange &&rhs) noexcept {
    this->~UffdMemoryRange();
    this->mapStart = rhs.mapStart;
    this->mapBytes = rhs.mapBytes;
    this->validBytes = rhs.validBytes.load();
    rhs.validBytes = 0;
    rhs.mapStart = nullptr;
    rhs.mapBytes = 0;
    return *this;
  }

  inline void discardAll() {
    const size_t wasBytes = validBytes.exchange(0, std::memory_order_acq_rel);
    madvise((void *)mapStart, wasBytes, MADV_DONTNEED);
  }

  inline void resize(size_t newSize) {
    size_t oldSize =
        this->validBytes.exchange(newSize, std::memory_order_acq_rel);
    if (oldSize > newSize) {
      madvise((void *)(mapStart + newSize), oldSize - newSize, MADV_DONTNEED);
    }
  }

  inline bool pointerInRange(const std::byte *ptr) const {
    size_t addr = (size_t)ptr;
    return addr >= (size_t)mapStart && addr < (size_t)(mapStart + mapBytes);
  }

  inline bool pointerInValidRange(const std::byte *ptr) const {
    size_t addr = (size_t)ptr;
    return addr >= (size_t)mapStart &&
           addr <
               (size_t)(mapStart + validBytes.load(std::memory_order_acquire));
  }

  inline std::strong_ordering operator<=>(const UffdMemoryRange &rhs) const {
    return size_t(mapStart) <=> size_t(rhs.mapStart);
  }

  inline friend std::strong_ordering operator<=>(const UffdMemoryRange &umr,
                                                 const std::byte *ptr) {
    if (umr.pointerInRange(ptr)) {
      return std::strong_ordering::equal;
    }
    return size_t(umr.mapStart) <=> size_t(ptr);
  }

  inline friend std::strong_ordering operator<=>(const std::byte *ptr,
                                                 const UffdMemoryRange &umr) {
    return umr <=> ptr;
  }
};

struct UffdMemoryRangeLess final {
  using is_transparent = std::true_type;

  bool operator()(const UffdMemoryRange &a1, const std::byte *a2) const {
    return a1 < a2;
  }

  bool operator()(const std::byte *a1, const UffdMemoryRange &a2) const {
    return a1 < a2;
  }

  bool operator()(const UffdMemoryRange &a1, const UffdMemoryRange &a2) const {
    return a1 < a2;
  }

  bool operator()(const std::shared_ptr<UffdMemoryRange> &a1,
                  const std::shared_ptr<UffdMemoryRange> &a2) const {
    return (*a1) < (*a2);
  }

  bool operator()(const std::shared_ptr<UffdMemoryRange> &a1,
                  const std::byte *a2) const {
    return (*a1) < a2;
  }

  bool operator()(const std::byte *a1,
                  const std::shared_ptr<UffdMemoryRange> &a2) const {
    return a1 < (*a2);
  }
};

class UffdMemoryArenaManager final {
public:
  UffdMemoryArenaManager(const UffdMemoryArenaManager &) = delete;
  UffdMemoryArenaManager &operator=(const UffdMemoryArenaManager &) = delete;
  static UffdMemoryArenaManager &instance();

  // Allocates a UFFD-backed memory range, and returns the pointer to the
  // beginning of it.
  std::byte *allocateRange(size_t pages, size_t alignmentLog2 = 0);

  void modifyRange(std::byte *start,
                   std::function<void(UffdMemoryRange &)> action);

  // Remove and free a UFFD-backed range completely
  void freeRange(std::byte *start);

  // The signal handler
  friend void sigbusHandler(int code, siginfo_t *siginfo, void *contextR);

  using RangeSet =
      absl::btree_set<std::shared_ptr<UffdMemoryRange>, UffdMemoryRangeLess>;

  std::shared_ptr<RangeSet> rangesSnapshot() const {
    return std::atomic_load_explicit(&ranges, std::memory_order_acquire);
  }

private:
  std::shared_mutex mx;
  UserfaultFd uffd;
  std::shared_ptr<RangeSet> ranges;

  // Needs to be called while holding an exclusive lock over mx
  // Don't hold a reference to old ranges or a deadlock will happen
  void updateRanges(std::shared_ptr<RangeSet> newRanges) {
    std::shared_ptr oldRanges = std::atomic_exchange_explicit(
        &ranges, newRanges, std::memory_order_acq_rel);
    // Make it most likely for this thread to destroy the object rather than
    // a reader
    while (oldRanges.use_count() > 1) {
      std::this_thread::yield();
    }
  }

  UffdMemoryArenaManager();
  ~UffdMemoryArenaManager() = default;
};

void checkErrno(int ec, const char *msg) {
  if (ec < 0) {
    perror(msg);
    throw std::system_error(errno, std::generic_category(), msg);
  }
}

UffdMemoryRange::~UffdMemoryRange() noexcept {
  if (this->mapStart != nullptr && this->mapBytes > 0) {
    checkedMunmap((uint8_t *)mapStart, mapBytes);
    this->mapStart = nullptr;
    this->mapBytes = 0;
    this->validBytes = 0;
  }
}

UffdMemoryRange::UffdMemoryRange(size_t pages, size_t alignmentLog2) {
  const size_t numBytes = pages * 4096;

  this->mapStart =
      (std::byte *)alignedMmap(numBytes, alignmentLog2, PROT_READ | PROT_WRITE);
  this->mapBytes = numBytes;
  this->validBytes = 0;
}

UffdMemoryArenaManager &UffdMemoryArenaManager::instance() {
  static UffdMemoryArenaManager umam;
  return umam;
}

void sigbusHandler(int code, siginfo_t *siginfo, void *contextR) {
  constexpr size_t FAULT_PAGE_SIZE = 65536;
  if (code != SIGBUS) [[unlikely]] {
    std::terminate();
  }
  std::byte *faultAddr = (std::byte *)siginfo->si_addr;
  thread_local std::weak_ptr<UffdMemoryRange> rangeCache;
  std::shared_ptr range = rangeCache.lock();
  UffdMemoryArenaManager &umam = UffdMemoryArenaManager::instance();
  if (!range || !range->pointerInRange(faultAddr)) [[unlikely]] {
    auto ranges = umam.rangesSnapshot();
    auto rangeIt = ranges->find(faultAddr);
    if (rangeIt == ranges->end()) [[unlikely]] {
      fmt::print(stderr, "SIGBUS invalid access of memory at pointer {}\n",
                 (void *)(faultAddr));
      // faabric::util::printStackTrace(contextR);
      std::terminate();
    }
    range = *rangeIt;
    rangeCache = range;
  }
  std::unique_lock<std::mutex> rangeLock{range->mx};
  if (!range->pointerInValidRange(faultAddr)) [[unlikely]] {
    fmt::print(stderr,
               "[!] UFFD out of bounds access of memory at pointer {} in "
               "range {}..{}..{}\n",
               (void *)(faultAddr), (void *)(range->mapStart),
               (void *)(range->mapStart + range->validBytes.load()),
               (void *)(range->mapStart + range->mapBytes));
    rangeLock.unlock();
    // faabric::util::printStackTrace(contextR);
    ::pthread_kill(::pthread_self(), SIGSEGV);
    return;
  }
  size_t faultPageOffset =
      ((size_t(faultAddr) - size_t(range->mapStart)) & ~(FAULT_PAGE_SIZE - 1));

  size_t faultEnd =
      std::min(size_t(faultPageOffset) + FAULT_PAGE_SIZE, range->mapBytes);
  size_t faultSize = faultEnd - faultPageOffset;
  rangeLock.unlock();
  umam.uffd.zeroPages(size_t(range->mapStart) + faultPageOffset, faultSize);
}

UffdMemoryArenaManager::UffdMemoryArenaManager() {
  std::unique_lock<std::shared_mutex> lock{mx};
  uffd.create(O_CLOEXEC, true);
  struct sigaction action;
  action.sa_flags = SA_RESTART | SA_SIGINFO;
  action.sa_handler = nullptr;
  action.sa_sigaction = &sigbusHandler;
  sigemptyset(&action.sa_mask);
  checkErrno(sigaction(SIGBUS, &action, nullptr),
             "Couldn't register UFFD SIGBUS handler");
  updateRanges(std::make_shared<RangeSet>());
}

std::byte *UffdMemoryArenaManager::allocateRange(size_t pages,
                                                 size_t alignmentLog2) {
  std::shared_ptr range =
      std::make_shared<UffdMemoryRange>(pages, alignmentLog2);
  std::byte *start = range->mapStart;
  {
    std::unique_lock<std::shared_mutex> lock{mx};

    this->uffd.registerAddressRange(size_t(range->mapStart), range->mapBytes,
                                    true, false);
    std::shared_ptr newRanges = std::make_shared<RangeSet>(*rangesSnapshot());
    newRanges->insert(std::move(range));
    this->updateRanges(std::move(newRanges));
  }
  return start;
}

void UffdMemoryArenaManager::modifyRange(
    std::byte *start, std::function<void(UffdMemoryRange &)> action) {
  auto rangesSnap = this->rangesSnapshot();
  auto rangeIt = rangesSnap->find(start);
  if (rangeIt == rangesSnap->end()) {
    throw std::runtime_error("Invalid pointer for UFFD range action");
  }
  std::shared_ptr range = *rangeIt;
  std::unique_lock<std::mutex> rangeLock{range->mx};
  if (!range->pointerInRange(start)) {
    throw std::runtime_error("Invalid pointer found for UFFD range action");
  }
  action(*range);
}

void UffdMemoryArenaManager::freeRange(std::byte *start) {
  std::unique_lock<std::shared_mutex> lock{mx};
  std::shared_ptr newRanges = std::make_shared<RangeSet>(*rangesSnapshot());
  auto rangeIt = newRanges->find(start);
  if (rangeIt == newRanges->end()) {
    throw std::runtime_error("Invalid pointer for UFFD range removal");
  }
  auto &range = *rangeIt;
  std::unique_lock<std::mutex> rangeLock{range->mx};
  size_t mapStart = size_t(range->mapStart);
  size_t mapLength = range->mapBytes;
  uffd.unregisterAddressRange(mapStart, mapLength);
  range->discardAll();
  rangeLock.unlock();
  newRanges->erase(rangeIt);
  this->updateRanges(std::move(newRanges));
}
