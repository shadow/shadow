use linux_api::ucontext::{sigcontext, ucontext};
use shadow_shim_helper_rs::shim_event::ShimEventAddThreadReq;
use shadow_shmem::allocator::ShMemBlockSerialized;

/// Used below to validate the offset of `field` from `base`.
/// TODO: replace with `core::ptr::offset_of` once stabilized.
/// https://github.com/rust-lang/rust/issues/106655
fn sigcontext_offset_of(base: &sigcontext, field: &u64) -> usize {
    let base = base as *const _ as usize;
    let field = field as *const _ as usize;
    field - base
}

/// Round `ptr` down to a value that has alignment `align`. Useful when
/// allocating on a stack that grows downward.
///
/// Panics if `align` isn't a power of 2.
///
/// # Safety
///
/// The resulting aligned pointer must be part of the same allocation as `ptr`.
/// e.g. the stack that `ptr` points into must have enough room remaining to do
/// the alignment.
unsafe fn align_down(ptr: *mut u8, align: usize) -> *mut u8 {
    assert!(align.is_power_of_two());
    // Mask off enough low-order bits to ensure proper alignment.
    let ptr = ptr as usize;
    let ptr = ptr & !(align - 1);
    ptr as *mut u8
}

/// Helper for `do_clone`. Restores all general purpose registers, stack pointer,
/// and instruction pointer from `ctx` except for `rax`, which is set to 0.
///
/// # Safety
///
/// `ctx` must be safe to restore.
///
/// This is difficult to characterize in a general sense, but e.g. minimally the
/// stack and instruction pointers must be valid, and other register values must
/// correspond to "sound" values of whatever state they correspond to at that
/// instruction pointer.
unsafe extern "C" fn set_context(ctx: &sigcontext) -> ! {
    // These offsets are hard-coded into the asm format string below.
    // TODO: turn these into const parameters to the asm block when const
    // asm parameters are stabilized.
    // https://github.com/rust-lang/rust/issues/93332
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r8), 0);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r9), 0x8);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r10), 0x10);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r11), 0x18);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r12), 0x20);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r13), 0x28);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r14), 0x30);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.r15), 0x38);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.rsi), 0x48);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.rdi), 0x40);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.rbx), 0x58);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.rdx), 0x60);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.rbp), 0x50);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.rip), 0x80);
    debug_assert_eq!(sigcontext_offset_of(ctx, &ctx.rsp), 0x78);

    unsafe {
        core::arch::asm!(
            // Restore general purpose registers.
            // Offsets are validated in assertions above.
            "mov r8, [rax+0x0]",
            "mov r9, [rax+0x8]",
            "mov r10, [rax+0x10]",
            "mov r11, [rax+0x18]",
            "mov r12, [rax+0x20]",
            "mov r13, [rax+0x28]",
            "mov r14, [rax+0x30]",
            "mov r15, [rax+0x38]",
            "mov rsi, [rax+0x48]",
            "mov rdi, [rax+0x40]",
            "mov rbx, [rax+0x58]",
            "mov rdx, [rax+0x60]",
            "mov rbp, [rax+0x50]",
            "mov rsp, [rax+0x78]",

            // Push `ctx`'s `rip` to stack
            "mov rax, [rax+0x80]",
            "push rax",

            // Not restored:
            // - `rax`: stores the result of the syscall, which we set below.
            // - Floating point and other special registers: hopefully not needed.

            // Set `rax` to 0
            "mov rax, 0",

            // Ret to ctx's `rip`
            "ret",
            in("rax") ctx as *const _,
            options(noreturn)
        )
    };
}

/// `extern "C"` wrapper for `crate::tls_ipc::set`, which we can call from
/// assembly.
///
/// # Safety
///
/// `blk` must contained a serialized block of
/// type `IPCData`, which outlives the current thread.
unsafe extern "C" fn tls_ipc_set(blk: *const ShMemBlockSerialized) {
    let blk = unsafe { blk.as_ref().unwrap() };
    let prev = crate::global_allow_native_syscalls::swap(true);

    // SAFETY: ensured by caller
    unsafe { crate::tls_ipc::set(blk) };

    crate::global_allow_native_syscalls::swap(prev);
}

