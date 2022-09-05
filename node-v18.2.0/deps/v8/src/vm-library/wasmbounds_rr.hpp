#pragma once
// Wasmbounds Resizable Region API
// Made to be integrated into each WASM VM, replacing its memory
// allocation/deallocation for WASM memories.

#include <cstdint>
#include <cstdlib>
#include <memory>

// All functions must be thread-safe (use internal locking)
struct IResizableRegionAllocator {
  IResizableRegionAllocator() = default;
  IResizableRegionAllocator(const IResizableRegionAllocator &) = delete;
  virtual ~IResizableRegionAllocator() = default;

  virtual uint8_t *allocateRegion(size_t rwSize, size_t maxSize,
                                  size_t alignmentLog2 = 12) = 0;
  virtual void freeRegion(uint8_t *region, size_t size) = 0;
  // Returns old size
  // As-if discards data if shrunk
  // As-if zero-fills data if grown
  virtual void resizeRegion(uint8_t *region, size_t oldSize,
                            size_t newSize) = 0;
};

extern std::unique_ptr<IResizableRegionAllocator> resizableRegionAllocator;

enum class RraType : uint32_t {
  none = 0,
  mprotect,
  mdiscard,
  uffd,
};

std::unique_ptr<IResizableRegionAllocator> makeRra(RraType type);

extern "C" {
#include "wasmbounds_rr.h"
}
