#![allow(non_snake_case)]

use super::{RawCpuSet, CPU_SETSIZE};

#[inline]
pub(crate) fn CPU_SET(cpu: usize, cpuset: &mut RawCpuSet) {
    assert!(
        cpu < CPU_SETSIZE,
        "cpu out of bounds: the cpu max is {} but the cpu is {}",
        CPU_SETSIZE,
        cpu
    );
    unsafe { libc::CPU_SET(cpu, cpuset) }
}

#[inline]
pub(crate) fn CPU_ZERO(cpuset: &mut RawCpuSet) {
    unsafe { libc::CPU_ZERO(cpuset) }
}

#[inline]
pub(crate) fn CPU_CLR(cpu: usize, cpuset: &mut RawCpuSet) {
    assert!(
        cpu < CPU_SETSIZE,
        "cpu out of bounds: the cpu max is {} but the cpu is {}",
        CPU_SETSIZE,
        cpu
    );
    unsafe { libc::CPU_CLR(cpu, cpuset) }
}

#[inline]
pub(crate) fn CPU_ISSET(cpu: usize, cpuset: &RawCpuSet) -> bool {
    assert!(
        cpu < CPU_SETSIZE,
        "cpu out of bounds: the cpu max is {} but the cpu is {}",
        CPU_SETSIZE,
        cpu
    );
    unsafe { libc::CPU_ISSET(cpu, cpuset) }
}

#[cfg(any(target_os = "linux"))]
#[inline]
pub(crate) fn CPU_COUNT(cpuset: &RawCpuSet) -> u32 {
    use core::convert::TryInto;
    unsafe { libc::CPU_COUNT(cpuset).try_into().unwrap() }
}
