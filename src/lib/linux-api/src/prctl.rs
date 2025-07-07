use linux_syscall::Result64;
use linux_syscall::syscall;

use crate::errno::Errno;
use crate::{bindings, const_conversions};

/// Options for `man 2 prctl`.
// We want to allow unknown values since newer kernel versions may add new options, and we may want
// to gracefully handle these options (for example to pass them on to the kernel). Linux also uses
// the same namespace ("PR_") for both options and argument flags (for example the `option`
// `PR_SET_FP_MODE` and the bitflag arguments `PR_FP_MODE_FR` and `PR_FP_MODE_FRE` in `arg2`). We
// don't want bitflags in this struct, so when adding new "PR_" entries, make sure they correspond
// with prctl "options" and not "arguments".
#[derive(PartialEq, Eq)]
pub struct PrctlOp(i32);

impl PrctlOp {
    pub const PR_SET_PDEATHSIG: Self = Self::from_u32(bindings::LINUX_PR_SET_PDEATHSIG);
    pub const PR_GET_PDEATHSIG: Self = Self::from_u32(bindings::LINUX_PR_GET_PDEATHSIG);
    pub const PR_GET_DUMPABLE: Self = Self::from_u32(bindings::LINUX_PR_GET_DUMPABLE);
    pub const PR_SET_DUMPABLE: Self = Self::from_u32(bindings::LINUX_PR_SET_DUMPABLE);
    pub const PR_GET_UNALIGN: Self = Self::from_u32(bindings::LINUX_PR_GET_UNALIGN);
    pub const PR_SET_UNALIGN: Self = Self::from_u32(bindings::LINUX_PR_SET_UNALIGN);
    pub const PR_GET_KEEPCAPS: Self = Self::from_u32(bindings::LINUX_PR_GET_KEEPCAPS);
    pub const PR_SET_KEEPCAPS: Self = Self::from_u32(bindings::LINUX_PR_SET_KEEPCAPS);
    pub const PR_GET_FPEMU: Self = Self::from_u32(bindings::LINUX_PR_GET_FPEMU);
    pub const PR_SET_FPEMU: Self = Self::from_u32(bindings::LINUX_PR_SET_FPEMU);
    pub const PR_GET_FPEXC: Self = Self::from_u32(bindings::LINUX_PR_GET_FPEXC);
    pub const PR_SET_FPEXC: Self = Self::from_u32(bindings::LINUX_PR_SET_FPEXC);
    pub const PR_GET_TIMING: Self = Self::from_u32(bindings::LINUX_PR_GET_TIMING);
    pub const PR_SET_TIMING: Self = Self::from_u32(bindings::LINUX_PR_SET_TIMING);
    pub const PR_SET_NAME: Self = Self::from_u32(bindings::LINUX_PR_SET_NAME);
    pub const PR_GET_NAME: Self = Self::from_u32(bindings::LINUX_PR_GET_NAME);
    pub const PR_GET_ENDIAN: Self = Self::from_u32(bindings::LINUX_PR_GET_ENDIAN);
    pub const PR_SET_ENDIAN: Self = Self::from_u32(bindings::LINUX_PR_SET_ENDIAN);
    pub const PR_GET_SECCOMP: Self = Self::from_u32(bindings::LINUX_PR_GET_SECCOMP);
    pub const PR_SET_SECCOMP: Self = Self::from_u32(bindings::LINUX_PR_SET_SECCOMP);
    pub const PR_CAPBSET_READ: Self = Self::from_u32(bindings::LINUX_PR_CAPBSET_READ);
    pub const PR_CAPBSET_DROP: Self = Self::from_u32(bindings::LINUX_PR_CAPBSET_DROP);
    pub const PR_GET_TSC: Self = Self::from_u32(bindings::LINUX_PR_GET_TSC);
    pub const PR_SET_TSC: Self = Self::from_u32(bindings::LINUX_PR_SET_TSC);
    pub const PR_GET_SECUREBITS: Self = Self::from_u32(bindings::LINUX_PR_GET_SECUREBITS);
    pub const PR_SET_SECUREBITS: Self = Self::from_u32(bindings::LINUX_PR_SET_SECUREBITS);
    pub const PR_SET_TIMERSLACK: Self = Self::from_u32(bindings::LINUX_PR_SET_TIMERSLACK);
    pub const PR_GET_TIMERSLACK: Self = Self::from_u32(bindings::LINUX_PR_GET_TIMERSLACK);
    pub const PR_TASK_PERF_EVENTS_DISABLE: Self =
        Self::from_u32(bindings::LINUX_PR_TASK_PERF_EVENTS_DISABLE);
    pub const PR_TASK_PERF_EVENTS_ENABLE: Self =
        Self::from_u32(bindings::LINUX_PR_TASK_PERF_EVENTS_ENABLE);
    pub const PR_MCE_KILL: Self = Self::from_u32(bindings::LINUX_PR_MCE_KILL);
    pub const PR_SET_MM: Self = Self::from_u32(bindings::LINUX_PR_SET_MM);
    pub const PR_MCE_KILL_GET: Self = Self::from_u32(bindings::LINUX_PR_MCE_KILL_GET);
    pub const PR_SET_PTRACER: Self = Self::from_u32(bindings::LINUX_PR_SET_PTRACER);
    pub const PR_SET_CHILD_SUBREAPER: Self = Self::from_u32(bindings::LINUX_PR_SET_CHILD_SUBREAPER);
    pub const PR_GET_CHILD_SUBREAPER: Self = Self::from_u32(bindings::LINUX_PR_GET_CHILD_SUBREAPER);
    pub const PR_SET_NO_NEW_PRIVS: Self = Self::from_u32(bindings::LINUX_PR_SET_NO_NEW_PRIVS);
    pub const PR_GET_NO_NEW_PRIVS: Self = Self::from_u32(bindings::LINUX_PR_GET_NO_NEW_PRIVS);
    pub const PR_GET_TID_ADDRESS: Self = Self::from_u32(bindings::LINUX_PR_GET_TID_ADDRESS);
    pub const PR_SET_THP_DISABLE: Self = Self::from_u32(bindings::LINUX_PR_SET_THP_DISABLE);
    pub const PR_GET_THP_DISABLE: Self = Self::from_u32(bindings::LINUX_PR_GET_THP_DISABLE);
    pub const PR_MPX_ENABLE_MANAGEMENT: Self =
        Self::from_u32(bindings::LINUX_PR_MPX_ENABLE_MANAGEMENT);
    pub const PR_MPX_DISABLE_MANAGEMENT: Self =
        Self::from_u32(bindings::LINUX_PR_MPX_DISABLE_MANAGEMENT);
    pub const PR_SET_FP_MODE: Self = Self::from_u32(bindings::LINUX_PR_SET_FP_MODE);
    pub const PR_GET_FP_MODE: Self = Self::from_u32(bindings::LINUX_PR_GET_FP_MODE);
    pub const PR_CAP_AMBIENT: Self = Self::from_u32(bindings::LINUX_PR_CAP_AMBIENT);
    pub const PR_SVE_SET_VL: Self = Self::from_u32(bindings::LINUX_PR_SVE_SET_VL);
    pub const PR_SVE_GET_VL: Self = Self::from_u32(bindings::LINUX_PR_SVE_GET_VL);
    pub const PR_GET_SPECULATION_CTRL: Self =
        Self::from_u32(bindings::LINUX_PR_GET_SPECULATION_CTRL);
    pub const PR_SET_SPECULATION_CTRL: Self =
        Self::from_u32(bindings::LINUX_PR_SET_SPECULATION_CTRL);
    pub const PR_PAC_RESET_KEYS: Self = Self::from_u32(bindings::LINUX_PR_PAC_RESET_KEYS);
    pub const PR_SET_TAGGED_ADDR_CTRL: Self =
        Self::from_u32(bindings::LINUX_PR_SET_TAGGED_ADDR_CTRL);
    pub const PR_GET_TAGGED_ADDR_CTRL: Self =
        Self::from_u32(bindings::LINUX_PR_GET_TAGGED_ADDR_CTRL);
    pub const PR_SET_IO_FLUSHER: Self = Self::from_u32(bindings::LINUX_PR_SET_IO_FLUSHER);
    pub const PR_GET_IO_FLUSHER: Self = Self::from_u32(bindings::LINUX_PR_GET_IO_FLUSHER);
    pub const PR_SET_SYSCALL_USER_DISPATCH: Self =
        Self::from_u32(bindings::LINUX_PR_SET_SYSCALL_USER_DISPATCH);
    pub const PR_PAC_SET_ENABLED_KEYS: Self =
        Self::from_u32(bindings::LINUX_PR_PAC_SET_ENABLED_KEYS);
    pub const PR_PAC_GET_ENABLED_KEYS: Self =
        Self::from_u32(bindings::LINUX_PR_PAC_GET_ENABLED_KEYS);
    pub const PR_SCHED_CORE: Self = Self::from_u32(bindings::LINUX_PR_SCHED_CORE);
    pub const PR_SME_SET_VL: Self = Self::from_u32(bindings::LINUX_PR_SME_SET_VL);
    pub const PR_SME_GET_VL: Self = Self::from_u32(bindings::LINUX_PR_SME_GET_VL);
    pub const PR_SET_MDWE: Self = Self::from_u32(bindings::LINUX_PR_SET_MDWE);
    pub const PR_GET_MDWE: Self = Self::from_u32(bindings::LINUX_PR_GET_MDWE);
    pub const PR_SET_VMA: Self = Self::from_u32(bindings::LINUX_PR_SET_VMA);
    pub const PR_GET_AUXV: Self = Self::from_u32(bindings::LINUX_PR_GET_AUXV);
    pub const PR_SET_MEMORY_MERGE: Self = Self::from_u32(bindings::LINUX_PR_SET_MEMORY_MERGE);
    pub const PR_GET_MEMORY_MERGE: Self = Self::from_u32(bindings::LINUX_PR_GET_MEMORY_MERGE);
    pub const PR_RISCV_V_SET_CONTROL: Self = Self::from_u32(bindings::LINUX_PR_RISCV_V_SET_CONTROL);
    pub const PR_RISCV_V_GET_CONTROL: Self = Self::from_u32(bindings::LINUX_PR_RISCV_V_GET_CONTROL);
    pub const PR_RISCV_SET_ICACHE_FLUSH_CTX: Self =
        Self::from_u32(bindings::LINUX_PR_RISCV_SET_ICACHE_FLUSH_CTX);
    pub const PR_PPC_GET_DEXCR: Self = Self::from_u32(bindings::LINUX_PR_PPC_GET_DEXCR);
    pub const PR_PPC_SET_DEXCR: Self = Self::from_u32(bindings::LINUX_PR_PPC_SET_DEXCR);
    // NOTE: only add prctl options here (not prctl args), and add new entries to `to_str` below

