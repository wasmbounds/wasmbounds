//! memfd support: creation of backing images for modules, and logic
//! to support mapping these backing images into memory.

use crate::InstantiationError;
use anyhow::Result;
use libc::c_void;
use memfd::{Memfd, MemfdOptions};
use rustix::fd::AsRawFd;
use std::io::Write;
use std::sync::Arc;
use std::{convert::TryFrom, ops::Range};
use wasmtime_environ::{DefinedMemoryIndex, InitMemory, Module, PrimaryMap};

/// MemFDs containing backing images for certain memories in a module.
///
/// This is meant to be built once, when a module is first
/// loaded/constructed, and then used many times for instantiation.
pub struct ModuleMemFds {
    memories: PrimaryMap<DefinedMemoryIndex, Option<Arc<MemoryMemFd>>>,
}

const MAX_MEMFD_IMAGE_SIZE: usize = 1024 * 1024 * 1024; // limit to 1GiB.

impl ModuleMemFds {
    pub(crate) fn get_memory_image(
        &self,
        defined_index: DefinedMemoryIndex,
    ) -> Option<&Arc<MemoryMemFd>> {
        self.memories[defined_index].as_ref()
    }
}

/// One backing image for one memory.
#[derive(Debug)]
pub struct MemoryMemFd {
    /// The actual memfd image: an anonymous file in memory which we
    /// use as the backing content for a copy-on-write (CoW) mapping
    /// in the memory region.
    pub fd: Memfd,
    /// Length of image. Note that initial memory size may be larger;
    /// leading and trailing zeroes are truncated (handled by
    /// anonymous backing memfd).
    ///
    /// Must be a multiple of the system page size.
    pub len: usize,
    /// Image starts this many bytes into heap space. Note that the
    /// memfd's offsets are always equal to the heap offsets, so we
    /// map at an offset into the fd as well. (This simplifies
    /// construction.)
    ///
    /// Must be a multiple of the system page size.
    pub offset: usize,
}

fn create_memfd() -> Result<Memfd> {
    // Create the memfd. It needs a name, but the
    // documentation for `memfd_create()` says that names can
    // be duplicated with no issues.
    MemfdOptions::new()
        .allow_sealing(true)
        .create("wasm-memory-image")
        .map_err(|e| e.into())
}

