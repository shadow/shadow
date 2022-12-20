use nix::errno::Errno;
use std::any::TypeId;
use std::fmt::Display;
use std::marker::PhantomData;

use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, SysCallReg, SyscallError, SyscallResult};
use crate::host::thread::ThreadId;
use crate::utility::time::TimeParts;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum FmtOptions {
    Standard,
    Deterministic,
}

// this type is required until we no longer need to access the format options from C
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

/// Convert from a `SysCallReg`.
pub trait TryFromSyscallReg
where
    Self: Sized,
{
    fn try_from_reg(reg: SysCallReg) -> Option<Self>;
}

/// Format trait for syscall data.
pub trait SyscallDataDisplay {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result;
}

/// Format trait for syscall pointers. Should be implemented on `SyscallPtr<*const T>` or
/// `SyscallPtr<[T; K]>` types.
pub trait SyscallPtrDisplay {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        options: FmtOptions,
        mem: &MemoryManager,
    ) -> std::fmt::Result;
}

impl<T: From<SysCallReg>> TryFromSyscallReg for T {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Some(Self::from(reg))
    }
}

/// A typed PluginPtr.
pub struct SyscallPtr<T> {
    pub ptr: PluginPtr,
    _phantom: PhantomData<T>,
}

impl<T> SyscallPtr<*const T> {
    pub fn new(ptr: PluginPtr) -> Self {
        Self {
            ptr,
            _phantom: PhantomData::default(),
        }
    }
}

impl<T, const K: usize> SyscallPtr<[T; K]> {
    pub fn new(ptr: PluginPtr) -> Self {
        Self {
            ptr,
            _phantom: PhantomData::default(),
        }
    }
}

impl<T> From<SysCallReg> for SyscallPtr<*const T> {
    fn from(reg: SysCallReg) -> Self {
        Self::new(PluginPtr::from(reg))
    }
}

impl<T, const K: usize> From<SysCallReg> for SyscallPtr<[T; K]> {
    fn from(reg: SysCallReg) -> Self {
        Self::new(PluginPtr::from(reg))
    }
}

/// A trait for objects that can be read from plugin memory.
pub trait FromSyscallMem<'a> {
    fn from_mem(reg: SysCallReg, options: FmtOptions, mem: &'a MemoryManager) -> Self;
}

/// A syscall value, either data or a pointer.
pub enum SyscallVal<'a, T> {
    Data(SysCallReg),
    Ptr(SyscallPtr<T>, FmtOptions, &'a MemoryManager),
}

impl<'a, T: TryFromSyscallReg> FromSyscallMem<'a> for SyscallVal<'a, T> {
    fn from_mem(reg: SysCallReg, _options: FmtOptions, _mem: &'a MemoryManager) -> Self {
        Self::Data(reg)
    }
}

impl<'a, T> FromSyscallMem<'a> for SyscallVal<'a, *const T> {
    fn from_mem(reg: SysCallReg, options: FmtOptions, mem: &'a MemoryManager) -> Self {
        Self::Ptr(SyscallPtr::from(reg), options, mem)
    }
}

impl<'a, T, const K: usize> FromSyscallMem<'a> for SyscallVal<'a, [T; K]> {
    fn from_mem(reg: SysCallReg, options: FmtOptions, mem: &'a MemoryManager) -> Self {
        Self::Ptr(SyscallPtr::from(reg), options, mem)
    }
}

impl<'a, T> Display for SyscallVal<'a, T>
where
    T: SyscallDataDisplay + TryFromSyscallReg,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Data(reg) => match T::try_from_reg(*reg) {
                Some(x) => x.fmt(f),
                // if the conversion to type T was unsuccessful, just show an integer
                None => write!(f, "{:#x} <invalid>", unsafe { reg.as_u64 }),
            },
            Self::Ptr(_, _, _) => unreachable!(),
        }
    }
}

impl<'a, T> Display for SyscallVal<'a, *const T>
where
    SyscallPtr<*const T>: SyscallPtrDisplay,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Data(_) => unreachable!(),
            Self::Ptr(x, options, mem) => x.fmt(f, *options, mem),
        }
    }
}

impl<'a, T, const K: usize> Display for SyscallVal<'a, [T; K]>
where
    SyscallPtr<[T; K]>: SyscallPtrDisplay,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Data(_) => unreachable!(),
            Self::Ptr(x, options, mem) => x.fmt(f, *options, mem),
        }
    }
}

/// A marker type for indicating there are no types left in the syscall arguments.
#[derive(Default)]
pub struct NoArg {}

impl SyscallDataDisplay for NoArg {
    fn fmt(&self, _f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        panic!("We shouldn't ever try to format this.");
    }
}

