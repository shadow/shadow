//! Types used in emulating syscalls.

use std::marker::PhantomData;
use std::mem::size_of;

use linux_api::errno::Errno;
use log::Level::Debug;
use log::*;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::syscall_types::{ForeignPtr, SyscallReg};

use crate::cshadow as c;
use crate::host::descriptor::{File, FileState};
use crate::host::syscall::condition::SyscallCondition;
use crate::host::syscall::Trigger;

/// Wrapper around a [`ForeignPtr`] that encapsulates its size and current position.
#[derive(Copy, Clone)]
pub struct ForeignArrayPtr<T> {
    base: ForeignPtr<T>,
    count: usize,
    _phantom: std::marker::PhantomData<T>,
}

impl<T> std::fmt::Debug for ForeignArrayPtr<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ForeignArrayPtr")
            .field("base", &self.base)
            .field("count", &self.count)
            .field("size_of::<T>", &size_of::<T>())
            .finish()
    }
}

impl<T> ForeignArrayPtr<T> {
    /// Creates a typed pointer. Note though that the pointer *isn't* guaranteed
    /// to be aligned for `T`.
    pub fn new(ptr: ForeignPtr<T>, count: usize) -> Self {
        if log_enabled!(Debug) && usize::from(ptr) % std::mem::align_of::<T>() != 0 {
            // Linux allows unaligned pointers from user-space, being careful to
            // avoid unaligned accesses that aren's supported by the CPU.
            // https://www.kernel.org/doc/html/latest/core-api/unaligned-memory-access.html.
            //
            // We do the same (e.g. by avoiding direct dereference of such
            // pointers even if mmap'd into shadow), but some bugs may slip
            // through (e.g. by asking for a u8 pointer from the mapping code,
            // but then casting it to some other type). Here we leave a debug
            // message here as a sign-post that this could be the root cause of
            // weirdness that happens afterwards.
            debug!(
                "Creating unaligned pointer {ptr:?}. This is legal, but could trigger latent bugs."
            );
        }
        ForeignArrayPtr {
            base: ptr,
            count,
            _phantom: PhantomData,
        }
    }

    /// Raw foreign pointer.
    pub fn ptr(&self) -> ForeignPtr<T> {
        self.base
    }

    /// Number of items pointed to.
    pub fn len(&self) -> usize {
        self.count
    }

    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    pub fn is_null(&self) -> bool {
        self.base.is_null()
    }

    /// Cast to type `U`. Fails if the total size isn't a multiple of `sizeof<U>`.
    pub fn cast<U>(&self) -> Option<ForeignArrayPtr<U>> {
        let count_bytes = self.count * size_of::<T>();
        if count_bytes % size_of::<U>() != 0 {
            return None;
        }
        Some(ForeignArrayPtr::new(
            self.base.cast::<U>(),
            count_bytes / size_of::<U>(),
        ))
    }

    /// Cast to u8. Infallible since `size_of<u8>` is 1.
    pub fn cast_u8(&self) -> ForeignArrayPtr<u8> {
        self.cast::<u8>().unwrap()
    }

    /// Return a slice of this pointer.
    pub fn slice<R: std::ops::RangeBounds<usize>>(&self, range: R) -> ForeignArrayPtr<T> {
        use std::ops::Bound;
        let excluded_end = match range.end_bound() {
            Bound::Included(e) => e + 1,
            Bound::Excluded(e) => *e,
            Bound::Unbounded => self.count,
        };
        let included_start = match range.start_bound() {
            Bound::Included(s) => *s,
            Bound::Excluded(s) => s + 1,
            Bound::Unbounded => 0,
        };
        assert!(included_start <= excluded_end);
        assert!(excluded_end <= self.count);
        // `<=` rather than `<`, to allow empty slice at end of ptr.
        // e.g. `assert_eq!(&[1,2,3][3..3], &[])` passes.
        assert!(included_start <= self.count);

        ForeignArrayPtr {
            base: self.base.add(included_start),
            count: excluded_end - included_start,
            _phantom: PhantomData,
        }
    }
}

// Calling all of these errors is stretching the semantics of 'error' a bit,
// but it makes for fluent programming in syscall handlers using the `?` operator.
#[derive(Debug, PartialEq, Eq)]
pub enum SyscallError {
    Failed(Failed),
    Blocked(Blocked),
    Native,
}

#[derive(Debug, PartialEq, Eq)]
pub struct Blocked {
    pub condition: SyscallCondition,
    pub restartable: bool,
}

#[derive(Debug, PartialEq, Eq)]
pub struct Failed {
    pub errno: linux_api::errno::Errno,
    pub restartable: bool,
}

pub type SyscallResult = Result<SyscallReg, SyscallError>;

impl From<SyscallReturn> for SyscallResult {
    fn from(r: SyscallReturn) -> Self {
        match r {
            SyscallReturn::Done(done) => {
                match crate::utility::syscall::raw_return_value_to_result(i64::from(done.retval)) {
                    Ok(r) => Ok(r),
                    Err(e) => Err(SyscallError::Failed(Failed {
                        errno: e,
                        restartable: done.restartable,
                    })),
                }
            }
            // SAFETY: XXX: We're assuming this points to a valid SysCallCondition.
            SyscallReturn::Block(blocked) => Err(SyscallError::Blocked(Blocked {
                condition: unsafe { SyscallCondition::consume_from_c(blocked.cond) },
                restartable: blocked.restartable,
            })),
            SyscallReturn::Native => Err(SyscallError::Native),
        }
    }
}

