use crate::{bindings, const_conversions};

/// bytes of args + environ for exec()
pub const ARG_MAX: usize = const_conversions::usize_from_u32(bindings::LINUX_ARG_MAX);

/// # chars in a file name
pub const NAME_MAX: usize = const_conversions::usize_from_u32(bindings::LINUX_NAME_MAX);

/// # chars in a path name including nul
pub const PATH_MAX: usize = const_conversions::usize_from_u32(bindings::LINUX_PATH_MAX);
