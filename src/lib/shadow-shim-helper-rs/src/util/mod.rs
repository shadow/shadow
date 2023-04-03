/// A trait to prevent type inference during function calls. Useful when you have a type that wraps
/// a pointer (like `ForeignArrayPtr`) and you don't want Rust to infer the type of pointer
/// during creation.  Instead, the caller must specify the generic type.
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
