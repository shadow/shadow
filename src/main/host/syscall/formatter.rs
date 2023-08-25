use std::any::TypeId;
use std::fmt::Display;
use std::marker::PhantomData;

use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::syscall_types::SysCallReg;
use shadow_shim_helper_rs::util::time::TimeParts;

use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::host::thread::ThreadId;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum FmtOptions {
    Standard,
    Deterministic,
}

// this type is required until we no longer need to access the format options from C
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(C)]
pub enum StraceFmtMode {
    Off,
    Standard,
    Deterministic,
}

impl From<StraceFmtMode> for Option<FmtOptions> {
    fn from(x: StraceFmtMode) -> Self {
        match x {
            StraceFmtMode::Off => None,
            StraceFmtMode::Standard => Some(FmtOptions::Standard),
            StraceFmtMode::Deterministic => Some(FmtOptions::Deterministic),
        }
    }
}

impl From<Option<FmtOptions>> for StraceFmtMode {
    fn from(x: Option<FmtOptions>) -> Self {
        match x {
            None => StraceFmtMode::Off,
            Some(FmtOptions::Standard) => StraceFmtMode::Standard,
            Some(FmtOptions::Deterministic) => StraceFmtMode::Deterministic,
        }
    }
}

pub trait SyscallDisplay {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        options: FmtOptions,
        mem: &MemoryManager,
    ) -> std::fmt::Result;
}

/// A syscall argument or return value. It implements [`Display`], and only reads memory and
/// converts types when being formatted.
pub struct SyscallVal<'a, T> {
    pub reg: SysCallReg,
    pub args: [SysCallReg; 6],
    options: FmtOptions,
    mem: &'a MemoryManager,
    _phantom: PhantomData<T>,
}

impl<'a, T> SyscallVal<'a, T> {
    pub fn new(
        reg: SysCallReg,
        args: [SysCallReg; 6],
        options: FmtOptions,
        mem: &'a MemoryManager,
    ) -> Self {
        Self {
            reg,
            args,
            options,
            mem,
            _phantom: PhantomData,
        }
    }
}

impl<'a, T> Display for SyscallVal<'a, T>
where
    SyscallVal<'a, T>: SyscallDisplay,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        SyscallDisplay::fmt(self, f, self.options, self.mem)
    }
}

/// A marker type for indicating there are no types left in the syscall arguments.
#[derive(Default)]
pub struct NoArg {}

impl SyscallDisplay for SyscallVal<'_, NoArg> {
    fn fmt(
        &self,
        _f: &mut std::fmt::Formatter<'_>,
        _options: FmtOptions,
        _mem: &MemoryManager,
    ) -> std::fmt::Result {
        panic!("We shouldn't ever try to format this.");
    }
}

/// A formatting wrapper for six syscall arguments.
pub struct SyscallArgsFmt<'a, A = NoArg, B = NoArg, C = NoArg, D = NoArg, E = NoArg, F = NoArg> {
    a: SyscallVal<'a, A>,
    b: SyscallVal<'a, B>,
    c: SyscallVal<'a, C>,
    d: SyscallVal<'a, D>,
    e: SyscallVal<'a, E>,
    f: SyscallVal<'a, F>,
}

impl<'a, A, B, C, D, E, F> SyscallArgsFmt<'a, A, B, C, D, E, F>
where
    SyscallVal<'a, A>: Display,
    SyscallVal<'a, B>: Display,
    SyscallVal<'a, C>: Display,
    SyscallVal<'a, D>: Display,
    SyscallVal<'a, E>: Display,
    SyscallVal<'a, F>: Display,
{
    pub fn new(args: [SysCallReg; 6], options: FmtOptions, mem: &'a MemoryManager) -> Self {
        Self {
            a: SyscallVal::new(args[0], args, options, mem),
            b: SyscallVal::new(args[1], args, options, mem),
            c: SyscallVal::new(args[2], args, options, mem),
            d: SyscallVal::new(args[3], args, options, mem),
            e: SyscallVal::new(args[4], args, options, mem),
            f: SyscallVal::new(args[5], args, options, mem),
        }
    }
}

impl<'a, A, B, C, D, E, F> Display for SyscallArgsFmt<'a, A, B, C, D, E, F>
where
    SyscallVal<'a, A>: Display,
    SyscallVal<'a, B>: Display,
    SyscallVal<'a, C>: Display,
    SyscallVal<'a, D>: Display,
    SyscallVal<'a, E>: Display,
    SyscallVal<'a, F>: Display,
    A: 'static,
    B: 'static,
    C: 'static,
    D: 'static,
    E: 'static,
    F: 'static,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let args: [&dyn Display; 6] = [&self.a, &self.b, &self.c, &self.d, &self.e, &self.f];

        let types: [TypeId; 6] = [
            TypeId::of::<A>(),
            TypeId::of::<B>(),
            TypeId::of::<C>(),
            TypeId::of::<D>(),
            TypeId::of::<E>(),
            TypeId::of::<F>(),
        ];

        let mut first = true;
        for (arg, arg_type) in args.iter().zip(types) {
            if arg_type == TypeId::of::<NoArg>() {
                // the user didn't override this generic type, so it and any following types/args
                // should not be shown
                break;
            }

            if first {
                write!(f, "{arg}")?;
                first = false;
            } else {
                write!(f, ", {arg}")?;
            }
        }

        Ok(())
    }
}

