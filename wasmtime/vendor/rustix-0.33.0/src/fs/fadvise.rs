use crate::{imp, io};
use imp::fd::AsFd;

pub use imp::fs::Advice;

/// `posix_fadvise(fd, offset, len, advice)`—Declares an expected access
/// pattern for a file.
///
/// # References
///  - [POSIX]
///  - [Linux]
///
/// [POSIX]: https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_fadvise.html
/// [Linux]: https://man7.org/linux/man-pages/man2/posix_fadvise.2.html
#[inline]
#[doc(alias = "posix_fadvise")]
pub fn fadvise<Fd: AsFd>(fd: &Fd, offset: u64, len: u64, advice: Advice) -> io::Result<()> {
    let fd = fd.as_fd();
    imp::syscalls::fadvise(fd, offset, len, advice)
}
