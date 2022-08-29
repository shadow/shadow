use crate::core::worker::Worker;
use crate::host::context::ThreadContext;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, SyscallResult, TypedPluginPtr};
use crate::utility::pod;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* info */ *const libc::sysinfo)]
    pub fn sysinfo(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        // Pointer to the plugin memory where we write the result.
        let info_ptr = TypedPluginPtr::new::<libc::sysinfo>(PluginPtr::from(args.get(0)), 1);

        // Seconds are needed for uptime.
        let seconds = Worker::current_time()
            .unwrap()
            .duration_since(&EmulatedTime::SIMULATION_START)
            .as_secs();

        // Get a zeroed struct to make sure we init all fields.
        let mut info = pod::zeroed::<libc::sysinfo>();

        // These values are chosen arbitrarily; we don't think it matters too
        // much, except to maintain determinism. For example, Tor make decisions
        // about how many circuits to allow to be open (and other OOM settings)
        // based on available memory.
        info.uptime = i64::try_from(seconds).unwrap_or(i64::MAX);
        info.loads[0] = 1;
        info.loads[1] = 1;
        info.loads[2] = 1;
        info.totalram = 32;
        info.freeram = 24;
        info.sharedram = 4;
        info.bufferram = 4;
        info.totalswap = 0;
        info.freeswap = 0;
        info.procs = 100;
        info.totalhigh = 4;
        info.freehigh = 3;
        info.mem_unit = 1024 * 1024 * 1024; // GiB

        // Write the result to plugin memory.
        ctx.process.memory_mut().copy_to_ptr(info_ptr, &[info])?;
        Ok(0.into())
    }
}
