//! no_std thread-local storage
//!
//! This module provides a level of indirection for thread-local storage, with
//! different options trading off performance and stability. See [`Mode`] for more
//! about each of these.
use core::cell::{Cell, UnsafeCell};
use core::marker::PhantomData;
use core::mem::MaybeUninit;
use core::num::NonZeroUsize;
use core::ops::Deref;
use core::sync::atomic::{self, AtomicI8, AtomicUsize};

use linux_api::signal::{rt_sigprocmask, SigProcMaskAction};
use num_enum::{IntoPrimitive, TryFromPrimitive};
use rustix::process::Pid;
use vasi_sync::atomic_tls_map::{self, AtomicTlsMap};
use vasi_sync::lazy_lock::{self, LazyLock};

use crate::mmap_box::MmapBox;

/// Modes of operation for this module.
#[derive(Debug, Eq, PartialEq, Copy, Clone, TryFromPrimitive, IntoPrimitive)]
#[repr(i8)]
pub enum Mode {
    /// The default before calling [`global_preferred_mode::set`]. While this
    /// mode is set, trying to access any instances of [`ShimTlsVar`] will
    /// panic.
    //
    // This is an explicit variant of `Mode` to facilitate storing in a global
    // atomic.
    Disabled,
    /// Delegate back to ELF native thread local storage. This is the fastest
    /// option, and simplest with respect to our own code, but is unsound.
    /// We should probably ultimately disable or remove it.
    ///
    /// In native thread local storage for ELF executables, an access to a
    /// thread-local variable (with C storage specifier `__thread`) from a
    /// dynamically shared object (like the Shadow shim) involves implicitly calling
    /// the libc function `__tls_get_addr`. That function is *not* guaranteed to be
    /// async-signal-safe (See `signal-safety(7)`), and can end up making system
    /// calls and doing memory allocation. This has caused problems with some versions of
    /// glibc (Can't find the issue #...), and recently when running managed
    /// processed compiled with asan <https://github.com/shadow/shadow/issues/2790>.
    ///
    /// SAFETY: `__tls_get_addr` in the linked version of libc must not make
    /// system calls or do anything async-signal unsafe. This basically
    /// can't be ensured, but is often true in practice.
    //
    // TODO: I *think* if we want to avoid the shim linking with libc at all,
    // we'll need to disable this mode at compile-time by removing it or making
    // it a compile-time feature.
    Native,
    /// This mode takes advantage of ELF native thread local storage, but only
    /// leverages it as a cheap-to-retrieve thread identifier. It does not call
    /// into libc or store anything directly in the native thread local storage.
    ///
    /// In particular, based on 3.4.6 and 3.4.2 of [ELF-TLS], we know that we
    /// can retrieve the "thread pointer" by loading offset zero of the `fs`
    /// register; i.e. `%fs:0`.
    ///
    /// The contents of the data pointed to by the thread pointer are an
    /// implementation detail of the compiler, linker, and libc, so we don't
    /// depend on it. However it seems reasonable to assume that this address is
    /// unique to each live thread, and doesn't change during the lifetime of a
    /// thread. Therefore we use the address as a thread-identifier, which we in
    /// turn use as key to our own allocated thread-local-storage.
    ///
    /// This mode is nearly as fast as native, but:
    /// * Assumes that if the "thread pointer" in `%fs:0` is non-NULL for a
    /// given thread, that it is stable and unique for the lifetime of that
    /// thread.  This seems like a fairly reasonable assumption, and seems to
    /// hold so far, but isn't guaranteed.
    /// * Requires that each thread using thread local storage from this module
    /// calls [`unregister_and_exit_current_thread`] before exiting, since the thread pointer
    /// may subsequently be used for another thread.
    ///
    /// [ELF-TLS]: "ELF Handling For Thread-Local Storage", by Ulrich Drepper.
    /// <https://www.akkadia.org/drepper/tls.pdf>
    ///
    /// SAFETY: Requires that each thread using this thread local storage
    /// calls [`unregister_and_exit_current_thread`] before exiting.
    NativeTlsId,
    /// This mode is similar to `NativeTlsId`, but instead of using the ELF thread
    /// pointer to identify each thread, it uses the system thread ID as retrieved by
    /// the `gettid` syscall.
    ///
    /// Unlike `NativeTlsId`, this approach doesn't rely on any assumptions about
    /// the implementation details of thread local storage in the managed process.
    /// It also *usually* still works without calling [`ThreadLocalStorage::unregister_current_thread`],
    /// but technically still requires it to guarantee soundness, since thread
    ///
    /// Unfortunately this mode is *much* slower than the others.
    ///
    /// SAFETY: Each thread using this thread local storage must call
    /// [`unregister_and_exit_current_thread`] before exiting.
    #[allow(unused)]
    Gettid,
}

