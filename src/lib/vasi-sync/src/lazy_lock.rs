use core::marker::PhantomData;
use core::mem::MaybeUninit;

use num_enum::{IntoPrimitive, TryFromPrimitive};

use crate::sync::atomic;
use crate::sync::{self, ConstPtr, UnsafeCell};

/// For use with `LazyLock`.
///
/// This trait is implemented for `FnOnce` closures, so most users can just use
/// that directly.  However in case they need to name a type in the `LazyLock`,
/// they can create a type that implements this trait instead.
pub trait Producer<T> {
    fn initialize(self) -> T;
}

impl<F, T> Producer<T> for F
where
    F: FnOnce() -> T,
{
    fn initialize(self) -> T {
        (self)()
    }
}

#[derive(TryFromPrimitive, IntoPrimitive, Eq, PartialEq, Debug)]
#[repr(u32)]
enum InitState {
    Uninitd,
    Initd,
    // A thread is currently initializing, and there are no threads
    // asleep on the futex.
    Initializing,
    // A thread is currently initializing, and there *are* threads asleep on the
    // futex. We use this extra state to avoid doing a `futex_wake` when there
    // are no threads asleep on the futex.
    InitializingWithSleepers,
}

/// Analogous to `std::sync::LazyLock`, but works on stable Rust, is no_std,
/// only makes direct syscalls, etc.
//
// TODO: implement or derive `VirtualAddressSpaceIndependent`. The Derive
// macro currently doesn't work, because the default for `Init` is *not*
// `VirtualAddressSpaceIndependent`. We could implement the trait manually,
// but this is error-prone, and we don't currently need it.
// // #[cfg_attr(not(loom), derive(VirtualAddressSpaceIndependent))]
//
// If we *do* make this `VirtualAddressSpaceIndependent`, consider also making
// it `repr(C)`. Left as `repr(rust)` for now to let the compiler choose the
// most efficient field ordering based on the alignments of `T` and `Init`.
pub struct LazyLock<T, Init = fn() -> T> {
    value: UnsafeCell<MaybeUninit<T>>,
    initializer: UnsafeCell<Option<Init>>,
    init_state: atomic::AtomicU32,
}
// `T` must be `Sync`, since it will be shared across threads once initialized.
// `Init` only needs to be `Send` since it'll only ever be accessed by the
// single thread that performs the initialization.
unsafe impl<T, Init> Sync for LazyLock<T, Init>
where
    T: Sync,
    Init: Send,
{
}
unsafe impl<T, Init> Send for LazyLock<T, Init>
where
    T: Send,
    Init: Send,
{
}

