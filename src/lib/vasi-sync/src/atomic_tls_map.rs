use crate::sync::{AtomicUsize, Cell, ConstPtr, UnsafeCell, atomic};

use core::hash::{BuildHasher, Hasher};
use core::marker::PhantomData;
use core::mem::MaybeUninit;
use core::num::NonZeroUsize;
use core::ops::Deref;

/// A lockless, no_std, no-alloc hash table.
///
/// Allows insertion and removal from an immutable reference, but does not
/// support getting mutable references to internal values, and requires that a
/// particular key is only ever accessed from the thread that inserted it, until
/// that thread removes it.
///
/// Uses linear probing, and doesn't support resizing.  Lookup is `Θ(1)`
/// (average case) if the key is present and the key hasn't been forced far away
/// from its "home" location, but is `O(N)` worst case. Lookup of a non-present
/// key is always `O(N)`; we need to scan the whole table.
///
/// This is designed mostly for use by `shadow_shim::tls` to help implement
/// thread-local storage.
pub struct AtomicTlsMap<const N: usize, V, H = core::hash::BuildHasherDefault<rustc_hash::FxHasher>>
where
    H: BuildHasher,
{
    keys: [AtomicOptionNonZeroUsize; N],
    values: [UnsafeCell<MaybeUninit<V>>; N],
    // This lets us enforce that a still-referenced key is not removed.
    // TODO: Consider storing `RefCell<V>` in `values` instead. That'd be a bit
    // more idiomatic, and is probably a better layout for cache performance.
    refcounts: [Cell<usize>; N],
    build_hasher: H,
}
/// Override default of `UnsafeCell`, `Cell`, and `V` not being `Sync`.  We
/// synchronize access to these (if partly by requiring users to guarantee no
/// parallel access to a given key from multiple threads).
/// Likewise `V` only needs to be `Send`.
unsafe impl<const N: usize, V, H> Sync for AtomicTlsMap<N, V, H>
where
    // Requires for the Drop implementation to be able to drop values that were
    // inserted by a different thread. Also if we want to support values being
    // accessed by multiple threads with some kind of external synchronization,
    // but I don't think we do.
    //
    // Alternatively we could only have this bound on the `Drop` implemenation,
    // and document that the final contents aren't dropped if `V` isn't send. Or
    // just remove the `Drop` impementation altogether.
    V: Send,
    H: Sync + BuildHasher,
{
}

/// Adapter for `Option<NonZeroUsize>` around `AtomicUsize`
struct AtomicOptionNonZeroUsize(AtomicUsize);
impl AtomicOptionNonZeroUsize {
    fn to_usize(val: Option<NonZeroUsize>) -> usize {
        val.map(NonZeroUsize::get).unwrap_or(0)
    }

    fn from_usize(val: usize) -> Option<NonZeroUsize> {
        NonZeroUsize::new(val)
    }

    pub fn new(val: Option<NonZeroUsize>) -> Self {
        Self(AtomicUsize::new(Self::to_usize(val)))
    }

    pub fn load(&self, order: atomic::Ordering) -> Option<NonZeroUsize> {
        Self::from_usize(self.0.load(order))
    }

    pub fn store(&self, val: Option<NonZeroUsize>, order: atomic::Ordering) {
        self.0.store(Self::to_usize(val), order)
    }

    pub fn compare_exchange(
        &self,
        current: Option<NonZeroUsize>,
        new: Option<NonZeroUsize>,
        success: atomic::Ordering,
        failure: atomic::Ordering,
    ) -> Result<Option<NonZeroUsize>, Option<NonZeroUsize>> {
        self.0
            .compare_exchange(
                Self::to_usize(current),
                Self::to_usize(new),
                success,
                failure,
            )
            .map(Self::from_usize)
            .map_err(Self::from_usize)
    }
}