impl ModuleMemFds {
    /// Create a new `ModuleMemFds` for the given module. This can be
    /// passed in as part of a `InstanceAllocationRequest` to speed up
    /// instantiation and execution by using memfd-backed memories.
    pub fn new(module: &Module, wasm_data: &[u8]) -> Result<Option<Arc<ModuleMemFds>>> {
        let page_size = region::page::size() as u64;
        let page_align = |x: u64| x & !(page_size - 1);
        let page_align_up = |x: u64| page_align(x + page_size - 1);

        // First build up an in-memory image for each memory. This in-memory
        // representation is discarded if the memory initializers aren't "of
        // the right shape" where the desired shape is:
        //
        // * Only initializers for defined memories.
        // * Only initializers with static offsets (no globals).
        // * Only in-bound initializers.
        //
        // The `init_memory` method of `MemoryInitialization` is used here to
        // do most of the validation for us, and otherwise the data chunks are
        // collected into the `images` array here.
        let mut images: PrimaryMap<DefinedMemoryIndex, Vec<u8>> = PrimaryMap::default();
        let num_defined_memories = module.memory_plans.len() - module.num_imported_memories;
        for _ in 0..num_defined_memories {
            images.push(Vec::new());
        }
        let ok = module.memory_initialization.init_memory(
            InitMemory::CompileTime(module),
            &mut |memory, offset, data_range| {
                // Memfd-based initialization of an imported memory isn't
                // implemented right now, although might perhaps be
                // theoretically possible for statically-known-in-bounds
                // segments with page-aligned portions.
                let memory = match module.defined_memory_index(memory) {
                    Some(index) => index,
                    None => return false,
                };

                // Splat the `data_range` into the `image` for this memory,
                // updating it as necessary with 0s for holes and such.
                let image = &mut images[memory];
                let data = &wasm_data[data_range.start as usize..data_range.end as usize];
                let offset = offset as usize;
                let new_image_len = offset + data.len();
                if image.len() < new_image_len {
                    if new_image_len > MAX_MEMFD_IMAGE_SIZE {
                        return false;
                    }
                    image.resize(new_image_len, 0);
                }
                image[offset..][..data.len()].copy_from_slice(data);
                true
            },
        );

        // If any initializer wasn't applicable then we skip memfds entirely.
        if !ok {
            return Ok(None);
        }

        // With an in-memory representation of all memory images a `memfd` is
        // now created and the data is pushed into the memfd. Note that the
        // memfd representation will trim leading and trailing pages of zeros
        // to store as little data as possible in the memfd. This is not only a
        // performance improvement in the sense of "copy less data to the
        // kernel" but it's also more performant to fault in zeros from
        // anonymous-backed pages instead of memfd-backed pages-of-zeros (as
        // the kernel knows anonymous mappings are always zero and has a cache
        // of zero'd pages).
        let mut memories = PrimaryMap::default();
        for (defined_memory, image) in images {
            // Find the first nonzero byte, and if all the bytes are zero then
            // we can skip the memfd for this memory since there's no
            // meaningful initialization.
            let nonzero_start = match image.iter().position(|b| *b != 0) {
                Some(i) => i as u64,
                None => {
                    memories.push(None);
                    continue;
                }
            };

            // Find the last nonzero byte, which must exist at this point since
            // we found one going forward. Add one to find the index of the
            // last zero, which may also be the length of the image.
            let nonzero_end = image.iter().rposition(|b| *b != 0).unwrap() as u64 + 1;

            // The offset of this image must be OS-page-aligned since we'll be
            // starting the mmap at an aligned address. Align down the start
            // index to the first index that's page aligned.
            let offset = page_align(nonzero_start);

            // The length of the image must also be page aligned and may reach
            // beyond the end of the `image` array we have already. Take the
            // length of the nonzero portion and then align it up to the page size.
            let len = page_align_up(nonzero_end - offset);

            // Write the nonzero data to the memfd and then use `set_len` to
            // ensure that the length of the memfd is page-aligned where the gap
            // at the end, if any, is filled with zeros.
            let memfd = create_memfd()?;
            memfd
                .as_file()
                .write_all(&image[offset as usize..nonzero_end as usize])?;
            memfd.as_file().set_len(len)?;

            // Seal the memfd's data and length.
            //
            // This is a defense-in-depth security mitigation. The
            // memfd will serve as the starting point for the heap of
            // every instance of this module. If anything were to
            // write to this, it could affect every execution. The
            // memfd object itself is owned by the machinery here and
            // not exposed elsewhere, but it is still an ambient open
            // file descriptor at the syscall level, so some other
            // vulnerability that allowed writes to arbitrary fds
            // could modify it. Or we could have some issue with the
            // way that we map it into each instance. To be
            // extra-super-sure that it never changes, and because
            // this costs very little, we use the kernel's "seal" API
            // to make the memfd image permanently read-only.
            memfd.add_seal(memfd::FileSeal::SealGrow)?;
            memfd.add_seal(memfd::FileSeal::SealShrink)?;
            memfd.add_seal(memfd::FileSeal::SealWrite)?;
            memfd.add_seal(memfd::FileSeal::SealSeal)?;

            assert_eq!(offset % page_size, 0);
            assert_eq!(len % page_size, 0);

            let idx = memories.push(Some(Arc::new(MemoryMemFd {
                fd: memfd,
                offset: usize::try_from(offset).unwrap(),
                len: usize::try_from(len).unwrap(),
            })));
            assert_eq!(idx, defined_memory);
        }

        Ok(Some(Arc::new(ModuleMemFds { memories })))
    }
}