/// Manipulates the process-global preferred mode of operation.
pub mod global_preferred_mode {
    use super::*;

    static PREFERRED_MODE: AtomicI8 = AtomicI8::new(Mode::Disabled as i8);

    /// Permits the mode to be set again. This allows us to more easily
    /// test each mode.
    ///
    /// # Safety
    ///
    /// There must not exist any live threads that have accessed Tls variables
    /// and not yet called [`unregister_and_exit_current_thread`].
    #[cfg(test)]
    pub unsafe fn reset_for_testing() {
        PREFERRED_MODE.store(Mode::Disabled as i8, atomic::Ordering::Relaxed);
    }

    /// Sets the *preferred* [`Mode`]. Modes [`Mode::Native`] and [`Mode::NativeTlsId`],
    /// will fall back to [`Mode::Gettid`] for any thread that doesn't appear to have
    /// configured native ELF thread local storage (i.e. whose thread pointer
    /// retrieved from `$fs:0` is `NULL`).
    ///
    /// Panics if thread local storage has already been set.
    ///
    /// # Safety
    ///
    /// See [`Mode`] for addititional requirements for each mode.
    pub unsafe fn set(mode: Mode) {
        PREFERRED_MODE
            .compare_exchange(
                Mode::Disabled.into(),
                mode.into(),
                atomic::Ordering::Relaxed,
                atomic::Ordering::Relaxed,
            )
            .expect("Thread local storage mode cannot safely be changed.");
    }

    /// Get the current mode. After calling this method, the mode can't be
    /// changed again.
    pub(super) fn get() -> Mode {
        PREFERRED_MODE
            .load(atomic::Ordering::Relaxed)
            .try_into()
            .unwrap()
    }
}

/// This needs to be big enough to store all thread-local variables for a single
/// thread. We fail at runtime if this limit is exceeded.
pub const BYTES_PER_THREAD: usize = 1024;

// Max threads for our slow TLS fallback mechanism.  We support recycling
// storage of exited threads, so this is the max *concurrent* threads per
// process.
const TLS_FALLBACK_MAX_THREADS: usize = 1000;

/// An ELF thread pointer, as specified in
/// <https://www.akkadia.org/drepper/tls.pdf)>
///
/// Guaranteed not to be zero/NULL.
///
/// Only useful for comparisons, since the contents of the pointer are an
/// implementation detail of libc and the linker.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
struct ElfThreadPointer(NonZeroUsize);

impl ElfThreadPointer {
    /// Thread pointer for the current thread, if one is set.
    pub fn current() -> Option<Self> {
        // Based on 3.4.6 and 3.4.2 of [ELF-TLS], we retrieve the "thread
        // pointer" by loading offset zero of the `fs` register; i.e. `%fs:0`.
        let fs: usize;
        unsafe { core::arch::asm!("mov {fs}, $fs:0x0", fs = out(reg) fs) };
        NonZeroUsize::new(fs).map(Self)
    }
}

// The alignment we choose here becomes the max alignment we can support for
// [`ShimTlsVar`]. 16 is enough for most types, and is e.g. the alignment of
// pointers returned by glibc's `malloc`, but we can increase as needed.
#[repr(C, align(16))]
struct ShimThreadLocalStorage {
    // Used as a backing store for instances of `ShimTlsVarStorage`. Must be
    // initialized to zero so that the first time that a given range of bytes is
    // interpreted as a `ShimTlsVarStorage`, `ShimTlsVarStorage::initd` is
    // correctly `false`.
    //
    // `MaybeUninit` because after a given range starts to be used as
    // `ShimTlsVarStorage<T>`, T may *uninitialize* some bytes e.g. due to
    // padding or its own use of `MaybeUninit`.
    bytes: UnsafeCell<[MaybeUninit<u8>; BYTES_PER_THREAD]>,
}

