#include "wasmbounds_rr.hpp"
#include "../runner.h"

#include <concepts>
#include <sys/mman.h>
#include <unistd.h>

namespace {

template <std::integral T> T ilog2(T v) {
#if defined(__GNUC__)
  return (v == 0) ? 0 : 63 - __builtin_clzll(v);
#else
  T result{0};
  while (v > 1) {
    v /= 2;
    result++;
  }
  return result;
#endif
}

constexpr size_t HUGEPAGE_ALIGNMENT = 12;

size_t roundUpToPageSize(size_t bytes) {
  // 21 for huge pages
  // 12 for regular pages
  size_t low = bytes & ((1ULL << HUGEPAGE_ALIGNMENT) - 1);
  return (low != 0) ? (bytes - low + (1ULL << HUGEPAGE_ALIGNMENT)) : bytes;
}

uint8_t *checkedAnonMmap(size_t bytes, int protection) {
  bytes = roundUpToPageSize(bytes);
  size_t lowBytes = bytes & ((1ULL << HUGEPAGE_ALIGNMENT) - 1);
  if (lowBytes != 0) {
    fmt::print(stderr, "Unaligned mmap bytes: {:x} requested, unaligned: {:x}",
               bytes, lowBytes);
    throw std::runtime_error("Anon mmap bytes not aligned on a 2MiB boundary");
  }
  // MAP_HUGETLB | ((HUGEPAGE_ALIGNMENT & MAP_HUGE_MASK) << MAP_HUGE_SHIFT)
  uint8_t *ptr = (uint8_t *)mmap(nullptr, bytes, protection,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) {
    perror("Couldn't map memory");
    std::terminate();
    return nullptr;
  }
  return ptr;
}

void checkedMunmap(uint8_t *ptr, size_t bytes) {
  bytes = roundUpToPageSize(bytes);
  if (bytes == 0 || ptr == nullptr) {
    return;
  }
  if (munmap(ptr, bytes) < 0) {
    perror("Couldn't munmap memory");
    std::terminate();
  }
}

void checkedMprotect(uint8_t *ptr, size_t bytes, int prot) {
  if (bytes == 0 || ptr == nullptr) {
    return;
  }
  if (mprotect(ptr, bytes, prot) < 0) {
    perror("Couldn't mprotect memory");
    std::terminate();
  }
}

uint8_t *alignedMmap(size_t numBytes, int alignmentLog2, int protection) {
  // From WAVM MemoryPOSIX.cpp
  // Copyright (c) 2019, Andrew Scheidecker
  // Under the license in WAVM/LICENSE in the repository
  const size_t pageSize = 2 * 1024 * 1024;
  const size_t pageSizeLog2 = ilog2(pageSize);
  const size_t numPages = numBytes >> pageSizeLog2;
  if (size_t(alignmentLog2) > pageSizeLog2) {
    throw std::runtime_error(
        "Overaligned >2MiB allocations currently disabled.");
    // Call mmap with enough padding added to the size to align the allocation
    // within the unaligned mapping.
    const uintptr_t alignmentBytes = 1ull << alignmentLog2;
    uint8_t *unalignedBaseAddress =
        checkedAnonMmap(numBytes + alignmentBytes, protection);

    const uintptr_t address = reinterpret_cast<uintptr_t>(unalignedBaseAddress);
    const uintptr_t alignedAddress =
        (address + alignmentBytes - 1) & ~(alignmentBytes - 1);
    uint8_t *result = reinterpret_cast<uint8_t *>(alignedAddress);

    // Unmap the start and end of the unaligned mapping, leaving the aligned
    // mapping in the middle.
    const uintptr_t numHeadPaddingBytes = alignedAddress - address;
    if (numHeadPaddingBytes > 0) {
      checkedMunmap(unalignedBaseAddress, numHeadPaddingBytes);
    }

    const uintptr_t numTailPaddingBytes =
        alignmentBytes - (alignedAddress - address);
    if (numTailPaddingBytes > 0) {
      checkedMunmap(result + (numPages << pageSizeLog2), numTailPaddingBytes);
    }

    return result;
  } else {
    uint8_t *unalignedBaseAddress =
        (uint8_t *)checkedAnonMmap(numBytes, protection);
    return unalignedBaseAddress;
  }
}

} // namespace