/// A single slot handled by the memfd instance-heap mechanism.
///
/// The mmap scheme is:
///
/// base ==> (points here)
/// - (image.offset bytes)   anonymous zero memory, pre-image
/// - (image.len bytes)      CoW mapping of memfd heap image
/// - (up to static_size)    anonymous zero memory, post-image
///
/// The ordering of mmaps to set this up is:
///
/// - once, when pooling allocator is created:
///   - one large mmap to create 8GiB * instances * memories slots
///
/// - per instantiation of new image in a slot:
///   - mmap of anonymous zero memory, from 0 to max heap size
///     (static_size)
///   - mmap of CoW'd memfd image, from `image.offset` to
///     `image.offset + image.len`. This overwrites part of the
///     anonymous zero memory, potentially splitting it into a pre-
///     and post-region.
///   - mprotect(PROT_NONE) on the part of the heap beyond the initial
///     heap size; we re-mprotect it with R+W bits when the heap is
///     grown.
#[derive(Debug)]
pub struct MemFdSlot {
    /// The base of the actual heap memory. Bytes at this address are
    /// what is seen by the Wasm guest code.
    base: usize,
    /// The maximum static memory size, plus post-guard.
    static_size: usize,
    /// The memfd image that backs this memory. May be `None`, in
    /// which case the memory is all zeroes.
    pub(crate) image: Option<Arc<MemoryMemFd>>,
    /// The initial heap size.
    initial_size: usize,
    /// The current heap size. All memory above `base + cur_size`
    /// should be PROT_NONE (mapped inaccessible).
    cur_size: usize,
    /// Whether this slot may have "dirty" pages (pages written by an
    /// instantiation). Set by `instantiate()` and cleared by
    /// `clear_and_remain_ready()`, and used in assertions to ensure
    /// those methods are called properly.
    ///
    /// Invariant: if !dirty, then this memory slot contains a clean
    /// CoW mapping of `image`, if `Some(..)`, and anonymous-zero
    /// memory beyond the image up to `static_size`. The addresses
    /// from offset 0 to `initial_size` are accessible R+W and the
    /// rest of the slot is inaccessible.
    dirty: bool,
    /// Whether this MemFdSlot is responsible for mapping anonymous
    /// memory (to hold the reservation while overwriting mappings
    /// specific to this slot) in place when it is dropped. Default
    /// on, unless the caller knows what they are doing.
    clear_on_drop: bool,
}

impl MemFdSlot {
    /// Create a new MemFdSlot. Assumes that there is an anonymous
    /// mmap backing in the given range to start.
    pub(crate) fn create(base_addr: *mut c_void, initial_size: usize, static_size: usize) -> Self {
        let base = base_addr as usize;
        MemFdSlot {
            base,
            static_size,
            initial_size,
            cur_size: initial_size,
            image: None,
            dirty: false,
            clear_on_drop: true,
        }
    }

    /// Inform the MemFdSlot that it should *not* clear the underlying
    /// address space when dropped. This should be used only when the
    /// caller will clear or reuse the address space in some other
    /// way.
    pub(crate) fn no_clear_on_drop(&mut self) {
        self.clear_on_drop = false;
    }

    pub(crate) fn set_heap_limit(&mut self, size_bytes: usize) -> Result<()> {
        // mprotect the relevant region.
        self.set_protection(
            self.cur_size..size_bytes,
            rustix::io::MprotectFlags::READ | rustix::io::MprotectFlags::WRITE,
        )?;
        self.cur_size = size_bytes;

        Ok(())
    }