impl<T, Init> LazyLock<T, Init>
where
    Init: Producer<T>,
{
    // TODO: merge with `new` when loom's `UnsafeCell` supports const init.
    #[cfg(not(loom))]
    pub const fn const_new(initializer: Init) -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::uninit()),
            init_state: atomic::AtomicU32::new(InitState::Uninitd as u32),
            initializer: UnsafeCell::new(Some(initializer)),
        }
    }

    pub fn new(initializer: Init) -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::uninit()),
            init_state: atomic::AtomicU32::new(InitState::Uninitd.into()),
            initializer: UnsafeCell::new(Some(initializer)),
        }
    }

    fn init(&self, mut state: InitState) {
        loop {
            match state {
                InitState::Uninitd => {
                    match self.init_state.compare_exchange(
                        InitState::Uninitd.into(),
                        InitState::Initializing.into(),
                        atomic::Ordering::Relaxed,
                        // (Potentially) pairs with `Release` in `Initializing` case.
                        atomic::Ordering::Acquire,
                    ) {
                        Ok(_) => {
                            // We have the initialization lock.
                            self.value.get_mut().with(|p: *mut MaybeUninit<T>| unsafe {
                                let initializer =
                                    self.initializer.get_mut().deref().take().unwrap();
                                (*p).write(initializer.initialize());
                            });
                            // `Release` pairs with `Acquire` in `Self::force`
                            // and/or this method's `Initializing` case.
                            let prev = InitState::try_from(
                                self.init_state
                                    .swap(InitState::Initd.into(), atomic::Ordering::Release),
                            )
                            .unwrap();
                            match prev {
                                InitState::Initializing => {
                                    // No sleepers; no need to do a futex wake
                                }
                                InitState::InitializingWithSleepers => {
                                    // There are blocked threads; wake them.
                                    sync::futex_wake_all(&self.init_state).unwrap();
                                }
                                other => panic!("Unexpected state {other:?}"),
                            };
                            return;
                        }
                        Err(v) => {
                            // It changed out from under us; update and try again.
                            state = v.try_into().unwrap();
                        }
                    }
                }
                InitState::Initializing => {
                    state = match self.init_state.compare_exchange(
                        InitState::Initializing.into(),
                        InitState::InitializingWithSleepers.into(),
                        atomic::Ordering::Relaxed,
                        // Potentially pair with initialization `Release`.
                        atomic::Ordering::Acquire,
                    ) {
                        Ok(_) => InitState::InitializingWithSleepers,
                        Err(v) => v.try_into().unwrap(),
                    }
                }
                InitState::InitializingWithSleepers => {
                    match sync::futex_wait(
                        &self.init_state,
                        InitState::InitializingWithSleepers.into(),
                        None,
                    ) {
                        Ok(_) | Err(rustix::io::Errno::INTR) | Err(rustix::io::Errno::AGAIN) => (),
                        Err(e) => panic!("Unexpected error: {e:?}"),
                    }
                    // `Acquire` pairs with `Release` in `Uninitd` case.
                    state = InitState::try_from(self.init_state.load(atomic::Ordering::Acquire))
                        .unwrap();
                }
                InitState::Initd => {
                    break;
                }
            }
        }
    }

    /// Force initialization and return a reference object.
    ///
    /// std's LazyLock returns an actual reference here and implements Deref.
    /// We can't because of our usage of loom's UnsafeCell, which doesn't support
    /// getting a raw reference that's not borrowed from an intermediate temp object.
    #[inline]
    pub fn force(&self) -> Ref<T> {
        // `Acquire` pairs with `Release` in `Uninitd` case of `init`.
        let state = InitState::try_from(self.init_state.load(atomic::Ordering::Acquire)).unwrap();

        // Having this check in this small `inline` function that avoids calling
        // the larger initialization function in the common case substantially
        // improves performance in the microbenchmark.
        if state != InitState::Initd {
            // Call the non-inlined slow path
            self.init(state);
        }

        Ref {
            val: self.value.get(),
            _phantom: PhantomData,
        }
    }

    /// Whether `self` is initialized yet.
    #[inline]
    pub fn initd(&self) -> bool {
        InitState::try_from(self.init_state.load(atomic::Ordering::Relaxed)).unwrap()
            == InitState::Initd
    }
}

/// We only implement Deref outside of Loom, since Loom requires an intermediate
/// object to catch invalid accesses to our internal `UnsafeCell`s.
#[cfg(not(loom))]
impl<T, Init> core::ops::Deref for LazyLock<T, Init>
where
    Init: Producer<T>,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.force();
        let ptr: *mut MaybeUninit<T> = self.value.untracked_get();
        // SAFETY: Pointer is valid and no mutable refs exist after
        // initialization.
        let ptr = unsafe { ptr.as_ref().unwrap() };
        // SAFETY: `force` ensured initialization.
        unsafe { ptr.assume_init_ref() }
    }
}

impl<T, Init> Drop for LazyLock<T, Init> {
    fn drop(&mut self) {
        // `Acquire` pairs with `Release` in `init`.
        if InitState::try_from(self.init_state.load(atomic::Ordering::Acquire)).unwrap()
            == InitState::Initd
        {
            unsafe { self.value.get_mut().deref().assume_init_drop() }
        }
    }
}

pub struct Ref<'a, T> {
    val: ConstPtr<MaybeUninit<T>>,
    _phantom: PhantomData<&'a T>,
}

impl<T> core::ops::Deref for Ref<'_, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: LazyLock enforces no more mutable references to this data
        // before any instances of this type are constructed, and that val is
        // initd.
        unsafe { self.val.deref().assume_init_ref() }
    }
}