impl From<SyscallResult> for SyscallReturn {
    fn from(syscall_return: SyscallResult) -> Self {
        match syscall_return {
            Ok(r) => SyscallReturn::Done(SyscallReturnDone {
                retval: r,
                // N/A for non-error result (and non-EINTR result in particular)
                restartable: false,
            }),
            Err(SyscallError::Failed(failed)) => SyscallReturn::Done(SyscallReturnDone {
                retval: (-(i64::from(failed.errno))).into(),
                restartable: failed.restartable,
            }),
            Err(SyscallError::Blocked(blocked)) => SyscallReturn::Block(SyscallReturnBlocked {
                cond: blocked.condition.into_inner(),
                restartable: blocked.restartable,
            }),
            Err(SyscallError::Native) => SyscallReturn::Native,
        }
    }
}

impl From<linux_api::errno::Errno> for SyscallError {
    fn from(e: linux_api::errno::Errno) -> Self {
        SyscallError::Failed(Failed {
            errno: e,
            restartable: false,
        })
    }
}

impl From<std::io::Error> for SyscallError {
    fn from(e: std::io::Error) -> Self {
        match std::io::Error::raw_os_error(&e) {
            Some(e) => SyscallError::Failed(Failed {
                // this probably won't panic if rust's io::Error does only return "raw os" error
                // values
                errno: Errno::try_from(u16::try_from(e).unwrap()).unwrap(),
                restartable: false,
            }),
            None => {
                let default = Errno::ENOTSUP;
                warn!("Mapping error {} to {}", e, default);
                SyscallError::from(default)
            }
        }
    }
}

impl SyscallError {
    pub fn new_blocked_on_file(file: File, state: FileState, restartable: bool) -> Self {
        Self::Blocked(Blocked {
            condition: SyscallCondition::new(Trigger::from_file(file, state)),
            restartable,
        })
    }

    pub fn new_blocked_on_child(restartable: bool) -> Self {
        Self::Blocked(Blocked {
            condition: SyscallCondition::new(Trigger::child()),
            restartable,
        })
    }

    pub fn new_blocked_until(unblock_time: EmulatedTime, restartable: bool) -> Self {
        Self::Blocked(Blocked {
            condition: SyscallCondition::new_from_wakeup_time(unblock_time),
            restartable,
        })
    }

    pub fn new_interrupted(restartable: bool) -> Self {
        Self::Failed(Failed {
            errno: Errno::EINTR,
            restartable,
        })
    }

    /// Returns the [condition](SyscallCondition) that the syscall is blocked on.
    pub fn blocked_condition(&mut self) -> Option<&mut SyscallCondition> {
        if let Self::Blocked(Blocked {
            condition, ..
        }) = self
        {
            Some(condition)
        } else {
            None
        }
    }
}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct SyscallReturnDone {
    pub retval: SyscallReg,
    // Only meaningful when `retval` is -EINTR.
    //
    // Whether the interrupted syscall is restartable.
    pub restartable: bool,
}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct SyscallReturnBlocked {
    pub cond: *mut c::SysCallCondition,
    // True if the syscall is restartable in the case that it was interrupted by
    // a signal. e.g. if the syscall was a `read` operation on a socket without
    // a configured timeout. See socket(7).
    pub restartable: bool,
}

#[derive(Copy, Clone, Debug)]
#[repr(i8, C)]
pub enum SyscallReturn {
    /// Done executing the syscall; ready to let the plugin thread resume.
    Done(SyscallReturnDone),
    /// We don't have the result yet.
    Block(SyscallReturnBlocked),
    /// Direct plugin to make the syscall natively.
    Native,
}

mod export {
    use shadow_shim_helper_rs::syscall_types::UntypedForeignPtr;

    use super::*;

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeDone(retval: SyscallReg) -> SyscallReturn {
        SyscallReturn::Done(SyscallReturnDone {
            retval,
            restartable: false,
        })
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeDoneI64(retval: i64) -> SyscallReturn {
        SyscallReturn::Done(SyscallReturnDone {
            retval: retval.into(),
            restartable: false,
        })
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeDoneU64(retval: u64) -> SyscallReturn {
        SyscallReturn::Done(SyscallReturnDone {
            retval: retval.into(),
            restartable: false,
        })
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeDonePtr(
        retval: UntypedForeignPtr,
    ) -> SyscallReturn {
        SyscallReturn::Done(SyscallReturnDone {
            retval: retval.into(),
            restartable: false,
        })
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeDoneErrno(err: i32) -> SyscallReturn {
        debug_assert!(err > 0);
        // Should use `syscallreturn_makeInterrupted` instead
        debug_assert!(err != libc::EINTR);
        SyscallReturn::Done(SyscallReturnDone {
            retval: (-err).into(),
            restartable: false,
        })
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeInterrupted(
        restartable: bool,
    ) -> SyscallReturn {
        SyscallReturn::Done(SyscallReturnDone {
            retval: (-libc::EINTR).into(),
            restartable,
        })
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeBlocked(
        cond: *mut c::SysCallCondition,
        restartable: bool,
    ) -> SyscallReturn {
        SyscallReturn::Block(SyscallReturnBlocked { cond, restartable })
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_makeNative() -> SyscallReturn {
        SyscallReturn::Native
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_blocked(
        scr: *mut SyscallReturn,
    ) -> *mut SyscallReturnBlocked {
        let scr = unsafe { scr.as_mut().unwrap() };
        let SyscallReturn::Block(b) = scr else {
            panic!("Unexpected scr {:?}", scr);
        };
        b
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn syscallreturn_done(
        scr: *mut SyscallReturn,
    ) -> *mut SyscallReturnDone {
        let scr = unsafe { scr.as_mut().unwrap() };
        let SyscallReturn::Done(d) = scr else {
            panic!("Unexpected scr {:?}", scr);
        };
        d
    }
}
