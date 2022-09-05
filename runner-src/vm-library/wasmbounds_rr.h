#pragma once

uint8_t *wasmboundsAllocateRegion(size_t rwSize, size_t maxSize,
                                  size_t alignmentLog2
#ifdef __cplusplus
                                  = 12
#endif
);

void wasmboundsFreeRegion(uint8_t *region, size_t size);
void wasmboundsResizeRegion(uint8_t *region, size_t oldSize, size_t newSize);
