//! The `rustix` module contains code specific to the Posix-ish platforms
//! supported by the `rustix` crate.

pub(crate) mod fs;

#[cfg(any(target_os = "macos", target_os = "ios"))]
mod darwin;
#[cfg(any(target_os = "android", target_os = "linux"))]
mod linux;