impl ShimThreadLocalStorage {
    /// # Safety
    ///
    /// * `alloc` must be dereferenceable and live for the lifetime of this process.
    /// * `alloc` must be zeroed the first time it is passed to this function.
    /// * `alloc` must *only* be access through this function.
    pub unsafe fn from_static_lifetime_zeroed_allocation(
        alloc: *mut ShimThreadLocalStorageAllocation,
    ) -> &'static Self {
        type Output = ShimThreadLocalStorage;
        static_assertions::assert_eq_align!(ShimThreadLocalStorageAllocation, Output);
        static_assertions::assert_eq_size!(ShimThreadLocalStorageAllocation, Output);
        unsafe { &*alloc.cast_const().cast::<Output>() }
    }

    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        Self {
            bytes: UnsafeCell::new([MaybeUninit::new(0); BYTES_PER_THREAD]),
        }
    }
}

/// This is a "proxy" type to [`ShimThreadLocalStorage`] with the same size and alignment.
/// Unlike [`ShimThreadLocalStorage`], it is exposed to C, that C code can provide
/// a "thread-local allocator" that we delegate to in [`Mode::Native`].
#[repr(C, align(16))]
#[derive(Copy, Clone)]
pub struct ShimThreadLocalStorageAllocation {
    _bytes: [u8; BYTES_PER_THREAD],
}
static_assertions::assert_eq_align!(ShimThreadLocalStorageAllocation, ShimThreadLocalStorage);
static_assertions::assert_eq_size!(ShimThreadLocalStorageAllocation, ShimThreadLocalStorage);

/// An opaque, per-thread identifier. These are only guaranteed to be unique for
/// *live* threads; in particular [`FastThreadId::ElfThreadPointer`] of a live
/// thread can have the same value as a previously seen dead thread. See
/// [`unregister_and_exit_current_thread`].
#[derive(Debug, Eq, PartialEq, Copy, Clone, Hash)]
enum FastThreadId {
    ElfThreadPointer(ElfThreadPointer),
    NativeTid(Pid),
}

impl FastThreadId {
    fn to_nonzero_usize(self) -> NonZeroUsize {
        // Kernel-space addresses have the most-significant bit set.
        // https://en.wikipedia.org/wiki/X86-64#Virtual_address_space_details
        //
        // Conversely, user-space addresses do not.
        //
        // The thread pointer value, when, set, should contain a user-space
        // address. i.e.  this bit should be unset.
        //
        // Since Pids are 32-bits, we can therefore use this bit to distinguish the
        // two "types" of thread IDs.
        const KERNEL_SPACE_BIT: usize = 1 << 63;
        match self {
            FastThreadId::ElfThreadPointer(ElfThreadPointer(fs)) => {
                assert_eq!(fs.get() & KERNEL_SPACE_BIT, 0);
                fs.get().try_into().unwrap()
            }
            FastThreadId::NativeTid(t) => {
                let pid = usize::try_from(t.as_raw_nonzero().get()).unwrap();
                (pid | KERNEL_SPACE_BIT).try_into().unwrap()
            }
        }
    }

    /// Id for the current thread.
    pub fn current() -> Self {
        #[cfg(not(miri))]
        {
            ElfThreadPointer::current()
                .map(Self::ElfThreadPointer)
                .unwrap_or_else(|| Self::NativeTid(rustix::thread::gettid()))
        }
        #[cfg(miri)]
        {
            // In miri we can't use inline assembly the get the fs register or
            // get a numeric thread ID. We have to generate synthetic IDs from
            // `std::thread::ThreadId` instead.

            use std::collections::HashMap;
            use std::sync::Mutex;
            use std::thread::ThreadId;

            static SYNTHETIC_IDS: Mutex<Option<HashMap<ThreadId, FastThreadId>>> = Mutex::new(None);
            static NEXT_ID: AtomicUsize = AtomicUsize::new(1);

            let mut synthetic_ids = SYNTHETIC_IDS.lock().unwrap();
            let mut synthetic_ids = synthetic_ids.get_or_insert_with(|| HashMap::new());
            let id = std::thread::current().id();
            *synthetic_ids
                .entry(std::thread::current().id())
                .or_insert_with(|| {
                    Self::ElfThreadPointer(ElfThreadPointer(
                        NonZeroUsize::new(
                            NEXT_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed),
                        )
                        .unwrap(),
                    ))
                })
        }
    }
}