    pub(crate) fn instantiate(
        &mut self,
        initial_size_bytes: usize,
        maybe_image: Option<&Arc<MemoryMemFd>>,
    ) -> Result<(), InstantiationError> {
        assert!(!self.dirty);
        assert_eq!(self.cur_size, self.initial_size);

        // Fast-path: previously instantiated with the same image, or
        // no image but the same initial size, so the mappings are
        // already correct; there is no need to mmap anything. Given
        // that we asserted not-dirty above, any dirty pages will have
        // already been thrown away by madvise() during the previous
        // termination. The `clear_and_remain_ready()` path also
        // mprotects memory above the initial heap size back to
        // PROT_NONE, so we don't need to do that here.
        if (self.image.is_none()
            && maybe_image.is_none()
            && self.initial_size == initial_size_bytes)
            || (self.image.is_some()
                && maybe_image.is_some()
                && self.image.as_ref().unwrap().fd.as_file().as_raw_fd()
                    == maybe_image.as_ref().unwrap().fd.as_file().as_raw_fd())
        {
            self.dirty = true;
            return Ok(());
        }
        // Otherwise, we need to transition from the previous state to the
        // state now requested. An attempt is made here to minimize syscalls to
        // the kernel to ideally reduce the overhead of this as it's fairly
        // performance sensitive with memories. Note that the "previous state"
        // is assumed to be post-initialization (e.g. after an mmap on-demand
        // memory was created) or after `clear_and_remain_ready` was called
        // which notably means that `madvise` has reset all the memory back to
        // its original state.
        //
        // Security/audit note: we map all of these MAP_PRIVATE, so
        // all instance data is local to the mapping, not propagated
        // to the backing fd. We throw away this CoW overlay with
        // madvise() below, from base up to static_size (which is the
        // whole slot) when terminating the instance.

        if self.image.is_some() {
            // In this case the state of memory at this time is that the memory
            // from `0..self.initial_size` is reset back to its original state,
            // but this memory contians a CoW image that is different from the
            // one specified here. To reset state we first reset the mapping
            // of memory to anonymous PROT_NONE memory, and then afterwards the
            // heap is made visible with an mprotect.
            self.reset_with_anon_memory()
                .map_err(|e| InstantiationError::Resource(e.into()))?;
            self.set_protection(
                0..initial_size_bytes,
                rustix::io::MprotectFlags::READ | rustix::io::MprotectFlags::WRITE,
            )
            .map_err(|e| InstantiationError::Resource(e.into()))?;
        } else if initial_size_bytes < self.initial_size {
            // In this case the previous module had now CoW image which means
            // that the memory at `0..self.initial_size` is all zeros and
            // read-write, everything afterwards being PROT_NONE.
            //
            // Our requested heap size is smaller than the previous heap size
            // so all that's needed now is to shrink the heap further to
            // `initial_size_bytes`.
            //
            // So we come in with:
            // - anon-zero memory, R+W,  [0, self.initial_size)
            // - anon-zero memory, none, [self.initial_size, self.static_size)
            // and we want:
            // - anon-zero memory, R+W,  [0, initial_size_bytes)
            // - anon-zero memory, none, [initial_size_bytes, self.static_size)
            //
            // so given initial_size_bytes < self.initial_size we
            // mprotect(NONE) the zone from the first to the second.
            self.set_protection(
                initial_size_bytes..self.initial_size,
                rustix::io::MprotectFlags::empty(),
            )
            .map_err(|e| InstantiationError::Resource(e.into()))?;
        } else if initial_size_bytes > self.initial_size {
            // In this case, like the previous one, the previous module had no
            // CoW image but had a smaller heap than desired for this module.
            // That means that here `mprotect` is used to make the new pages
            // read/write, and since they're all reset from before they'll be
            // made visible as zeros.
            self.set_protection(
                self.initial_size..initial_size_bytes,
                rustix::io::MprotectFlags::READ | rustix::io::MprotectFlags::WRITE,
            )
            .map_err(|e| InstantiationError::Resource(e.into()))?;
        } else {
            // The final case here is that the previous module has no CoW image
            // so the previous heap is all zeros. The previous heap is the exact
            // same size as the requested heap, so no syscalls are needed to do
            // anything else.
        }

        // The memory image, at this point, should have `initial_size_bytes` of
        // zeros starting at `self.base` followed by inaccessible memory to
        // `self.static_size`. Update sizing fields to reflect this.
        self.initial_size = initial_size_bytes;
        self.cur_size = initial_size_bytes;

        // The initial memory image, if given. If not, we just get a
        // memory filled with zeroes.
        if let Some(image) = maybe_image {
            assert!(image.offset.checked_add(image.len).unwrap() <= initial_size_bytes);
            if image.len > 0 {
                unsafe {
                    let ptr = rustix::io::mmap(
                        (self.base + image.offset) as *mut c_void,
                        image.len,
                        rustix::io::ProtFlags::READ | rustix::io::ProtFlags::WRITE,
                        rustix::io::MapFlags::PRIVATE | rustix::io::MapFlags::FIXED,
                        image.fd.as_file(),
                        0,
                    )
                    .map_err(|e| InstantiationError::Resource(e.into()))?;
                    assert_eq!(ptr as usize, self.base + image.offset);
                }
            }
        }

        self.image = maybe_image.cloned();
        self.dirty = true;

        Ok(())
    }

