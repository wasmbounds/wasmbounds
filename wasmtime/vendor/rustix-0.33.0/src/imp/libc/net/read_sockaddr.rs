use super::super::c;
use super::ext::{in6_addr_s6_addr, in_addr_s_addr, sockaddr_in6_sin6_scope_id};
#[cfg(not(windows))]
use super::SocketAddrUnix;
#[cfg(not(windows))]
use crate::as_ptr;
#[cfg(not(windows))]
use crate::ffi::ZStr;
use crate::io;
use crate::net::{Ipv4Addr, Ipv6Addr, SocketAddrAny, SocketAddrV4, SocketAddrV6};
#[cfg(not(windows))]
use alloc::vec::Vec;
use core::mem::size_of;

// This must match the header of `sockaddr`.
#[repr(C)]
struct sockaddr_header {
    #[cfg(any(
        target_os = "netbsd",
        target_os = "macos",
        target_os = "ios",
        target_os = "freebsd",
        target_os = "openbsd"
    ))]
    sa_len: u8,
    #[cfg(any(
        target_os = "netbsd",
        target_os = "macos",
        target_os = "ios",
        target_os = "freebsd",
        target_os = "openbsd"
    ))]
    ss_family: u8,
    #[cfg(not(any(
        target_os = "netbsd",
        target_os = "macos",
        target_os = "ios",
        target_os = "freebsd",
        target_os = "openbsd"
    )))]
    ss_family: u16,
}

#[inline]
unsafe fn read_ss_family(storage: *const c::sockaddr_storage) -> u16 {
    // Assert that we know the layout of `sockaddr`.
    let _ = c::sockaddr {
        #[cfg(any(
            target_os = "dragonfly",
            target_os = "freebsd",
            target_os = "ios",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        sa_len: 0_u8,
        #[cfg(any(
            target_os = "dragonfly",
            target_os = "ios",
            target_os = "freebsd",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        sa_family: 0_u8,
        #[cfg(not(any(
            target_os = "dragonfly",
            target_os = "ios",
            target_os = "freebsd",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        )))]
        sa_family: 0_u16,
        sa_data: [0; 14],
    };

    (*storage.cast::<sockaddr_header>()).ss_family.into()
}

pub(crate) unsafe fn read_sockaddr(
    storage: *const c::sockaddr_storage,
    len: usize,
) -> io::Result<SocketAddrAny> {
    #[cfg(not(windows))]
    let offsetof_sun_path = super::offsetof_sun_path();

    if len < size_of::<c::sa_family_t>() {
        return Err(io::Error::INVAL);
    }
    match read_ss_family(storage).into() {
        c::AF_INET => {
            if len < size_of::<c::sockaddr_in>() {
                return Err(io::Error::INVAL);
            }
            let decode = *storage.cast::<c::sockaddr_in>();
            Ok(SocketAddrAny::V4(SocketAddrV4::new(
                Ipv4Addr::from(u32::from_be(in_addr_s_addr(decode.sin_addr))),
                u16::from_be(decode.sin_port),
            )))
        }
        c::AF_INET6 => {
            if len < size_of::<c::sockaddr_in6>() {
                return Err(io::Error::INVAL);
            }
            let decode = *storage.cast::<c::sockaddr_in6>();
            #[cfg(not(windows))]
            let s6_addr = decode.sin6_addr.s6_addr;
            #[cfg(windows)]
            let s6_addr = *decode.sin6_addr.u.Byte();
            #[cfg(not(windows))]
            let sin6_scope_id = decode.sin6_scope_id;
            #[cfg(windows)]
            let sin6_scope_id = *decode.u.sin6_scope_id();
            Ok(SocketAddrAny::V6(SocketAddrV6::new(
                Ipv6Addr::from(s6_addr),
                u16::from_be(decode.sin6_port),
                u32::from_be(decode.sin6_flowinfo),
                sin6_scope_id,
            )))
        }
        #[cfg(not(windows))]
        c::AF_UNIX => {
            if len < offsetof_sun_path {
                return Err(io::Error::INVAL);
            }
            if len == offsetof_sun_path {
                Ok(SocketAddrAny::Unix(SocketAddrUnix::new(&[][..]).unwrap()))
            } else {
                let decode = *storage.cast::<c::sockaddr_un>();

                // Trim off unused bytes from the end of `path_bytes`.
                let path_bytes = if cfg!(target_os = "freebsd") {
                    // FreeBSD sometimes sets the length to longer than the length
                    // of the NUL-terminated string. Find the NUL and truncate the
                    // string accordingly.
                    &decode.sun_path[..decode.sun_path.iter().position(|b| *b == 0).unwrap()]
                } else {
                    // Otherwise, use the provided length.
                    let provided_len = len - 1 - offsetof_sun_path;
                    if decode.sun_path[provided_len] != b'\0' as c::c_char {
                        return Err(io::Error::INVAL);
                    }
                    debug_assert_eq!(
                        ZStr::from_ptr(decode.sun_path.as_ptr().cast())
                            .to_bytes()
                            .len(),
                        provided_len
                    );
                    &decode.sun_path[..provided_len]
                };

                Ok(SocketAddrAny::Unix(
                    SocketAddrUnix::new(path_bytes.iter().map(|c| *c as u8).collect::<Vec<u8>>())
                        .unwrap(),
                ))
            }
        }
        _ => Err(io::Error::INVAL),
    }
}