/// Execute a native `clone` syscall. The newly created child thread will
/// resume execution from `ctx`, which should be the point where the managed
/// code originally made a `clone` syscall (but was intercepted by seccomp).
///
/// # Safety
///
/// * `ctx` must be dereferenceable, and must be safe for the newly spawned
/// child thread to restore.
/// * Other pointers, if non-null, must be safely dereferenceable.
/// * `child_stack` must be "sufficiently big" for the child thread to run on.
/// * `tls` if provided must point to correctly initialized thread local storage.
pub unsafe fn do_clone(ctx: &ucontext, event: &ShimEventAddThreadReq) -> i64 {
    let flags = event.flags;
    let ptid: *mut i32 = event.ptid.cast::<i32>().into_raw_mut();
    let ctid: *mut i32 = event.ctid.cast::<i32>().into_raw_mut();
    let child_stack: *mut u8 = event.child_stack.cast::<u8>().into_raw_mut();
    let newtls = event.newtls;

    assert!(
        !child_stack.is_null(),
        "clone without a new stack not implemented"
    );

    // x86-64 calling conventions require a 16-byte aligned stack
    assert_eq!(
        child_stack.align_offset(16),
        0,
        "clone with unaligned new stack not implemented"
    );

    // Copy ctx to top of the child stack.
    // SAFETY: Should still point within stack, assuming it fits.
    let child_current_rsp = unsafe { child_stack.sub(core::mem::size_of::<sigcontext>()) };
    let child_current_rsp =
        unsafe { align_down(child_current_rsp, core::mem::align_of::<sigcontext>()) };
    let child_sigcontext = child_current_rsp.cast::<sigcontext>();
    unsafe { core::ptr::write(child_sigcontext, ctx.uc_mcontext) };

    // Update child's copy of context to use the child's stack.
    let child_sigctx = unsafe { child_sigcontext.as_mut().unwrap() };
    child_sigctx.rsp = child_stack as u64;

    // Copy child's IPC block to child's stack
    let child_current_rsp =
        unsafe { child_current_rsp.sub(core::mem::size_of::<ShMemBlockSerialized>()) };
    let child_current_rsp = unsafe {
        align_down(
            child_current_rsp,
            core::mem::align_of::<ShMemBlockSerialized>(),
        )
    };
    let child_ipc_blk = child_current_rsp.cast::<ShMemBlockSerialized>();
    unsafe { core::ptr::write(child_ipc_blk, event.ipc_block) };

    // Ensure stack is 16-aligned so that we can safely make function calls.
    let child_current_rsp = unsafe { align_down(child_current_rsp, 16) };

    let rv: i64;
    // SAFETY:
    //
    // This block makes the clone syscall, which is tricky because Rust currently
    // doesn't have a way to tell the compiler that a block or function "returns twice".
    // <https://github.com/rust-lang/libc/issues/1596>
    //
    // We work around this by using a single asm block to:
    // * Make the `clone` syscall
    // * Do the required per-thread shim initialization
    // * Restore CPU state and *jump* to the point where the managed code was
    // originally trying to make the syscall.
    //
    // The point we jump to should already be a point that was expecting to make
    // the clone syscall, so should already correctly handle that both the
    // parent and child thread resume execution there. (The parent thread
    // resumes execution there after returning from the seccomp signal handler
    // normally).
    unsafe {
        core::arch::asm!(
            // Make the clone syscall
            "syscall",
            // If in the parent, exit the asm block (by jumping forward to the label
            // `2`). https://doc.rust-lang.org/rust-by-example/unsafe/asm.html#labels
            "cmp rax, 0",
            "jne 2f",

            // Initialize the IPC block for this thread
            "mov rdi, {blk}",
            "call {tls_ipc_set}",

            // Initialize state for this thread
            "call {shim_init_thread}",

            // Set CPU state from ctx
            "mov rdi, r12",
            "call {set_context}",

            "2:",
            // clone syscall number in, rv out
            inout("rax") libc::SYS_clone => rv,
            // clone syscall arg1
            in("rdi") flags,
            // clone syscall arg2
            in("rsi") child_current_rsp,
            // clone syscall arg3
            in("rdx") ptid,
            // clone syscall arg4
            in("r10") ctid,
            // clone syscall arg5
            in("r8") newtls,
            blk = in(reg) child_ipc_blk,
            tls_ipc_set = sym tls_ipc_set,
            shim_init_thread = sym crate::init_thread,
            // callee-saved register
            in("r12") child_sigcontext as * const _,
            set_context = sym set_context,
        )
    }
    rv
}

pub mod export {
    use super::*;

    /// Execute a native `clone` syscall. The newly created child thread will
    /// resume execution from `ctx`, which should be the point where the managed
    /// code originally made a `clone` syscall (but was intercepted by seccomp).
    ///
    /// # Safety
    ///
    /// * `ctx` must be dereferenceable, and must be safe for the newly spawned
    /// child thread to restore.
    /// * Other pointers, if non-null, must be safely dereferenceable.
    /// * `child_stack` must be "sufficiently big" for the child thread to run on.
    /// * `tls` if provided must point to correctly initialized thread local storage.
    #[no_mangle]
    pub unsafe extern "C" fn shim_do_clone(
        ctx: *const libc::ucontext_t,
        event: *const ShimEventAddThreadReq,
    ) -> i64 {
        assert!(
            !ctx.is_null(),
            "clone without signal ucontext unimplemented"
        );
        // Cast from libc to linux kernel context. These are compatible in practice.
        // TODO: Change calling code to use linux kernel context.
        let ctx = ctx.cast::<linux_api::ucontext::ucontext>();
        let ctx = unsafe { ctx.as_ref().unwrap() };

        let event = unsafe { event.as_ref().unwrap() };

        unsafe { do_clone(ctx, event) }
    }
}