mod global_storages {
    use super::*;

    struct StoragesMapProducer {}
    impl
        lazy_lock::Producer<
            MmapBox<AtomicTlsMap<TLS_FALLBACK_MAX_THREADS, MmapBox<ShimThreadLocalStorage>>>,
        > for StoragesMapProducer
    {
        fn initialize(
            self,
        ) -> MmapBox<AtomicTlsMap<TLS_FALLBACK_MAX_THREADS, MmapBox<ShimThreadLocalStorage>>>
        {
            MmapBox::new(AtomicTlsMap::new())
        }
    }

    // The raw byte thread local storage for each thread.
    #[repr(transparent)]
    struct StoragesType {
        // Allocate lazily via `mmap`, to avoid unnecessarily consuming
        // the memory in processes where we always use native thread local storage.
        storages: LazyLock<
            MmapBox<AtomicTlsMap<TLS_FALLBACK_MAX_THREADS, MmapBox<ShimThreadLocalStorage>>>,
            StoragesMapProducer,
        >,
    }

    impl StoragesType {
        pub const fn new() -> Self {
            Self {
                storages: LazyLock::const_new(StoragesMapProducer {}),
            }
        }
    }

    static STORAGES: StoragesType = StoragesType::new();

    /// Get the storage associated with the current thread. The bytes are initially zero.
    ///
    /// # Safety
    ///
    /// All previous threads that have called `get_or_init_current` and
    /// subsequently exited must have called `remove_current`.
    pub(super) unsafe fn get_or_init_current(
    ) -> atomic_tls_map::Ref<'static, MmapBox<ShimThreadLocalStorage>> {
        let id = FastThreadId::current();
        // SAFETY: `id` is unique to this live thread, and caller guarantees
        // any previous thread with this `id` has been removed.
        let res = unsafe {
            STORAGES
                .storages
                .deref()
                .get_or_insert_with(id.to_nonzero_usize(), || {
                    MmapBox::new(ShimThreadLocalStorage::new())
                })
        };
        res
    }

    /// Panics if there are still any live references to this thread's storage.
    ///
    /// # Safety
    ///
    /// All previous threads that have accessed this module and have since
    /// exited must have called [`unregister_and_exit_current_thread`].
    pub(super) unsafe fn remove_current() {
        if !STORAGES.storages.initd() {
            // Nothing to do. Even if another thread happens to be initializing
            // concurrently, we know the *current* thread isn't registered.
            return;
        }

        let storages = STORAGES.storages.force();

        let id = FastThreadId::current();
        // SAFETY: `id` is unique to this live thread, and caller guarantees
        // any previous thread with this `id` has been removed.
        unsafe { storages.remove(id.to_nonzero_usize()) };
    }
}

/// Unit tests using `std::thread` should use this instead of
/// `unregister_and_exit_current_thread`. The latter may have unintended side
/// effects since it would bypass any of `std::thread`'s application-level
/// cleanup.
///
/// # Safety
///
/// The current thread must not access thread local storage again before exiting.
#[cfg(test)]
pub unsafe fn unregister_current_thread_for_test() {
    // SAFETY: No code can access thread local storage in between deregistration
    // and exit.
    unsafe { global_storages::remove_current() }
}

/// Release this thread's thread local storage and exit the thread.
///
/// Should be called by every thread that accesses thread local storage.
/// This is a no-op when using native thread-local storage, but is required for
/// correctness otherwise, since thread IDs can be reused.
///
/// Panics if there are still any live references to this thread's [`ShimTlsVar`]s.
///
/// # Safety
///
/// In the case that this function somehow panics, caller must not
/// access thread local storage again from the current thread, e.g.
/// using `std::panic::catch_unwind` or a custom panic hook.
pub unsafe fn unregister_and_exit_current_thread(exit_status: i32) -> ! {
    // Block all signals, to ensure a signal handler can't run and attempt to
    // access thread local storage.
    rt_sigprocmask(
        SigProcMaskAction::SIG_BLOCK,
        &linux_api::signal::sigset_t::FULL,
        None,
    )
    .unwrap();

    // SAFETY: No code can access thread local storage in between deregistration
    // and exit.
    unsafe { global_storages::remove_current() }

    linux_api::exit::exit_raw(exit_status).unwrap();
    unreachable!()
}