impl TryFromSyscallReg for NoArg {
    fn try_from_reg(_: SysCallReg) -> Option<Self> {
        Some(NoArg::default())
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
    SyscallVal<'a, A>: Display + FromSyscallMem<'a>,
    SyscallVal<'a, B>: Display + FromSyscallMem<'a>,
    SyscallVal<'a, C>: Display + FromSyscallMem<'a>,
    SyscallVal<'a, D>: Display + FromSyscallMem<'a>,
    SyscallVal<'a, E>: Display + FromSyscallMem<'a>,
    SyscallVal<'a, F>: Display + FromSyscallMem<'a>,
{
    pub fn new(args: &SysCallArgs, options: FmtOptions, mem: &'a MemoryManager) -> Self {
        Self {
            a: SyscallVal::from_mem(args.get(0), options, mem),
            b: SyscallVal::from_mem(args.get(1), options, mem),
            c: SyscallVal::from_mem(args.get(2), options, mem),
            d: SyscallVal::from_mem(args.get(3), options, mem),
            e: SyscallVal::from_mem(args.get(4), options, mem),
            f: SyscallVal::from_mem(args.get(5), options, mem),
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
                write!(f, "{}", arg)?;
                first = false;
            } else {
                write!(f, ", {}", arg)?;
            }
        }

        Ok(())
    }
}

/// A formatting wrapper for the syscall result.
pub struct SyscallResultFmt<'a, RV>
where
    SyscallVal<'a, RV>: Display + FromSyscallMem<'a>,
    RV: std::fmt::Debug,
{
    rv: &'a SyscallResult,
    options: FmtOptions,
    mem: &'a MemoryManager,
    _phantom: PhantomData<RV>,
}

impl<'a, RV> SyscallResultFmt<'a, RV>
where
    SyscallVal<'a, RV>: Display + FromSyscallMem<'a>,
    RV: std::fmt::Debug,
{
    pub fn new(rv: &'a SyscallResult, options: FmtOptions, mem: &'a MemoryManager) -> Option<Self> {
        match &rv {
            SyscallResult::Ok(_)
            | SyscallResult::Err(SyscallError::Failed(_))
            | SyscallResult::Err(SyscallError::Native) => Some(Self {
                rv,
                options,
                mem,
                _phantom: PhantomData::default(),
            }),
            // the syscall was not completed and will be re-run again later
            SyscallResult::Err(SyscallError::Blocked(_)) => None,
        }
    }
}

impl<'a, RV> Display for SyscallResultFmt<'a, RV>
where
    SyscallVal<'a, RV>: Display + FromSyscallMem<'a>,
    RV: std::fmt::Debug,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.rv {
            SyscallResult::Ok(x) => {
                let rv = SyscallVal::<'_, RV>::from_mem(*x, self.options, self.mem);
                write!(f, "{}", rv)
            }
            SyscallResult::Err(SyscallError::Failed(failed)) => {
                let errno: Errno = failed.errno;
                let rv = SysCallReg {
                    as_i64: -(errno as i64),
                };
                let rv = SyscallVal::<'_, RV>::from_mem(rv, self.options, self.mem);
                write!(f, "{} ({})", rv, errno)
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
    let sim_time = sim_time.fmt_hr_min_sec_milli();

    writeln!(
        writer,
        "{} [tid {}] {}({}) = {}",
        sim_time, tid, name, args, rv
    )
}

mod export {
    use super::*;
    use crate::core::worker::Worker;
    use crate::cshadow as c;
    use std::ffi::CStr;

    #[no_mangle]
    pub extern "C" fn log_syscall(
        proc: *mut c::Process,
        logging_mode: StraceFmtMode,
        tid: libc::pid_t,
        name: *const libc::c_char,
        args: *const libc::c_char,
        result: c::SysCallReturn,
    ) -> c::SysCallReturn {
        assert!(!proc.is_null());
        assert!(!name.is_null());
        assert!(!args.is_null());

        let name = unsafe { CStr::from_ptr(name) }.to_str().unwrap();
        let args = unsafe { CStr::from_ptr(args) }.to_str().unwrap();
        let result = SyscallResult::from(result);

        let logging_mode = logging_mode.into();
        let Some(logging_mode) = logging_mode else {
            // logging was disabled
            return result.into()
        };

        Worker::with_active_host(|host| {
            let proc = unsafe { c::process_getRustProcess(proc).as_ref().unwrap() };
            let proc = proc.borrow(host.root());

            // we don't know the type, so just show it as an int
            let memory = proc.memory_borrow();
            let rv = SyscallResultFmt::<libc::c_long>::new(&result, logging_mode, &memory);

            if let Some(ref rv) = rv {
                proc.with_strace_file(|file| {
                    let time = Worker::current_time();

                    if let (Some(time), Ok(tid)) = (time, tid.try_into()) {
                        write_syscall(file, &time, tid, name, args, rv).unwrap();
                    } else {
                        log::warn!(
                            "Could not log syscall {} with time {:?} and tid {:?}",
                            name,
                            time,
                            tid
                        );
                    }
                });
            }
        })
        .unwrap();

        // need to return the result, otherwise the drop impl will free the condition pointer
        result.into()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::process::Command;

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
        let _syscall_args = <SyscallArgsFmt>::new(&args, FmtOptions::Standard, &mem);

        proc.kill().unwrap();
        proc.wait().unwrap();
    }
}