#include "wasmbounds_uffd.ipp"

struct NoneResizableRegionAllocator final : public IResizableRegionAllocator {
  NoneResizableRegionAllocator() = default;
  NoneResizableRegionAllocator(const NoneResizableRegionAllocator &) = delete;
  virtual ~NoneResizableRegionAllocator() = default;

  uint8_t *allocateRegion(size_t rwSize, size_t maxSize,
                          size_t alignmentLog2 = 12) override {
    return alignedMmap(maxSize, alignmentLog2, PROT_READ | PROT_WRITE);
  }
  void freeRegion(uint8_t *region, size_t size) override {
    checkedMunmap(region, size);
  }
  // Returns old size
  // As-if discards data if shrunk
  // As-if zero-fills data if grown
  void resizeRegion(uint8_t *region, size_t oldSize, size_t newSize) override {
    oldSize = roundUpToPageSize(oldSize);
    newSize = roundUpToPageSize(newSize);
    if (newSize < oldSize) {
      const size_t discardSize = oldSize - newSize;
      madvise(region + newSize, discardSize, MADV_DONTNEED);
    }
  }
};

struct MprotectResizableRegionAllocator final
    : public IResizableRegionAllocator {
  MprotectResizableRegionAllocator(bool useDontNeed)
      : useDontNeed(useDontNeed) {}
  MprotectResizableRegionAllocator(const MprotectResizableRegionAllocator &) =
      delete;
  virtual ~MprotectResizableRegionAllocator() = default;

  uint8_t *allocateRegion(size_t rwSize, size_t maxSize,
                          size_t alignmentLog2 = 12) override {
    uint8_t *region = alignedMmap(maxSize, alignmentLog2, PROT_NONE);
    if (rwSize > 0) {
      resizeRegion(region, 0, rwSize);
    }
    return region;
  }
  void freeRegion(uint8_t *region, size_t size) override {
    checkedMunmap(region, size);
  }
  // Returns old size
  // As-if discards data if shrunk
  // As-if zero-fills data if grown
  void resizeRegion(uint8_t *region, size_t oldSize, size_t newSize) override {
    oldSize = roundUpToPageSize(oldSize);
    newSize = roundUpToPageSize(newSize);
    if (newSize < oldSize) {
      const size_t discardSize = oldSize - newSize;
      if (useDontNeed) {
        madvise(region + newSize, discardSize, MADV_DONTNEED);
        checkedMprotect(region + newSize, discardSize, PROT_NONE);
      } else {
        // MAP_HUGETLB | ((HUGEPAGE_ALIGNMENT & MAP_HUGE_MASK) <<
        // MAP_HUGE_SHIFT)
        if (mmap(region + newSize, discardSize, PROT_NONE,
                 MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1,
                 0) == MAP_FAILED) {
          perror("Couldn't fixed-remap fresh memory");
          std::terminate();
        }
      }
    } else if (newSize > oldSize) {
      const size_t growSize = newSize - oldSize;
      checkedMprotect(region + oldSize, growSize, PROT_READ | PROT_WRITE);
    }
  }

  bool useDontNeed;
};

struct UffdResizableRegionAllocator final : public IResizableRegionAllocator {
  UffdResizableRegionAllocator() { UffdMemoryArenaManager::instance(); }
  UffdResizableRegionAllocator(const UffdResizableRegionAllocator &) = delete;
  virtual ~UffdResizableRegionAllocator() = default;

