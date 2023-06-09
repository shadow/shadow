use crate::bindings;

// Manually translated from linux/sysinfo.h.
// bindgen incorrectly generates an IncompleteArrayField at the end.
#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct linux_sysinfo {
    /// Seconds since boot
    pub uptime: bindings::linux___kernel_long_t,
    /// 1, 5, and 15 minute load averages
    pub loads: [bindings::linux___kernel_ulong_t; 3],
    /// Total usable main memory size
    pub totalram: bindings::linux___kernel_ulong_t,
    /// Available memory size
    pub freeram: bindings::linux___kernel_ulong_t,
    /// Amount of shared memory
    pub sharedram: bindings::linux___kernel_ulong_t,
    /// Memory used by buffers
    pub bufferram: bindings::linux___kernel_ulong_t,
    /// Total swap space size
    pub totalswap: bindings::linux___kernel_ulong_t,
    /// swap space still available
    pub freeswap: bindings::linux___kernel_ulong_t,
    /// Number of current processes
    pub procs: bindings::linux___u16,
    /// Explicit padding for m68k
    pub pad: bindings::linux___u16,
    /// Total high memory size
    pub totalhigh: bindings::linux___kernel_ulong_t,
    /// Available high memory size
    pub freehigh: bindings::linux___kernel_ulong_t,
    /// Memory unit size in bytes
    pub mem_unit: bindings::linux___u32,
    /// Padding: libc5 uses this..
    //
    // Manually translated from
    // `char _f[20-2*sizeof(__kernel_ulong_t)-sizeof(__u32)];`
    pub l_f: [core::ffi::c_char;
        20 - 2 * core::mem::size_of::<bindings::linux___kernel_ulong_t>()
            - core::mem::size_of::<bindings::linux___u32>()],
}

#[allow(non_camel_case_types)]
pub type sysinfo = linux_sysinfo;
unsafe impl shadow_pod::Pod for sysinfo {}
