#![no_std]

use core::{mem, slice};

pub struct RawBytes;

impl RawBytes {
    #[inline(always)]
    pub fn bytes_view<T: Sync + Unpin + ?Sized>(v: &T) -> &[u8] {
        unsafe { slice::from_raw_parts(v as *const _ as *const u8, mem::size_of_val(v)) }
    }

    #[inline(always)]
    pub fn bytes_view_mut<T: Sync + Unpin + ?Sized>(v: &mut T) -> &mut [u8] {
        unsafe { slice::from_raw_parts_mut(v as *mut _ as *mut u8, mem::size_of_val(v)) }
    }
}