enum ShimThreadLocalStorageRef {
    Native(&'static ShimThreadLocalStorage),
    Mapped(atomic_tls_map::Ref<'static, MmapBox<ShimThreadLocalStorage>>),
}

impl Deref for ShimThreadLocalStorageRef {
    type Target = ShimThreadLocalStorage;

    fn deref(&self) -> &Self::Target {
        match self {
            ShimThreadLocalStorageRef::Native(n) => n,
            ShimThreadLocalStorageRef::Mapped(m) => m,
        }
    }
}

/// Returns thread local storage for the current thread. The raw byte contents
/// are initialized to zero.
fn tls_storage() -> ShimThreadLocalStorageRef {
    match global_preferred_mode::get() {
        Mode::Disabled => panic!("Storage mode not set"),
        Mode::Native => {
            if ElfThreadPointer::current().is_some() {
                // Native (libc) TLS seems to be set up properly. Use it.
                let alloc: *mut ShimThreadLocalStorageAllocation =
                    unsafe { crate::bindings::shim_native_tls() };
                return ShimThreadLocalStorageRef::Native(unsafe {
                    ShimThreadLocalStorage::from_static_lifetime_zeroed_allocation(alloc)
                });
            }
            // else fallthrough
        }
        // Fall through
        Mode::NativeTlsId | Mode::Gettid => (),
    }
    // Use our fallback mechanism.
    ShimThreadLocalStorageRef::Mapped(unsafe { global_storages::get_or_init_current() })
}

/// One of these is placed in each thread's thread-local-storage, for each
/// `ShimTlsVar`.
///
/// It is designed to be safely *zeroable*, and for all zeroes to be the correct
/// initial state, indicating that the actual value of the variable hasn't yet
/// been initialized. This is because the first access for each of these
/// variables is from the middle of an array of zeroed bytes in
/// `ShimThreadLocalStorage`.
///
/// TODO: Consider adding a process-global identifier for each [`ShimTlsVar`],
/// and a map mapping each of those to the init state. We then wouldn't
/// need the internal `initd` flag, and maybe wouldn't need this type at all.
/// It would mean another map-lookup on every thread-local access, but it'd
/// probably ok.
struct ShimTlsVarStorage<T> {
    // Whether the var has been initialized for this thread.
    //
    // For `bool`, the bit pattern 0 is guaranteed to represent
    // `false`, and `Cell` has the same layout as its inner type. Hence,
    // interpreting 0-bytes as `Self` is sound and correctly indicates that it hasn't
    // been initialized.
    // <https://doc.rust-lang.org/std/cell/struct.Cell.html#memory-layout>
    // <https://doc.rust-lang.org/reference/types/boolean.html>
    initd: Cell<bool>,
    value: UnsafeCell<MaybeUninit<T>>,
}

impl<T> ShimTlsVarStorage<T> {
    fn get(&self) -> &T {
        assert!(self.initd.get());

        // SAFETY: We've ensured this value is initialized, and that
        // there are no exclusive references created after initialization.
        unsafe { (*self.value.get()).assume_init_ref() }
    }

    fn ensure_init(&self, initializer: impl FnOnce() -> T) {
        if !self.initd.get() {
            // Initialize the value.

            // SAFETY: This thread has exclusive access to the underlying storage.
            // This is the only place we ever construct a mutable reference to this
            // value, and we know we've never constructed a reference before, since
            // the data isn't initialized.
            let value: &mut MaybeUninit<T> = unsafe { &mut *self.value.get() };
            value.write(initializer());
            self.initd.set(true);
        }
    }
}

/// An initializer for internal use with `LazyLock`. We need an explicit type
/// instead of just a closure so that we can name the type  in `ShimTlsVar`'s
/// definition.
struct OffsetInitializer {
    align: usize,
    size: usize,
}

impl OffsetInitializer {
    pub const fn new<T>() -> Self {
        Self {
            align: core::mem::align_of::<ShimTlsVarStorage<T>>(),
            size: core::mem::size_of::<ShimTlsVarStorage<T>>(),
        }
    }
}

impl lazy_lock::Producer<usize> for OffsetInitializer {
    // Finds and assigns the next free and suitably aligned offset within
    // thread-local-storage for a value of type `T`, initialized with function
    // `F`.
    fn initialize(self) -> usize {
        // The alignment we ensure here is an offset from the base of a [`ShimThreadLocalStorage`].
        // It won't be meaningful if [`ShimThreadLocalStorage`] has a smaller alignment requirement
        // than this variable.
        assert!(self.align <= core::mem::align_of::<ShimThreadLocalStorage>());
        static NEXT_OFFSET: AtomicUsize = AtomicUsize::new(0);
        let mut next_offset_val = NEXT_OFFSET.load(atomic::Ordering::Relaxed);
        loop {
            // Create a synthetic pointer just so we can call `align_offset`
            // instead of doing the fiddly math ourselves.  This is sound, but
            // causes miri to generate a warning.  We should use
            // `core::ptr::invalid` here once stabilized to make our intent
            // explicit that yes, really, we want to make an invalid pointer
            // that we have no intention of dereferencing.
            let fake: *const u8 = next_offset_val as *const u8;
            let this_var_offset = next_offset_val + fake.align_offset(self.align);

            let next_next_offset_val = this_var_offset + self.size;
            if next_next_offset_val > BYTES_PER_THREAD {
                panic!("Exceeded hard-coded limit of {BYTES_PER_THREAD} per thread of thread local storage");
            }

            match NEXT_OFFSET.compare_exchange(
                next_offset_val,
                next_next_offset_val,
                atomic::Ordering::Relaxed,
                atomic::Ordering::Relaxed,
            ) {
                Ok(_) => return this_var_offset,
                Err(v) => {
                    // We raced with another thread. This *shouldn't* happen in
                    // the shadow shim, since only one thread is allowed to run
                    // at a time, but handle it gracefully. Update the current
                    // value of the atomic and try again.
                    next_offset_val = v;
                }
            }
        }
    }
}

/// Thread local storage for a variable of type `T`, initialized on first access
/// by each thread using a function of type `F`.
///
/// The `Drop` implementation of `T` is *not* called, e.g. when threads exit or
/// this value itself is dropped.
//
// TODO: Consider changing API to only provide a `with` method instead of
// allowing access to `'static` references. This would let us validate in
// [`unregister_and_exit_current_thread`] that no variables are currently being accessed
// and enforce that none are accessed afterwards, and potentially let us run
// `Drop` impls (though I think we'd also need an allocator for the latter).
pub struct ShimTlsVar<T, F = fn() -> T>
where
    F: Fn() -> T,
{
    // We wrap in a lazy lock to support const initialization of `Self`.
    offset: LazyLock<usize, OffsetInitializer>,
    f: F,
    _phantom: PhantomData<T>,
}
// SAFETY: Still `Sync` even if T is `!Sync`, since each thread gets its own
// instance of the value. `F` must still be `Sync`, though, since that *is*
// shared across threads.
unsafe impl<T, F> Sync for ShimTlsVar<T, F> where F: Sync + Fn() -> T {}

impl<T, F> ShimTlsVar<T, F>
where
    F: Fn() -> T,
{
    /// Create a variable that will be uniquely instantiated for each thread,
    /// initialized with `f` on first access by each thread.
    ///
    /// Typically this should go in a `static`.
    pub const fn new(f: F) -> Self {
        Self {
            offset: LazyLock::const_new(OffsetInitializer::new::<T>()),
            f,
            _phantom: PhantomData,
        }
    }

    /// Access the inner value.
    ///
    /// The returned wrapper can't be sent to or shared with other threads,
    /// since the underlying storage is invalidated when the originating thread
    /// calls [`ThreadLocalStorage::unregister_current_thread`].
    pub fn get(&self) -> TLSVarRef<T> {
        // SAFETY: This offset into TLS storage is a valid instance of
        // `ShimTlsVarStorage<T>`. We've ensured the correct size and alignment,
        // and the backing bytes have been initialized to 0.
        unsafe { TLSVarRef::new(tls_storage(), *self.offset.force(), &self.f) }
    }
}

pub struct TLSVarRef<'a, T> {
    storage: ShimThreadLocalStorageRef,
    offset: usize,

    // Force to be !Sync and !Send.
    _phantom: core::marker::PhantomData<*mut T>,
    // Defensively bind to lifetime of `TLSVar`.  Currently not technically
    // required, since we don't "deallocate" the backing storage of a `TLSVar`
    // that's uninitialized, and a no-op in "standard" usage since `TLSVar`s
    // generally have a `'static` lifetime, but let's avoid a potential
    // surprising lifetime extension that we shouldn't need.
    _phantom_lifetime: core::marker::PhantomData<&'a ()>,
}
// Double check `!Send` and `!Sync`.
static_assertions::assert_not_impl_any!(TLSVarRef<'static, ()>: Send, Sync);

impl<'a, T> TLSVarRef<'a, T> {
    /// # Safety
    ///
    /// There must be an initialized instance of `ShimTlsVarStorage<T> at the
    /// address of `&storage.bytes[offset]`.
    unsafe fn new(
        storage: ShimThreadLocalStorageRef,
        offset: usize,
        initializer: impl FnOnce() -> T,
    ) -> Self {
        let this = Self {
            storage,
            offset,
            _phantom: PhantomData,
            _phantom_lifetime: PhantomData,
        };
        this.var_storage().ensure_init(initializer);
        this
    }

    fn var_storage(&self) -> &ShimTlsVarStorage<T> {
        // SAFETY: We ensured `offset` is in bounds at construction time.
        let this_var_bytes: *mut u8 = {
            let storage: *mut [MaybeUninit<u8>; BYTES_PER_THREAD] = self.storage.bytes.get();
            let storage: *mut u8 = storage.cast();
            unsafe { storage.add(self.offset) }
        };

        let this_var: *const ShimTlsVarStorage<T> = this_var_bytes as *const ShimTlsVarStorage<T>;
        assert_eq!(
            this_var.align_offset(core::mem::align_of::<ShimTlsVarStorage<T>>()),
            0
        );
        // SAFETY: The TLS bytes for each thread are initialized to 0, and
        // all-zeroes is a valid value of `ShimTlsVarStorage<T>`.
        //
        // We've ensure proper alignment when calculating the offset,
        // and verified in the assertion just above.
        let this_var: &ShimTlsVarStorage<T> = unsafe { &*this_var };
        this_var
    }
}

impl<'a, T> Deref for TLSVarRef<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.var_storage().get()
    }
}