    #[allow(dead_code)] // ignore warnings as this is only used in some cfgs
    pub(crate) fn clear_and_remain_ready(&mut self) -> Result<()> {
        assert!(self.dirty);
        // madvise the image range. This will throw away dirty pages,
        // which are CoW-private pages on top of the initial heap
        // image memfd.
        unsafe {
            rustix::io::madvise(
                self.base as *mut c_void,
                self.cur_size,
                rustix::io::Advice::LinuxDontNeed,
            )?;
        }

        // mprotect the initial heap region beyond the initial heap size back to PROT_NONE.
        self.set_protection(
            self.initial_size..self.cur_size,
            rustix::io::MprotectFlags::empty(),
        )?;
        self.cur_size = self.initial_size;
        self.dirty = false;
        Ok(())
    }

    fn set_protection(&self, range: Range<usize>, flags: rustix::io::MprotectFlags) -> Result<()> {
        assert!(range.start <= range.end);
        assert!(range.end <= self.static_size);
        let mprotect_start = self.base.checked_add(range.start).unwrap();
        if range.len() > 0 {
            unsafe {
                rustix::io::mprotect(mprotect_start as *mut _, range.len(), flags)?;
            }
        }

        Ok(())
    }

    pub(crate) fn has_image(&self) -> bool {
        self.image.is_some()
    }

    #[allow(dead_code)] // ignore warnings as this is only used in some cfgs
    pub(crate) fn is_dirty(&self) -> bool {
        self.dirty
    }

    /// Map anonymous zeroed memory across the whole slot,
    /// inaccessible. Used both during instantiate and during drop.
    fn reset_with_anon_memory(&self) -> Result<()> {
        unsafe {
            let ptr = rustix::io::mmap_anonymous(
                self.base as *mut c_void,
                self.static_size,
                rustix::io::ProtFlags::empty(),
                rustix::io::MapFlags::PRIVATE | rustix::io::MapFlags::FIXED,
            )?;
            assert_eq!(ptr as usize, self.base);
        }
        Ok(())
    }
}

impl Drop for MemFdSlot {
    fn drop(&mut self) {
        // The MemFdSlot may be dropped if there is an error during
        // instantiation: for example, if a memory-growth limiter
        // disallows a guest from having a memory of a certain size,
        // after we've already initialized the MemFdSlot.
        //
        // We need to return this region of the large pool mmap to a
        // safe state (with no module-specific mappings). The
        // MemFdSlot will not be returned to the MemoryPool, so a new
        // MemFdSlot will be created and overwrite the mappings anyway
        // on the slot's next use; but for safety and to avoid
        // resource leaks it's better not to have stale mappings to a
        // possibly-otherwise-dead module's image.
        //
        // To "wipe the slate clean", let's do a mmap of anonymous
        // memory over the whole region, with PROT_NONE. Note that we
        // *can't* simply munmap, because that leaves a hole in the
        // middle of the pooling allocator's big memory area that some
        // other random mmap may swoop in and take, to be trampled
        // over by the next MemFdSlot later.
        //
        // Since we're in drop(), we can't sanely return an error if
        // this mmap fails. Let's ignore the failure if so; the next
        // MemFdSlot to be created for this slot will try to overwrite
        // the existing stale mappings, and return a failure properly
        // if we still cannot map new memory.
        //
        // The exception to all of this is if the `unmap_on_drop` flag
        // (which is set by default) is false. If so, the owner of
        // this MemFdSlot has indicated that it will clean up in some
        // other way.
        if self.clear_on_drop {
            let _ = self.reset_with_anon_memory();
        }
    }
}

#[cfg(test)]
mod test {
    use std::sync::Arc;

    use super::create_memfd;
    use super::MemFdSlot;
    use super::MemoryMemFd;
    use crate::mmap::Mmap;
    use anyhow::Result;
    use std::io::Write;

    fn create_memfd_with_data(offset: usize, data: &[u8]) -> Result<MemoryMemFd> {
        // Offset must be page-aligned.
        let page_size = region::page::size();
        assert_eq!(offset & (page_size - 1), 0);
        let memfd = create_memfd()?;
        memfd.as_file().write_all(data)?;

        // The image length is rounded up to the nearest page size
        let image_len = (data.len() + page_size - 1) & !(page_size - 1);
        memfd.as_file().set_len(image_len as u64)?;

        Ok(MemoryMemFd {
            fd: memfd,
            len: image_len,
            offset,
        })
    }