pub(crate) unsafe fn read_sockaddr_os(
    storage: *const c::sockaddr_storage,
    len: usize,
) -> SocketAddrAny {
    #[cfg(not(windows))]
    let z = c::sockaddr_un {
        #[cfg(any(
            target_os = "dragonfly",
            target_os = "ios",
            target_os = "freebsd",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        sun_len: 0_u8,
        #[cfg(any(
            target_os = "dragonfly",
            target_os = "ios",
            target_os = "freebsd",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        sun_family: 0_u8,
        #[cfg(not(any(
            target_os = "dragonfly",
            target_os = "ios",
            target_os = "freebsd",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        )))]
        sun_family: 0_u16,
        #[cfg(any(
            target_os = "dragonfly",
            target_os = "ios",
            target_os = "freebsd",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        sun_path: [0; 104],
        #[cfg(not(any(
            target_os = "dragonfly",
            target_os = "ios",
            target_os = "freebsd",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        )))]
        sun_path: [0; 108],
    };
    #[cfg(not(windows))]
    let offsetof_sun_path = (as_ptr(&z.sun_path) as usize) - (as_ptr(&z) as usize);

    assert!(len >= size_of::<c::sa_family_t>());
    match read_ss_family(storage).into() {
        c::AF_INET => {
            assert!(len >= size_of::<c::sockaddr_in>());
            let decode = *storage.cast::<c::sockaddr_in>();
            SocketAddrAny::V4(SocketAddrV4::new(
                Ipv4Addr::from(u32::from_be(in_addr_s_addr(decode.sin_addr))),
                u16::from_be(decode.sin_port),
            ))
        }
        c::AF_INET6 => {
            assert!(len >= size_of::<c::sockaddr_in6>());
            let decode = *storage.cast::<c::sockaddr_in6>();
            SocketAddrAny::V6(SocketAddrV6::new(
                Ipv6Addr::from(in6_addr_s6_addr(decode.sin6_addr)),
                u16::from_be(decode.sin6_port),
                u32::from_be(decode.sin6_flowinfo),
                sockaddr_in6_sin6_scope_id(decode),
            ))
        }
        #[cfg(not(windows))]
        c::AF_UNIX => {
            assert!(len >= offsetof_sun_path);
            if len == offsetof_sun_path {
                SocketAddrAny::Unix(SocketAddrUnix::new(&[][..]).unwrap())
            } else {
                let decode = *storage.cast::<c::sockaddr_un>();
                assert_eq!(
                    decode.sun_path[len - 1 - offsetof_sun_path],
                    b'\0' as c::c_char
                );
                let path_bytes = &decode.sun_path[..len - 1 - offsetof_sun_path];

                // FreeBSD sometimes sets the length to longer than the length
                // of the NUL-terminated string. Find the NUL and truncate the
                // string accordingly.
                #[cfg(target_os = "freebsd")]
                let path_bytes = &path_bytes[..path_bytes.iter().position(|b| *b == 0).unwrap()];

                SocketAddrAny::Unix(
                    SocketAddrUnix::new(path_bytes.iter().map(|c| *c as u8).collect::<Vec<u8>>())
                        .unwrap(),
                )
            }
        }
        other => unimplemented!("{:?}", other),
    }
}
