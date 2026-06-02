use crate::bindings;
use crate::posix_types::kernel_key_t;

/// Specifies permissions for an ipc object. Used in `shmid64_ds`, which is used
/// by the `shmctl` syscall.
pub use bindings::linux_ipc64_perm as ipc64_perm;

// Defined in linux/ipc.h, but bindgen doesn't seem to parse it.
// `#define IPC_PRIVATE ((__kernel_key_t) 0)`.
pub const IPC_PRIVATE: kernel_key_t = 0;