    #[test]
    fn instantiate_no_image() {
        // 4 MiB mmap'd area, not accessible
        let mut mmap = Mmap::accessible_reserved(0, 4 << 20).unwrap();
        // Create a MemFdSlot on top of it
        let mut memfd = MemFdSlot::create(mmap.as_mut_ptr() as *mut _, 0, 4 << 20);
        memfd.no_clear_on_drop();
        assert!(!memfd.is_dirty());
        // instantiate with 64 KiB initial size
        memfd.instantiate(64 << 10, None).unwrap();
        assert!(memfd.is_dirty());
        // We should be able to access this 64 KiB (try both ends) and
        // it should consist of zeroes.
        let slice = mmap.as_mut_slice();
        assert_eq!(0, slice[0]);
        assert_eq!(0, slice[65535]);
        slice[1024] = 42;
        assert_eq!(42, slice[1024]);
        // grow the heap
        memfd.set_heap_limit(128 << 10).unwrap();
        let slice = mmap.as_slice();
        assert_eq!(42, slice[1024]);
        assert_eq!(0, slice[131071]);
        // instantiate again; we should see zeroes, even as the
        // reuse-anon-mmap-opt kicks in
        memfd.clear_and_remain_ready().unwrap();
        assert!(!memfd.is_dirty());
        memfd.instantiate(64 << 10, None).unwrap();
        let slice = mmap.as_slice();
        assert_eq!(0, slice[1024]);
    }

    #[test]
    fn instantiate_image() {
        // 4 MiB mmap'd area, not accessible
        let mut mmap = Mmap::accessible_reserved(0, 4 << 20).unwrap();
        // Create a MemFdSlot on top of it
        let mut memfd = MemFdSlot::create(mmap.as_mut_ptr() as *mut _, 0, 4 << 20);
        memfd.no_clear_on_drop();
        // Create an image with some data.
        let image = Arc::new(create_memfd_with_data(4096, &[1, 2, 3, 4]).unwrap());
        // Instantiate with this image
        memfd.instantiate(64 << 10, Some(&image)).unwrap();
        assert!(memfd.has_image());
        let slice = mmap.as_mut_slice();
        assert_eq!(&[1, 2, 3, 4], &slice[4096..4100]);
        slice[4096] = 5;
        // Clear and re-instantiate same image
        memfd.clear_and_remain_ready().unwrap();
        memfd.instantiate(64 << 10, Some(&image)).unwrap();
        let slice = mmap.as_slice();
        // Should not see mutation from above
        assert_eq!(&[1, 2, 3, 4], &slice[4096..4100]);
        // Clear and re-instantiate no image
        memfd.clear_and_remain_ready().unwrap();
        memfd.instantiate(64 << 10, None).unwrap();
        assert!(!memfd.has_image());
        let slice = mmap.as_slice();
        assert_eq!(&[0, 0, 0, 0], &slice[4096..4100]);
        // Clear and re-instantiate image again
        memfd.clear_and_remain_ready().unwrap();
        memfd.instantiate(64 << 10, Some(&image)).unwrap();
        let slice = mmap.as_slice();
        assert_eq!(&[1, 2, 3, 4], &slice[4096..4100]);
        // Create another image with different data.
        let image2 = Arc::new(create_memfd_with_data(4096, &[10, 11, 12, 13]).unwrap());
        memfd.clear_and_remain_ready().unwrap();
        memfd.instantiate(128 << 10, Some(&image2)).unwrap();
        let slice = mmap.as_slice();
        assert_eq!(&[10, 11, 12, 13], &slice[4096..4100]);
        // Instantiate the original image again; we should notice it's
        // a different image and not reuse the mappings.
        memfd.clear_and_remain_ready().unwrap();
        memfd.instantiate(64 << 10, Some(&image)).unwrap();
        let slice = mmap.as_slice();
        assert_eq!(&[1, 2, 3, 4], &slice[4096..4100]);
    }
}
