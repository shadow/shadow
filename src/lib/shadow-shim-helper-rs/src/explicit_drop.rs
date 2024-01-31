/// Trait for a type that provides an explicit method for dropping its value.
///
/// Unlike the `Drop` trait, this traits method:
/// * Can take a parameter
/// * Can return something
/// * Consumes the value (instead of taking a `&mut`)
///
/// Unfortunately there is no built-in way to *ensure* a type is explicitly
/// dropped. One workaround is for a type to also implement `Drop`, and have
/// that implementation validate that `ExplicitDrop::explicit_drop` was called.
pub trait ExplicitDrop {
    type ExplicitDropParam;
    type ExplicitDropResult;
    fn explicit_drop(self, param: &Self::ExplicitDropParam) -> Self::ExplicitDropResult;
}

/// Wrapper that uses a provided function to drop the inner value.
///
/// This is helpful for working with locals that must be explicitly dropped, without
/// having to explicitly do so at every potential exit point of the current function.
///
/// ```
/// # use shadow_shim_helper_rs::explicit_drop::{ExplicitDrop, ExplicitDropper};
/// # use shadow_shim_helper_rs::rootedcell::Root;
/// # use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
/// # use std::string::String;
/// # use std::error::Error;
/// fn validate_and_rc(root: &Root, s: String) -> Result<RootedRc<String>, Box<dyn Error>> {
///   let rc_string = ExplicitDropper::new(
///     RootedRc::new(root, s),
///     |value| value.explicit_drop(root));
///
///   if !rc_string.starts_with("x") {
///     // `ExplicitDropper` will call the provided closure, safely dropping the RootedRc.
///     return Err("bad prefix".into());
///   }
///
///   // We extract the value from the dropper at the point where we transfer ownership;
///   // in this case the closure is never called.
///   return Ok(rc_string.into_value())
/// }
/// ```
pub struct ExplicitDropper<DropFn, Value>
where
    DropFn: FnOnce(Value),
{
    internal: Option<(DropFn, Value)>,
}

impl<DropFn, Value> ExplicitDropper<DropFn, Value>
where
    DropFn: FnOnce(Value),
{
    /// Create a wrapped value
    pub fn new(value: Value, dropper: DropFn) -> Self {
        Self {
            internal: Some((dropper, value)),
        }
    }

    /// Unwrap the value, discarding the dropper.
    pub fn into_value(mut self) -> Value {
        let (_drop_fn, value) = self.internal.take().unwrap();
        value
    }
}

impl<DropFn, Value> Drop for ExplicitDropper<DropFn, Value>
where
    DropFn: FnOnce(Value),
{
    fn drop(&mut self) {
        if let Some((drop_fn, value)) = self.internal.take() {
            drop_fn(value)
        }
    }
}

impl<DropFn, Value> std::ops::Deref for ExplicitDropper<DropFn, Value>
where
    DropFn: FnOnce(Value),
{
    type Target = Value;

    fn deref(&self) -> &Self::Target {
        let (_drop_fn, value) = self.internal.as_ref().unwrap();
        value
    }
}

impl<DropFn, Value> std::ops::DerefMut for ExplicitDropper<DropFn, Value>
where
    DropFn: FnOnce(Value),
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        let (_drop_fn, value) = self.internal.as_mut().unwrap();
        value
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_explicit_dropper_explicitly_drops() {
        let mut dropper_called = false;
        let x = ExplicitDropper::new(42, |val| {
            assert_eq!(val, 42);
            dropper_called = true;
        });
        drop(x);
        assert!(dropper_called);
    }

    #[test]
    fn test_explicit_dropper_into_value() {
        let mut dropper_called = false;
        let x = ExplicitDropper::new(42, |_| dropper_called = true);
        assert_eq!(x.into_value(), 42);
        assert!(!dropper_called);
    }
}
