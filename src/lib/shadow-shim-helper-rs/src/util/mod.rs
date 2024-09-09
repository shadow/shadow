pub mod time;

/// A trait to prevent type inference during function calls.
///
/// Useful when you have a type that wraps a pointer (like `ForeignArrayPtr`) and you don't want
/// Rust to infer the type of pointer during creation.  Instead, the caller must specify the generic
/// type.
///
/// Example:
///
/// ```ignore
/// let x: ForeignArrayPtr<u8>;
///
/// // normally the `<u8>` wouldn't be required since Rust would infer it from the type of `x`, but
/// // for this function using [`NoTypeInference`], the `<u8>` is required and must match
/// x = ForeignArrayPtr::new::<u8>(...);
/// ```
pub trait NoTypeInference {
    type This;
}

impl<T> NoTypeInference for T {
    type This = T;
}

/// A type that allows us to make a pointer Send + Sync since there is no way
/// to add these traits to the pointer itself.
pub struct SyncSendPointer<T>(*mut T);

// We can't automatically `derive` Copy and Clone without unnecessarily
// requiring T to be Copy and Clone.
// https://github.com/rust-lang/rust/issues/26925
impl<T> Copy for SyncSendPointer<T> {}
impl<T> Clone for SyncSendPointer<T> {
    fn clone(&self) -> Self {
        *self
    }
}

unsafe impl<T> Send for SyncSendPointer<T> {}
unsafe impl<T> Sync for SyncSendPointer<T> {}

impl<T> SyncSendPointer<T> {
    /// # Safety
    ///
    /// The object pointed to by `ptr` must actually be `Sync` and `Send` or
    /// else not subsequently used in contexts where it matters.
    pub unsafe fn new(ptr: *mut T) -> Self {
        Self(ptr)
    }

    pub fn ptr(&self) -> *mut T {
        self.0
    }
}

impl<T> std::fmt::Debug for SyncSendPointer<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.ptr())
    }
}

impl<T> PartialEq for SyncSendPointer<T> {
    fn eq(&self, other: &Self) -> bool {
        std::ptr::eq(self.ptr(), other.ptr())
    }
}

impl<T> Eq for SyncSendPointer<T> {}

/// A type that allows us to make a pointer Send since there is no way
/// to add this traits to the pointer itself.
pub struct SendPointer<T>(*mut T);

// We can't automatically `derive` Copy and Clone without unnecessarily
// requiring T to be Copy and Clone.
// https://github.com/rust-lang/rust/issues/26925
impl<T> Copy for SendPointer<T> {}
impl<T> Clone for SendPointer<T> {
    fn clone(&self) -> Self {
        *self
    }
}

unsafe impl<T> Send for SendPointer<T> {}

impl<T> SendPointer<T> {
    /// # Safety
    ///
    /// The object pointed to by `ptr` must actually be `Send` or else not
    /// subsequently used in contexts where it matters.
    pub unsafe fn new(ptr: *mut T) -> Self {
        Self(ptr)
    }

    pub fn ptr(&self) -> *mut T {
        self.0
    }
}

impl<T> std::fmt::Debug for SendPointer<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.ptr())
    }
}

impl<T> PartialEq for SendPointer<T> {
    fn eq(&self, other: &Self) -> bool {
        std::ptr::eq(self.ptr(), other.ptr())
    }
}

impl<T> Eq for SendPointer<T> {}

/// Implements [`Debug`](std::fmt::Debug) using the provided closure.
pub struct DebugFormatter<F>(pub F)
where
    F: Fn(&mut std::fmt::Formatter<'_>) -> std::fmt::Result;

impl<F> std::fmt::Debug for DebugFormatter<F>
where
    F: Fn(&mut std::fmt::Formatter<'_>) -> std::fmt::Result,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0(f)
    }
}