    pub const fn new(val: i32) -> Self {
        Self(val)
    }

    pub const fn val(&self) -> i32 {
        self.0
    }

    const fn from_u32(val: u32) -> Self {
        Self::new(const_conversions::i32_from_u32(val))
    }

    pub const fn to_str(&self) -> Option<&'static str> {
        match *self {
            Self::PR_SET_PDEATHSIG => Some("PR_SET_PDEATHSIG"),
            Self::PR_GET_PDEATHSIG => Some("PR_GET_PDEATHSIG"),
            Self::PR_GET_DUMPABLE => Some("PR_GET_DUMPABLE"),
            Self::PR_SET_DUMPABLE => Some("PR_SET_DUMPABLE"),
            Self::PR_GET_UNALIGN => Some("PR_GET_UNALIGN"),
            Self::PR_SET_UNALIGN => Some("PR_SET_UNALIGN"),
            Self::PR_GET_KEEPCAPS => Some("PR_GET_KEEPCAPS"),
            Self::PR_SET_KEEPCAPS => Some("PR_SET_KEEPCAPS"),
            Self::PR_GET_FPEMU => Some("PR_GET_FPEMU"),
            Self::PR_SET_FPEMU => Some("PR_SET_FPEMU"),
            Self::PR_GET_FPEXC => Some("PR_GET_FPEXC"),
            Self::PR_SET_FPEXC => Some("PR_SET_FPEXC"),
            Self::PR_GET_TIMING => Some("PR_GET_TIMING"),
            Self::PR_SET_TIMING => Some("PR_SET_TIMING"),
            Self::PR_SET_NAME => Some("PR_SET_NAME"),
            Self::PR_GET_NAME => Some("PR_GET_NAME"),
            Self::PR_GET_ENDIAN => Some("PR_GET_ENDIAN"),
            Self::PR_SET_ENDIAN => Some("PR_SET_ENDIAN"),
            Self::PR_GET_SECCOMP => Some("PR_GET_SECCOMP"),
            Self::PR_SET_SECCOMP => Some("PR_SET_SECCOMP"),
            Self::PR_CAPBSET_READ => Some("PR_CAPBSET_READ"),
            Self::PR_CAPBSET_DROP => Some("PR_CAPBSET_DROP"),
            Self::PR_GET_TSC => Some("PR_GET_TSC"),
            Self::PR_SET_TSC => Some("PR_SET_TSC"),
            Self::PR_GET_SECUREBITS => Some("PR_GET_SECUREBITS"),
            Self::PR_SET_SECUREBITS => Some("PR_SET_SECUREBITS"),
            Self::PR_SET_TIMERSLACK => Some("PR_SET_TIMERSLACK"),
            Self::PR_GET_TIMERSLACK => Some("PR_GET_TIMERSLACK"),
            Self::PR_TASK_PERF_EVENTS_DISABLE => Some("PR_TASK_PERF_EVENTS_DISABLE"),
            Self::PR_TASK_PERF_EVENTS_ENABLE => Some("PR_TASK_PERF_EVENTS_ENABLE"),
            Self::PR_MCE_KILL => Some("PR_MCE_KILL"),
            Self::PR_SET_MM => Some("PR_SET_MM"),
            Self::PR_SET_PTRACER => Some("PR_SET_PTRACER"),
            Self::PR_SET_CHILD_SUBREAPER => Some("PR_SET_CHILD_SUBREAPER"),
            Self::PR_GET_CHILD_SUBREAPER => Some("PR_GET_CHILD_SUBREAPER"),
            Self::PR_SET_NO_NEW_PRIVS => Some("PR_SET_NO_NEW_PRIVS"),
            Self::PR_GET_NO_NEW_PRIVS => Some("PR_GET_NO_NEW_PRIVS"),
            Self::PR_GET_TID_ADDRESS => Some("PR_GET_TID_ADDRESS"),
            Self::PR_SET_THP_DISABLE => Some("PR_SET_THP_DISABLE"),
            Self::PR_GET_THP_DISABLE => Some("PR_GET_THP_DISABLE"),
            Self::PR_MPX_ENABLE_MANAGEMENT => Some("PR_MPX_ENABLE_MANAGEMENT"),
            Self::PR_MPX_DISABLE_MANAGEMENT => Some("PR_MPX_DISABLE_MANAGEMENT"),
            Self::PR_SET_FP_MODE => Some("PR_SET_FP_MODE"),
            Self::PR_GET_FP_MODE => Some("PR_GET_FP_MODE"),
            Self::PR_CAP_AMBIENT => Some("PR_CAP_AMBIENT"),
            Self::PR_SVE_SET_VL => Some("PR_SVE_SET_VL"),
            Self::PR_SVE_GET_VL => Some("PR_SVE_GET_VL"),
            Self::PR_GET_SPECULATION_CTRL => Some("PR_GET_SPECULATION_CTRL"),
            Self::PR_SET_SPECULATION_CTRL => Some("PR_SET_SPECULATION_CTRL"),
            Self::PR_PAC_RESET_KEYS => Some("PR_PAC_RESET_KEYS"),
            Self::PR_SET_TAGGED_ADDR_CTRL => Some("PR_SET_TAGGED_ADDR_CTRL"),
            Self::PR_GET_TAGGED_ADDR_CTRL => Some("PR_GET_TAGGED_ADDR_CTRL"),
            Self::PR_SET_IO_FLUSHER => Some("PR_SET_IO_FLUSHER"),
            Self::PR_GET_IO_FLUSHER => Some("PR_GET_IO_FLUSHER"),
            Self::PR_SET_SYSCALL_USER_DISPATCH => Some("PR_SET_SYSCALL_USER_DISPATCH"),
            Self::PR_PAC_SET_ENABLED_KEYS => Some("PR_PAC_SET_ENABLED_KEYS"),
            Self::PR_PAC_GET_ENABLED_KEYS => Some("PR_PAC_GET_ENABLED_KEYS"),
            Self::PR_SCHED_CORE => Some("PR_SCHED_CORE"),
            Self::PR_SME_SET_VL => Some("PR_SME_SET_VL"),
            Self::PR_SME_GET_VL => Some("PR_SME_GET_VL"),
            Self::PR_SET_MDWE => Some("PR_SET_MDWE"),
            Self::PR_GET_MDWE => Some("PR_GET_MDWE"),
            Self::PR_SET_VMA => Some("PR_SET_VMA"),
            Self::PR_GET_AUXV => Some("PR_GET_AUXV"),
            Self::PR_SET_MEMORY_MERGE => Some("PR_SET_MEMORY_MERGE"),
            Self::PR_GET_MEMORY_MERGE => Some("PR_GET_MEMORY_MERGE"),
            Self::PR_RISCV_V_SET_CONTROL => Some("PR_RISCV_V_SET_CONTROL"),
            Self::PR_RISCV_V_GET_CONTROL => Some("PR_RISCV_V_GET_CONTROL"),
            Self::PR_RISCV_SET_ICACHE_FLUSH_CTX => Some("PR_RISCV_SET_ICACHE_FLUSH_CTX"),
            Self::PR_PPC_GET_DEXCR => Some("PR_PPC_GET_DEXCR"),
            Self::PR_PPC_SET_DEXCR => Some("PR_PPC_SET_DEXCR"),
            _ => None,
        }
    }
}