#[cfg(test)]
mod test {
    use core::cell::RefCell;
    use core::sync::atomic::AtomicI16;

    use vasi_sync::sync::AtomicI32;

    use super::*;

    fn test_each_mode(f: impl Fn()) {
        #[cfg(not(miri))]
        let modes = [Mode::Native, Mode::NativeTlsId, Mode::Gettid];

        // miri can't use Native
        #[cfg(miri)]
        let modes = [Mode::NativeTlsId, Mode::Gettid];

        // Run in each mode. We need to serialize here since the mode is global,
        // and can't be safely changed while TLS from another mode is in use.
        static MODE_MUTEX: std::sync::Mutex<()> = std::sync::Mutex::new(());
        for mode in &modes {
            let _lock = MODE_MUTEX.lock().unwrap();
            unsafe { global_preferred_mode::set(*mode) };
            f();
            unsafe { global_preferred_mode::reset_for_testing() };
        }
    }

    #[test_log::test]
    fn test_minimal() {
        static MY_VAR: ShimTlsVar<u32> = ShimTlsVar::new(|| 0);
        test_each_mode(|| {
            assert_eq!(*MY_VAR.get(), 0);
            unsafe { unregister_current_thread_for_test() };
        });
    }

    #[test_log::test]
    fn test_single_thread_mutate() {
        static MY_VAR: ShimTlsVar<RefCell<u32>> = ShimTlsVar::new(|| RefCell::new(0));
        test_each_mode(|| {
            assert_eq!(*MY_VAR.get().borrow(), 0);
            *MY_VAR.get().borrow_mut() = 42;
            assert_eq!(*MY_VAR.get().borrow(), 42);
            unsafe { unregister_current_thread_for_test() };
        });
    }