impl<const N: usize, V, H> AtomicTlsMap<N, V, H>
where
    H: BuildHasher,
{
    pub fn new_with_hasher(build_hasher: H) -> Self {
        Self {
            keys: core::array::from_fn(|_| AtomicOptionNonZeroUsize::new(None)),
            values: core::array::from_fn(|_| UnsafeCell::new(MaybeUninit::uninit())),
            refcounts: core::array::from_fn(|_| Cell::new(0)),
            build_hasher,
        }
    }

    /// All indexes starting from the hash position of `key`.
    fn indexes_from(&self, key: NonZeroUsize) -> impl Iterator<Item = usize> {
        let mut hasher = self.build_hasher.build_hasher();
        hasher.write_usize(key.get());
        let hash = hasher.finish();
        let start_idx = usize::try_from(hash).unwrap() % N;
        (start_idx..N).chain(0..start_idx)
    }

    /// The index containing `key`, if any. No synchronization.
    ///
    /// TODO: Consider keeping track of whether/where we saw vacancies along the
    /// way in linear search, and moving the value if its refcount is currently
    /// 0.
    fn idx(&self, key: NonZeroUsize) -> Option<usize> {
        self.indexes_from(key).find(|idx| {
            // Relaxed because of requirement that only one thread ever accesses
            // a given key at once.
            self.keys[*idx].load(atomic::Ordering::Relaxed) == Some(key)
        })
    }

    /// # Safety
    ///
    /// The value at `key`, if any, must have been inserted by the current thread.
    #[inline]
    pub unsafe fn get(&self, key: NonZeroUsize) -> Option<Ref<'_, V>> {
        // SAFETY: Ensured by caller
        let idx = self.idx(key)?;
        let ptr = self.values[idx].get();
        Some(unsafe { Ref::new(ptr, &self.refcounts[idx]) })
    }

    /// Insert `(key, value)`.
    ///
    /// If `key` is already present in `self`, the previous value would shadow
    /// the newly inserted value. We don't expose this function in the public
    /// API since this behavior would be confusing.
    ///
    /// Returns a reference to the newly inserted value.
    ///
    /// Panics if the table is full.
    ///
    /// # Safety
    ///
    /// There must not be a value at `key` that was inserted by a different
    /// thread.
    unsafe fn insert(&self, key: NonZeroUsize, value: V) -> Ref<'_, V> {
        let idx = self
            .indexes_from(key)
            .find(|idx| {
                self.keys[*idx]
                    .compare_exchange(
                        None,
                        Some(key),
                        // Syncs with `Release` on removal
                        atomic::Ordering::Acquire,
                        atomic::Ordering::Relaxed,
                    )
                    .is_ok()
            })
            .unwrap();
        self.values[idx].get_mut().with(|table_value| {
            let table_value = unsafe { &mut *table_value };
            table_value.write(value)
        });
        unsafe { Ref::new(self.values[idx].get(), &self.refcounts[idx]) }
    }

    /// Retrieve the value associated with `key`, initializing it with `init` if `key`
    /// is not already present.
    ///
    /// Panics if the table is full and `key` is not already present.
    ///
    /// # Safety
    ///
    /// There must not be a value at `key` that was inserted by a different
    /// thread.
    #[inline]
    pub unsafe fn get_or_insert_with(
        &self,
        key: NonZeroUsize,
        init: impl FnOnce() -> V,
    ) -> Ref<'_, V> {
        let val = unsafe { self.get(key) };
        val.unwrap_or_else(|| {
            let val = init();
            // SAFETY: Ensured by caller
            unsafe { self.insert(key, val) }
        })
    }

    /// Removes the value still for `key`, if any. Panics if this thread has
    /// any outstanding references for `key`.
    ///
    /// # Safety
    ///
    /// The value at `key`, if any, must have been inserted by the current thread.
    pub unsafe fn remove(&self, key: NonZeroUsize) -> Option<V> {
        let idx = self.idx(key)?;
        assert_eq!(
            self.refcounts[idx].get(),
            0,
            "Removed key while references still held: {key:?}"
        );
        let value = self.values[idx].get_mut().with(|value| {
            let value = unsafe { &mut *value };
            unsafe { value.assume_init_read() }
        });

        // Careful not to panic between `assume_init_read` above and the `store`
        // below; doing so would cause `value` to be dropped twice.

        // Syncs with `Acquire` on insertion
        self.keys[idx].store(None, atomic::Ordering::Release);
        Some(value)
    }

    /// Resets metadata in the map to mark all entries vacant, without dropping
    /// the values.
    ///
    /// Intended for use after `fork`, after which entries belonging to other threads
    /// are not guaranteed to be in any consistent state (so can't be dropped), but
    /// the threads owning those entries no longer exist in the child, so they *can*
    /// be safely overwritten.
    ///
    /// # Safety
    ///
    /// Any outstanding references from `self` (e.g. obtained via Self::get)
    /// must not be accessed *or dropped* again. e.g. references held by other
    /// threads before `fork` are OK, since those threads do not exist in the
    /// current process, and so will not access the child's copy of this table.
    /// References that have been forgotten via `core::mem::forget` are also ok.
    pub unsafe fn forget_all(&self) {
        for idx in 0..N {
            self.refcounts[idx].set(0);
            self.keys[idx].store(None, atomic::Ordering::Release);
        }
    }
}

