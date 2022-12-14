use bitflags::bitflags;

bitflags! {
    /// `GRND_*` flags for use with [`getrandom`].
    ///
    /// [`getrandom`]: crate::rand::getrandom
    pub struct GetRandomFlags: u32 {
        /// `GRND_RANDOM`
        const RANDOM = linux_raw_sys::v5_4::general::GRND_RANDOM;
        /// `GRND_NONBLOCK`
        const NONBLOCK = linux_raw_sys::v5_4::general::GRND_NONBLOCK;
    }
}