    #[test_log::test]
    fn test_multithread_mutate() {
        static MY_VAR: ShimTlsVar<RefCell<i32>> = ShimTlsVar::new(|| RefCell::new(0));
        test_each_mode(|| {
            let threads = (0..10).map(|i| {
                std::thread::spawn(move || {
                    assert_eq!(*MY_VAR.get().borrow(), 0);
                    *MY_VAR.get().borrow_mut() = i;
                    assert_eq!(*MY_VAR.get().borrow(), i);
                    unsafe { unregister_current_thread_for_test() };
                })
            });
            for t in threads {
                t.join().unwrap();
            }
            unsafe { unregister_current_thread_for_test() };
        });
    }

    #[test_log::test]
    fn test_multithread_mutate_small_alignment() {
        // Normally it'd make more sense to use cheaper interior mutability
        // such as `RefCell` or `Cell`, but here we want to ensure the alignment is 1
        // to validate that we don't overlap storage.
        static MY_VAR: ShimTlsVar<AtomicI8> = ShimTlsVar::new(|| AtomicI8::new(0));
        test_each_mode(|| {
            let threads = (0..10).map(|i| {
                std::thread::spawn(move || {
                    assert_eq!(MY_VAR.get().load(atomic::Ordering::Relaxed), 0);
                    MY_VAR.get().store(i, atomic::Ordering::Relaxed);
                    assert_eq!(MY_VAR.get().load(atomic::Ordering::Relaxed), i);
                    unsafe { unregister_current_thread_for_test() };
                })
            });
            for t in threads {
                t.join().unwrap();
            }
            unsafe { unregister_current_thread_for_test() };
        });
    }