impl core::fmt::Display for PrctlOp {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match self.to_str() {
            Some(s) => formatter.write_str(s),
            None => write!(formatter, "(unknown prctl option {})", self.0),
        }
    }
}

impl core::fmt::Debug for PrctlOp {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match self.to_str() {
            Some(s) => write!(formatter, "PrctlOp::{s}"),
            None => write!(formatter, "PrctlOp::<{}>", self.0),
        }
    }
}

impl From<PrctlOp> for i32 {
    #[inline]
    fn from(val: PrctlOp) -> Self {
        val.0
    }
}

impl From<i32> for PrctlOp {
    #[inline]
    fn from(val: i32) -> Self {
        Self::new(val)
    }
}

#[derive(PartialEq, Eq)]
pub struct ArchPrctlOp(i32);

impl ArchPrctlOp {
    pub const ARCH_SET_CPUID: Self = Self::from_u32(bindings::LINUX_ARCH_SET_CPUID);
    pub const ARCH_GET_CPUID: Self = Self::from_u32(bindings::LINUX_ARCH_GET_CPUID);
    pub const ARCH_SET_FS: Self = Self::from_u32(bindings::LINUX_ARCH_SET_FS);
    pub const ARCH_GET_FS: Self = Self::from_u32(bindings::LINUX_ARCH_GET_FS);
    pub const ARCH_SET_GS: Self = Self::from_u32(bindings::LINUX_ARCH_SET_GS);
    pub const ARCH_GET_GS: Self = Self::from_u32(bindings::LINUX_ARCH_GET_GS);
    pub const ARCH_GET_XCOMP_SUPP: Self = Self::from_u32(bindings::LINUX_ARCH_GET_XCOMP_SUPP);
    pub const ARCH_GET_XCOMP_PERM: Self = Self::from_u32(bindings::LINUX_ARCH_GET_XCOMP_PERM);
    pub const ARCH_REQ_XCOMP_PERM: Self = Self::from_u32(bindings::LINUX_ARCH_REQ_XCOMP_PERM);
    pub const ARCH_GET_XCOMP_GUEST_PERM: Self =
        Self::from_u32(bindings::LINUX_ARCH_GET_XCOMP_GUEST_PERM);
    pub const ARCH_REQ_XCOMP_GUEST_PERM: Self =
        Self::from_u32(bindings::LINUX_ARCH_REQ_XCOMP_GUEST_PERM);
    pub const ARCH_XCOMP_TILECFG: Self = Self::from_u32(bindings::LINUX_ARCH_XCOMP_TILECFG);
    pub const ARCH_XCOMP_TILEDATA: Self = Self::from_u32(bindings::LINUX_ARCH_XCOMP_TILEDATA);
    pub const ARCH_MAP_VDSO_X32: Self = Self::from_u32(bindings::LINUX_ARCH_MAP_VDSO_X32);
    pub const ARCH_MAP_VDSO_32: Self = Self::from_u32(bindings::LINUX_ARCH_MAP_VDSO_32);
    pub const ARCH_MAP_VDSO_64: Self = Self::from_u32(bindings::LINUX_ARCH_MAP_VDSO_64);
    pub const ARCH_GET_UNTAG_MASK: Self = Self::from_u32(bindings::LINUX_ARCH_GET_UNTAG_MASK);
    pub const ARCH_ENABLE_TAGGED_ADDR: Self =
        Self::from_u32(bindings::LINUX_ARCH_ENABLE_TAGGED_ADDR);
    pub const ARCH_GET_MAX_TAG_BITS: Self = Self::from_u32(bindings::LINUX_ARCH_GET_MAX_TAG_BITS);
    pub const ARCH_FORCE_TAGGED_SVA: Self = Self::from_u32(bindings::LINUX_ARCH_FORCE_TAGGED_SVA);
    pub const ARCH_SHSTK_ENABLE: Self = Self::from_u32(bindings::LINUX_ARCH_SHSTK_ENABLE);
    pub const ARCH_SHSTK_DISABLE: Self = Self::from_u32(bindings::LINUX_ARCH_SHSTK_DISABLE);
    pub const ARCH_SHSTK_LOCK: Self = Self::from_u32(bindings::LINUX_ARCH_SHSTK_LOCK);
    pub const ARCH_SHSTK_UNLOCK: Self = Self::from_u32(bindings::LINUX_ARCH_SHSTK_UNLOCK);
    pub const ARCH_SHSTK_STATUS: Self = Self::from_u32(bindings::LINUX_ARCH_SHSTK_STATUS);
    pub const ARCH_SHSTK_SHSTK: Self = Self::from_u32(bindings::LINUX_ARCH_SHSTK_SHSTK);
    pub const ARCH_SHSTK_WRSS: Self = Self::from_u32(bindings::LINUX_ARCH_SHSTK_WRSS);

    pub const fn new(val: i32) -> Self {
        Self(val)
    }

    const fn from_u32(val: u32) -> Self {
        Self::new(const_conversions::i32_from_u32(val))
    }
}

impl From<ArchPrctlOp> for core::ffi::c_int {
    fn from(value: ArchPrctlOp) -> Self {
        value.0
    }
}

/// Execute the `arch_prctl` syscall.
///
/// # Safety
///
/// Some operations may change OS behavior in a way that violates assumptions
/// that other code relies on.
pub unsafe fn arch_prctl_raw(
    option: core::ffi::c_int,
    arg2: core::ffi::c_ulong,
) -> Result<i64, Errno> {
    unsafe { syscall!(linux_syscall::SYS_arch_prctl, option, arg2) }
        .try_i64()
        .map_err(Errno::from)
}

/// Execute the `arch_prctl` syscall.
///
/// # Safety
///
/// Some operations may change OS behavior in a way that violates assumptions
/// that other code relies on.
pub unsafe fn arch_prctl(option: ArchPrctlOp, arg2: u64) -> Result<i64, Errno> {
    unsafe { arch_prctl_raw(option.into(), arg2) }
}
