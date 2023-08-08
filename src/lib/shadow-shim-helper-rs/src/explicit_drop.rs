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