    #[test_log::test]
    fn test_multithread_mutate_mixed_alignments() {
        static MY_I8: ShimTlsVar<AtomicI8> = ShimTlsVar::new(|| AtomicI8::new(0));
        static MY_I16: ShimTlsVar<AtomicI16> = ShimTlsVar::new(|| AtomicI16::new(0));
        static MY_I32: ShimTlsVar<AtomicI32> = ShimTlsVar::new(|| AtomicI32::new(0));
        test_each_mode(|| {
            let threads = (0..10).map(|i| {
                std::thread::spawn(move || {
                    // Access out of alignment order
                    assert_eq!(MY_I8.get().load(atomic::Ordering::Relaxed), 0);
                    assert_eq!(MY_I32.get().load(atomic::Ordering::Relaxed), 0);
                    assert_eq!(MY_I16.get().load(atomic::Ordering::Relaxed), 0);

                    // Order shouldn't matter here, but change it from above anyway.
                    MY_I32.get().store(i as i32, atomic::Ordering::Relaxed);
                    MY_I8.get().store(i as i8, atomic::Ordering::Relaxed);
                    MY_I16.get().store(i as i16, atomic::Ordering::Relaxed);

                    assert_eq!(MY_I16.get().load(atomic::Ordering::Relaxed), i as i16);
                    assert_eq!(MY_I32.get().load(atomic::Ordering::Relaxed), i as i32);
                    assert_eq!(MY_I8.get().load(atomic::Ordering::Relaxed), i as i8);
                    unsafe { unregister_current_thread_for_test() };
                })
            });
            for t in threads {
                t.join().unwrap();
            }
            unsafe { unregister_current_thread_for_test() };
        });
    }
}

mod export {
    use super::*;

    /// Should be used to exit every thread that accesses thread local storage.
    ///
    /// This is a no-op when using native thread-local storage, but lets us reclaim memory
    /// otherwise, and is necessary for correctness when [`Mode::NativeTlsId`] is enabled.
    ///
    /// # Safety
    ///
    /// In the case that this function somehow panics, caller must not
    /// access thread local storage again from the current thread, e.g.
    /// using `std::panic::catch_unwind` or a custom panic hook.
    #[no_mangle]
    pub extern "C" fn shim_tls_unregister_and_exit_current_thread(status: i32) {
        unsafe { unregister_and_exit_current_thread(status) }
    }
}