impl<const N: usize, V, H> AtomicTlsMap<N, V, H>
where
    H: BuildHasher + Default,
{
    // This `inline` is important when allocating large instances, since
    // otherwise the compiler can't avoid create a temporary copy on the stack,
    // which might not fit.
    //
    // See https://stackoverflow.com/questions/25805174/creating-a-fixed-size-array-on-heap-in-rust/68122278#68122278
    #[inline]
    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        Self::new_with_hasher(Default::default())
    }
}

impl<const N: usize, V, H> Drop for AtomicTlsMap<N, V, H>
where
    H: BuildHasher,
{
    fn drop(&mut self) {
        for idx in 0..N {
            // No special synchronization requirements here since we have a
            // `mut` reference to self. Even values that were inserted by other
            // threads should now be safe to access; for us to have obtained a
            // `mut` reference some external synchronization must have occurred,
            // which should make the values safely accessible by this thread.
            if self.keys[idx].load(atomic::Ordering::Relaxed).is_some() {
                self.values[idx].get_mut().with(|value| {
                    assert_eq!(self.refcounts[idx].get(), 0);
                    // SAFETY: We have exclusive access to `self`.
                    let value = unsafe { &mut *value };
                    // SAFETY: We know the value is initialized.
                    unsafe { value.assume_init_drop() }
                })
            }
        }
    }
}

pub struct Ref<'a, V> {
    ptr: ConstPtr<MaybeUninit<V>>,
    refcount: &'a Cell<usize>,
    _phantom: PhantomData<&'a V>,
}
static_assertions::assert_not_impl_any!(Ref<'static, ()>: Send, Sync);

impl<'a, V> Ref<'a, V> {
    /// # Safety
    ///
    /// Current thread must be the only one to access `refcount`
    unsafe fn new(ptr: ConstPtr<MaybeUninit<V>>, refcount: &'a Cell<usize>) -> Self {
        refcount.set(refcount.get() + 1);
        Self {
            ptr,
            refcount,
            _phantom: PhantomData,
        }
    }
}

impl<V> Deref for Ref<'_, V> {
    type Target = V;

    fn deref(&self) -> &Self::Target {
        // SAFETY: The table ensures no mutable accesses to this value as long
        // as `Ref`s exist.
        let val = unsafe { self.ptr.deref() };
        // SAFETY: The table ensures that the value is initialized before
        // constructing this `Ref`.
        unsafe { val.assume_init_ref() }
    }
}

impl<V> Drop for Ref<'_, V> {
    fn drop(&mut self) {
        self.refcount.set(self.refcount.get() - 1)
    }
}
