//! Wasmbounds hooks

// uint8_t *wasmboundsAllocateRegion(size_t rwSize, size_t maxSize,
// size_t alignmentLog2 = 12);

// void wasmboundsFreeRegion(uint8_t *region, size_t size);
// void wasmboundsResizeRegion(uint8_t *region, size_t oldSize, size_t newSize);

use std::ptr::null_mut;

extern "C" {
    fn wasmboundsAllocateRegion(
        rw_size: libc::size_t,
        max_size: libc::size_t,
        alignment_log2: libc::size_t,
    ) -> *mut u8;
    fn wasmboundsFreeRegion(region: *mut u8, size: libc::size_t);
    fn wasmboundsResizeRegion(region: *mut u8, old_size: libc::size_t, new_size: libc::size_t);
}

/// Allocate a resizable region
pub fn allocate_region(rw_size: usize, capacity: usize, alignment_log2: usize) -> *mut u8 {
    unsafe { wasmboundsAllocateRegion(rw_size, capacity, alignment_log2) }
}

/// Free a resizable region
/// # Safety
/// Region must come from allocate_region, with the right size
pub unsafe fn free_region(region: *mut u8, capacity: usize) {
    wasmboundsFreeRegion(region, capacity)
}

/// Resize a resizable region
/// # Safety
/// Region must come from allocate_region, with the right size
pub unsafe fn resize_region(region: *mut u8, old_size: usize, new_size: usize) {
    wasmboundsResizeRegion(region, old_size, new_size)
}

/// Safe wrapper around resizable regions
#[derive(Debug)]
pub struct WasmboundsResizableRegion {
    region: *mut u8,
    capacity: usize,
    size: usize,
}

unsafe impl Send for WasmboundsResizableRegion {}
unsafe impl Sync for WasmboundsResizableRegion {}

impl WasmboundsResizableRegion {
    /// New region
    pub fn new(initial_size: usize, capacity: usize, alignment_log2: usize) -> Self {
        Self {
            region: allocate_region(initial_size, capacity, alignment_log2),
            capacity,
            size: initial_size,
        }
    }

    /// Access capacity (in bytes)
    pub fn capacity(&self) -> usize {
        self.capacity
    }

    /// Access current valid size (in bytes)
    pub fn size(&self) -> usize {
        self.size
    }

    /// Resize the region
    pub fn resize(&mut self, new_size: usize) {
        assert!(!self.region.is_null());
        if new_size != self.size {
            unsafe {
                resize_region(self.region, self.size, new_size);
            }
            self.size = new_size;
        }
    }

    /// Gets the (safe, non-null) data pointer for the region
    /// # Safety
    /// The returned pointer has a lifetime as-if derived from &mut self
    pub unsafe fn as_mut_ptr(&self) -> *mut u8 {
        assert!(!self.region.is_null());
        self.region
    }

    /// Gets the (safe, non-null) data pointer for the region
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        assert!(!self.region.is_null());
        // Safety: Exclusive reference is held for self
        unsafe { std::slice::from_raw_parts_mut(self.region, self.size) }
    }
}

impl Drop for WasmboundsResizableRegion {
    fn drop(&mut self) {
        if !self.region.is_null() {
            unsafe {
                free_region(self.region, self.capacity);
            }
            self.region = null_mut();
        }
    }
}