  uint8_t *allocateRegion(size_t rwSize, size_t maxSize,
                          size_t alignmentLog2 = 12) override {
    auto &umam = UffdMemoryArenaManager::instance();
    std::byte *range = umam.allocateRange(maxSize / 4096, alignmentLog2);
    if (rwSize > 0) {
      umam.modifyRange(range, [&](UffdMemoryRange &r) { r.resize(rwSize); });
    }
    return (uint8_t *)range;
  }
  void freeRegion(uint8_t *region, size_t size) override {
    auto &umam = UffdMemoryArenaManager::instance();
    umam.freeRange((std::byte *)region);
  }
  // Returns old size
  // As-if discards data if shrunk
  // As-if zero-fills data if grown
  void resizeRegion(uint8_t *region, size_t oldSize, size_t newSize) override {
    oldSize = roundUpToPageSize(oldSize);
    newSize = roundUpToPageSize(newSize);
    auto &umam = UffdMemoryArenaManager::instance();
    umam.modifyRange((std::byte *)region,
                     [&](UffdMemoryRange &r) { r.resize(newSize); });
  }
};

struct ReusingRegionAllocator final : public IResizableRegionAllocator {
  std::unique_ptr<IResizableRegionAllocator> delegate;
  std::mutex freeListMx;
  std::vector<std::pair<uint8_t*, size_t>> freeList;

  ReusingRegionAllocator(std::unique_ptr<IResizableRegionAllocator> delegate) : delegate(std::move(delegate)) {}
  ReusingRegionAllocator(const ReusingRegionAllocator &) = delete;
  virtual ~ReusingRegionAllocator() = default;

  uint8_t *allocateRegion(size_t rwSize, size_t maxSize,
                          size_t alignmentLog2 = 12) override {
    {
      std::lock_guard<std::mutex> _l{freeListMx};
      auto it = freeList.begin();
      for (; it != freeList.end(); ++it) {
        if (it->second == maxSize) {
          break;
        }
      }
      if (it != freeList.end()) {
        uint8_t * ptr = it->first;
        freeList.erase(it);
        resizeRegion(ptr, 0, rwSize);
        return ptr;
      }
    }
    return delegate->allocateRegion(rwSize, maxSize, alignmentLog2);
  }
  void freeRegion(uint8_t *region, size_t size) override {
    resizeRegion(region, size, 0);
    std::lock_guard<std::mutex> _l{freeListMx};
    freeList.emplace_back(region, size);
  }
  // Returns old size
  // As-if discards data if shrunk
  // As-if zero-fills data if grown
  void resizeRegion(uint8_t *region, size_t oldSize, size_t newSize) override {
    if (oldSize != newSize) {
      delegate->resizeRegion(region, oldSize, newSize);
    }
  }
};

std::unique_ptr<IResizableRegionAllocator> makeRra(RraType type) {
  switch (type) {
  case RraType::none:
    return std::make_unique<NoneResizableRegionAllocator>();
  case RraType::mprotect:
    return std::make_unique<MprotectResizableRegionAllocator>(false);
  case RraType::mdiscard:
    return std::make_unique<MprotectResizableRegionAllocator>(true);
  case RraType::uffd:
    return std::make_unique<UffdResizableRegionAllocator>();
  default:
    fmt::print(stderr, "Invalid rra type {}", int(type));
    std::terminate();
  }
}

std::unique_ptr<IResizableRegionAllocator> wrapInReusingRra(std::unique_ptr<IResizableRegionAllocator> rra) {
  return std::make_unique<ReusingRegionAllocator>(std::move(rra));
}

// C API
extern "C" {
uint8_t *wasmboundsAllocateRegion(size_t rwSize, size_t maxSize,
                                  size_t alignmentLog2) {
  return resizableRegionAllocator->allocateRegion(rwSize, maxSize,
                                                  alignmentLog2);
}

void wasmboundsFreeRegion(uint8_t *region, size_t size) {
  return resizableRegionAllocator->freeRegion(region, size);
}
void wasmboundsResizeRegion(uint8_t *region, size_t oldSize, size_t newSize) {
  return resizableRegionAllocator->resizeRegion(region, oldSize, newSize);
}
}