/// A formatting wrapper for the syscall result.
pub struct SyscallResultFmt<'a, RV>
where
    SyscallVal<'a, RV>: Display,
    RV: std::fmt::Debug,
{
    rv: &'a SyscallResult,
    args: [SysCallReg; 6],
    options: FmtOptions,
    mem: &'a MemoryManager,
    _phantom: PhantomData<RV>,
}

impl<'a, RV> SyscallResultFmt<'a, RV>
where
    SyscallVal<'a, RV>: Display,
    RV: std::fmt::Debug,
{
    pub fn new(
        rv: &'a SyscallResult,
        args: [SysCallReg; 6],
        options: FmtOptions,
        mem: &'a MemoryManager,
    ) -> Option<Self> {
        match &rv {
            SyscallResult::Ok(_)
            | SyscallResult::Err(SyscallError::Failed(_))
            | SyscallResult::Err(SyscallError::Native) => Some(Self {
                rv,
                args,
                options,
                mem,
                _phantom: PhantomData,
            }),
            // the syscall was not completed and will be re-run again later
            SyscallResult::Err(SyscallError::Blocked(_)) => None,
        }
    }
}

impl<'a, RV> Display for SyscallResultFmt<'a, RV>
where
    SyscallVal<'a, RV>: Display,
    RV: std::fmt::Debug,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.rv {
            SyscallResult::Ok(x) => {
                let rv = SyscallVal::<'_, RV>::new(*x, self.args, self.options, self.mem);
                write!(f, "{rv}")
            }
            SyscallResult::Err(SyscallError::Failed(failed)) => {
                let errno = failed.errno;
                let rv = SysCallReg::from(errno.to_negated_i64());
                let rv = SyscallVal::<'_, RV>::new(rv, self.args, self.options, self.mem);
                write!(f, "{rv} ({errno})")
            }
            SyscallResult::Err(SyscallError::Native) => {
                write!(f, "<native>")
            }
            // the constructor doesn't allow this
            SyscallResult::Err(SyscallError::Blocked(_)) => unreachable!(),
        }
    }
}

/// Format and write the syscall.
pub fn write_syscall(
    mut writer: impl std::io::Write,
    sim_time: &EmulatedTime,
    tid: ThreadId,
    name: impl Display,
    args: impl Display,
    rv: impl Display,
) -> std::io::Result<()> {
    let sim_time = sim_time.duration_since(&EmulatedTime::SIMULATION_START);
    let sim_time = TimeParts::from_nanos(sim_time.as_nanos());
    let sim_time = sim_time.fmt_hr_min_sec_nano();

    writeln!(writer, "{sim_time} [tid {tid}] {name}({args}) = {rv}")
}

mod export {
    use std::ffi::CStr;

    use super::*;
    use crate::core::worker::Worker;
    use crate::host::process::Process;
    use crate::host::syscall_types::SyscallReturn;

    #[no_mangle]
    pub extern "C" fn log_syscall(
        proc: *const Process,
        logging_mode: StraceFmtMode,
        tid: libc::pid_t,
        name: *const libc::c_char,
        args_str: *const libc::c_char,
        args: &[SysCallReg; 6],
        result: SyscallReturn,
    ) -> SyscallReturn {
        assert!(!proc.is_null());
        assert!(!name.is_null());
        assert!(!args_str.is_null());

        let name = unsafe { CStr::from_ptr(name) }.to_str().unwrap();
        let args_str = unsafe { CStr::from_ptr(args_str) }.to_str().unwrap();
        let result = SyscallResult::from(result);

        let logging_mode = logging_mode.into();
        let Some(logging_mode) = logging_mode else {
            // logging was disabled
            return result.into();
        };

        let proc = unsafe { proc.as_ref().unwrap() };

        // we don't know the type, so just show it as an int
        let memory = proc.memory_borrow();
        let rv = SyscallResultFmt::<libc::c_long>::new(&result, *args, logging_mode, &memory);

        if let Some(ref rv) = rv {
            proc.with_strace_file(|file| {
                let time = Worker::current_time();

                if let (Some(time), Ok(tid)) = (time, tid.try_into()) {
                    write_syscall(file, &time, tid, name, args_str, rv).unwrap();
                } else {
                    log::warn!("Could not log syscall {name} with time {time:?} and tid {tid}");
                }
            });
        }

        // need to return the result, otherwise the drop impl will free the condition pointer
        result.into()
    }
}

#[cfg(test)]
mod test {
    use std::process::Command;

    use shadow_shim_helper_rs::syscall_types::SysCallArgs;

    use super::*;

    #[test]
    // can't call foreign function: gnu_get_libc_version
    #[cfg_attr(miri, ignore)]
    fn test_no_args() {
        let args = SysCallArgs {
            number: 100,
            args: [0u32.into(); 6],
        };

        // 10 seconds should be long enough to keep the process alive while the following code runs
        let mut proc = Command::new("sleep").arg(10.to_string()).spawn().unwrap();
        let pid = nix::unistd::Pid::from_raw(proc.id().try_into().unwrap());

        let mem = unsafe { MemoryManager::new(pid) };

        // make sure that we can construct a `SyscallArgsFmt` with no generic types
        let _syscall_args = <SyscallArgsFmt>::new(args.args, FmtOptions::Standard, &mem);

        proc.kill().unwrap();
        proc.wait().unwrap();
    }
}
