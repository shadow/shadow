# Overview

Today basic signal sending and delivery is implemented mostly by passing
everything through to native syscalls and signals in the kernel. This largely
works in ptrace mode, but not in preload mode. There are a few problems:

* There's an IPC state machine between Shadow and the shim. Naively sending the
  signal to the managed thread disrupts that state machine. This is one of the
  main problems we've observed in practice, and e.g.  why our current signal
  tests are disabled in preload mode
  (https://github.com/shadow/shadow/issues/1455). This may also cause breakage
  in golang's signal-based preemption mechanism
  (https://github.com/shadow/shadow/issues/1549#issuecomment-932606396).

* The shim itself installs signal handlers for SIGSEGV (to handle traps of the
  RDTSC instruction and emulate) and SIGSYS (to handle traps of the seccomp
  filter due to syscalls to be intercepted). These currently run on the
  thread's regular stack, which results in stack corruption in golang
  (https://github.com/shadow/shadow/issues/1549#issuecomment-1010220221).

  We also currently silently ignore attempts for the managed process to install
  their own handlers for these signals, though this hasn't yet caused problems
  in practice.

* Since the syscall being executed by the shim when the signal is received is
  part of the shim's syscall machinery, and not the original syscall that we're
  emulating (as it is in ptrace), the `SA_RESTART` flag won't cause the kernel
  to retry the original syscall for us. (We haven't seen this problem in
  practice, but that may only be because things already go wrong earlier).

My plan is to finish the current syscall/signal from Shadow's perspective,
while pushing a frame on the stack on the shim side. This is analagous to how
the Linux kernel handles signals.

Shadow will set the siginfo for the signal in shared memory, and send the normal
`SHD_SHIM_EVENT_SHMEM_COMPLETE` message. The shim will check for signals that
are pending and not blocked before returning the syscall return value.  When
there is one, it will store everything it needs to finish resolving the syscall
(either restarting it or returning an error code) as locals on the current
stack frame, call the handler, and then finish resolving the current syscall
(by returning the originally specified value, or retrying the syscall).

We *might* be able to save a little bit of emulation and shadowed data
structures by still sending a signal to the thread. In that case we would still
need some way of telling the the thread to prepare to receive a signal, but
instead of calling (or even knowing about) the handler itself, it could just
call something like `sigsuspend` to unmask and wait for the signal. I don't
think this would actually save that much complexity though. e.g. we would still
need to respect the simulated signal masks, either by fully tracking them
ourselves, or saving and restoring the real signal mask in the syscall handling
code (which would require a native syscall for each). We would also still need
to handle retrying the original syscall when appropriate, saving and restoring
interposition state etc. In the end I'm pretty sure this would be a wash at
best.

Most of this proposal focuses on getting signals working well in preload mode.
Since ptrace mode is likely to be dropped unless we find a compelling reason
not to, we won't invest effort in further improving the accuracy of its signal
handling. We will preserve the current functionality by continuing to delegate
to the native syscalls for signal handling in ptrace mode.

# Milestones

These don't necessarily map to milestones in shadow's project; it's just a
rough sequence of how to prioritize functionality.

* MS1: Run our handlers on an alternate signal stack and handle `sigaltstack`.
  It appears this may be enough for at least some small golang simulations,
  such as the one in #1549.
  * Use `sigaltstack` to run the shim's signal handlers on a dedicated stack.
    This prevents stack corruption in golang, which can migrate goroutine
    stacks between native threads. (I still don't 100% understand exactly how
    this results in stack corruption, but golang itself uses `sigaltstack` for
    presumably a similar reason, and I've confirmed that doing so for our own
    handlers fixes the current issue).
  * Prevent managed code calls to `sigaltstack` from actually making the native
    call. In particular on debian11-slim, glibc seems to do this sometimes.
    Running our own handlers on the specified stack *might* work, but they also
    drop the the `SS_AUTODISARM` flag when doing this, which definitely breaks
    our code. For this milestone it should be sufficient to mostly fake this
    syscall without actually doing much.
* MS2: Support running configured signal handlers in response to "standard" signals (1-31).
  * Support configuring handlers, signal masks, etc. in preload mode.  The
    calls and corresponding state will need to be emulated instead of being
    passed through to the kernel as they are now to prevent interfering with
    preload mode's own usage of signals and its communication state machine
    between shadow and the shim.
  * For syscalls that we don't implement for this milestone (e.g. `signalfd`),
    we should return an error rather than passing through as a native syscall.
    The former should make it relatively easy to identify the failure and see
    that we can implement the syscall. Continuing to do the latter may break
    things in more subtle ways.
* MS3: Deferred. These things aren't necessarily a lot more difficult, but it
  doesn't make sense to prioritize them without a concrete use case.
  * Realtime signals (numbers 32+). Not too difficult, but we don't know of any
    concrete use cases yet.
  * File-descriptor-based signal APIs. Not too difficult, but we don't know of
    any concrete use cases yet.
  * Syscalls assigned to this milestone below should return an error in MS2.

# Overview of code changes

## New event type

* Deliver signal(thread pointer, signal number, target=thread|process).
  Scheduled by syscalls such as `kill` and `tgkill`. Making
  this an event lets us "unwind the stack" of running those syscalls before
  beginning to handle the signal. When the event runs:
  * If the signal is no longer pending for the targeted thread, just return.
    (Not sure whether this can actually happen).
  * If the signal is now blocked by the current thread:
    * If the signal was process-targeted, and there is currently another thread
      without this signal blocked, reschedule this event for *that* thread.
    * Otherwise there is currently no eligible receiver; just return.
  * Call `thread_continue` for the given thread.

# New data structures

We'll tentatively keep per-thread signal state in the per-thread shared memory
block. I'm not sure yet whether this is necessary, but shouldn't hurt, and may
less us avoid some context switches between Shadow and the shim.

* signal mask bitfield: MS2. Per thread bitfield of masked signals.
  (`sigprocmask(2)`:  Each of the threads in a process has its own signal
  mask.)
* thread-signal pending siginfos: MS2. Array of `siginfo_t` (see
  `sigaction(2)`) of pending thread-directed signals.  (Alternatively, to save
  memory this could probably be a bit-field in memory, and then a dynamically
  allocated map from signal number to siginfo in Shadow's memory, which could
  then be sent over the IPC pipe in a "handle signal" message).
* run-state: MS3. 

Likewise the following state is per-process, and is probably best kept in a
per-process shared memory block:

* process-signal pending siginfos: MS2. array of `siginfo_t` (see
  `sigaction(2)`) of pending process-directed signals. (Or bitfield + map as
  for thread-signal pending siginfos).
* signal disposition table: MS2. per *process* table of signal number to `struct
  sigaction`, which includes disposition (`Term|Ign|Core|Stop|Cont`), pointer
  to handler, and flags. See `signal(7)`. As a flat array this will be
  large-ish (64 signals * ~36 bytes -> 2300 bytes).  Might be worth having
  shim-side only instead of in shared memory, so that this can start small and
  grow dynamically as needed, but some things will be easier and more efficient
  if this is available shadow-side as well. Hopefully the shared region won't
  spill to more than a page per process anyway.

In shim:

* altstack: MS2. `stack_t` struct that pecifies an alternate stack on which to run
  signal handlers. See `sigaltstack(2)`.

## In shim syscall handling loop

In the syscall handling loop, when a syscall result returns, check whether
there's also a deliverable signal pending by checking the process and
thread-level siginfos and masks. If so, look up disposition in sigaction table:

* Term: with interposition still disabled, use `sigaction` to change the native
  disposition of that signal to Term, and then use `tgkill` to send the current
  thread that signal. The OS will kill the process with that signal, which
  Shadow already detects and handles.
* Ign: do nothing.
* Core: as with `Term`, use the native signal disposition and `tgkill` to force real
  death by the signal.
* Stop: MS2: abort with error that this is unhandled. MS3: From the Shadow
  side, mark that the *process* is now in a 'stopped' state. There's more to
  work out here about the details of what that means and how a process gets out
  of that state.
* handler:
  * Ensure anything needed to restart the syscall later is saved locally on the
    stack (it should already be).
  * Save old signal mask (in a local variable on the stack).
  * Save sigaction's `sa_flags` for this signal locally on the stack. Need to
    refer to these during cleanup.
  * Adjust signal mask:
    * Add current signal being handled, unless `SA_NODEFER` is set in sigaction.
    * Merge `sa_mask` from sigaction.
  * If `SA_ONSTACK` is set in the sigaction and one has
    been set via `sigaltstack`
    * If the altstack was set with `SS_AUTODISARM`, save a copy of it locally and clear it.
    * Switch to the specified stack (we already have an example of how to do this in the shim).
  * Save the current value of `_shim_disable_interposition` and set it to 0.
    i.e. re-enable syscall interposition without unwinding the stack.
  * Copy the relevant `siginfo` to a local, and clear the one being serviced from
    the thread or process-level siginfo's.
  * Call the handler. (Check `SA_SIGINFO` to determine which field of the
    sigaction has the handler and what its function signature is.) 
  * Restore `_shim_disable_interposition`, disabling interposition.
  * Switch back to old stack if needed (probably just return from a wrapper
    function, which will return to the old stack if applicable). Also restore
    `altstack` if it was cleared via `SS_AUTODISARM`.
  * If `SA_RESETHAND` was set in the saved `sa_flags`, then reset sigaction for
    this signal to default.
  * If `SA_RESTART` was set in the saved `sa_flags` (or current flags?
    `sigaction(2)` doesn't specify which)
    * then re-attempt the syscall that was in progress. If the original syscall had
      relative timeouts, may need to adjust for time passed. See `restart_syscall(2)`.
    * else return the value specified by Shadow (typically `-EINTR`, but e.g. 0
      in the case of `kill` sending a signal to the current thread).

## In shim initialization

* MS1: Configure `sigaltstack` to run the shim's handlers on a stack owned by
  the shim.
* MS3: Install our own handlers for "synchronous" signals that may be raised
  natively while executing code.  These include `SIGBUS`, `SIGFPE`, and
  `SIGSEGV`. These handlers should call into the same core signal emulation
  code as used while accepting signals during a syscall, including switching
  the stack if necessary, etc. MS3 since the common case is that these signals
  are fatal anyway - either immediately or after a handler writes out some
  debugging information.

## In Thread

I don't think there's much code change in `Thread` or `ThreadPreload`, other than
initializing the per-thread data structures.

When a thread is in a blocking syscall and interrupted by a signal, ultimately
its `thread_continue` gets called, and it does the same as before - call the
sycall handler for the blocked syscall. It's up to
`syscallhandler_make_syscall` and/or the specific handlers to recognize that
there's a deliverable signal pending, clean up as needed, and return an
appropriate value for the current syscall (`-EINTR`).

## Signal-related syscall handlers

* `tgkill(2)`: MS2: Send a signal to a specific thread. If the specified thread
  already has a pending signal with the same signal number, do nothing.
  Otherwise write the pending signal info. If the target thread is not the one
  currently executing, and that thread doesn't currently have this signal
  blocked, schedule a "deliver signal" event for the target thread, for "now".

* `kill(2)`: MS2. Similar to `tgkill`, but sends a signal to a *process* or
  *process group*.  For each targeted process (which will currently only be 1
  since we don't track process groups):
  * If there's already a siginfo for this signal for this process, skip.
  * set the siginfo for this signal in this process.
  * If the process is the one currently running, and the signal isn't blocked
    in the current thread, continue to next process.
  * Iterate through threads in the process. If there's one without the signal
    blocked, schedule a "deliver signal" event for that thread.
  "process-signal pending siginfos", then iterate through threads until we find
  one that  hasn't masked that signal (if any) and schedule a "deliver signal"
  event for that thread.

* `pause(2)`: MS3. Suspends execution until any signal is caught. Basically want to
  return "blocked" from the signal handler and maybe set a flag on the thread.

* `pidfd_send_signal(2)`: MS3.

* `restart_syscall(2)`: MS3. Used by signal trampoline call to restart a syscall,
  updating time-related parameters if needed (for syscalls that specify a
  relative timeout). We shouldn't need to implement a handler for this syscall
  per se, but the man page is a good reference for what our equivalent
  shim-side code will need to take into account when restarting an interrupted
  system call.

* `rt_sigqueueinfo(2)`: MS3.

* `setitimer(2)`: MS3.

* `sigaction(2)`: Configure how a signal will be handled. This will manipulate
  the signal disposition table.

* `sgetmask(2)`: MS3. Deprecated version of `sigprocmask`.

* `sigaltstack(2)`: MS1 and MS2.
  Configures a stack for signal handlers to
  execute on (instead of just pushing a frame onto the current thread's regular
  stack), and returns information about the current configuration.
  * MS1: We need to stop passing through to the kernel, since this would
    interfere with the shim's `sigaltstack` configuration. We can probably get
    away with just faking the returned information about the current
    configuration for now.
  * MS2: Save the requested stack configuration and return it in subsequent
    calls. Actually switch to this stack when running the managed thread's
    signal handlers. We might be able to get away without this, but failures
    due to this functionality missing may be difficult to debug.
* `signal(2)`: MS3. Deprecated alternative to `sigaction`.
* `signalfd(2)`: MS3. Creates a file descriptor that can be used to listen for
  signals.
* `sigpending(2)`: MS3. Returns the set of signals pending for the thread
  (union of process-directed and thread-directed pending signals).
* `sigprocmask(2)`: MS2. Fetch and/or change calling thread's signal mask.
* `sigreturn(2)`: MS3. This is meant to be called by the kernel's/libc's signal
  handling code, which won't be invoked for emulated syscalls.
* `sigsuspend(2)`: MS3. Change signal mask, `pause(2)`, then restore signal mask.
* `sigtimedwait(2)`: MS3. Wait for a signal.
* `sigwaitinfo(2)`: MS3. Wait for a signal.

## Blocking-syscall handlers

When we enter `syscallhandler_make_syscall` with an unblocked signal already
pending, and a blocked syscall was in progress, we may need to do something to
cancel it to avoid losing data, and then we should ultimately return `-EINTR`.

If we don't do anything to cancel, will anything go wrong? e.g. if we don't
cancel a blocked read, and the thread is later continued when the file
descriptor becomes readable, hopefully we don't lose data?  I'd think this
would be the case assuming e.g. no data is copied nor the position in the
stream advanced until the "second half" of the syscall handler runs, which it
never would in this case. If so, then life is simple -
`syscallhandler_make_syscall` just returns `-EINTR` if there's an unblocked
signal pending on entry.

If not, then the next best option is if the base `SysCallHandler` has enough
information that we can generically cancel the current syscall from
`syscallhandler_make_syscall`. (This could also be desirable to avoid a
spurious wakeup later).

I could imagine a case where we need to still invoke the syscall-specific
handler, which would be responsible for recognizing that there's a pending
signal and cleaning up any pending state. ~Every blocking syscall handler would
need to be updated.

# New tests

Incomplete, but noting new tests we need as I think of them:

* Install a signal handler that mutates a global. `raise(3)` that signal. Check
  immediately afterwards that the global was mutated by the handler.
  (`raise(3)` guarantees that the handler has already run when `raise`
  returns).
* Install a signal handler that mutates a global. Block the signal with
  `sigprocmask`.  `raise(3)` that signal. Check that global wasn't mutated.
  Unblock the signal with `sigprocmask` - I *think* what we want to happen is
  that the signal handler runs, and then `sigprocmask` returns without error.
  Will need to verify that's what happens (or at least can happen) on Linux.
  * Same, but send the signal using `kill` or `tgkill` from another thread.
* Install a signal handler that increments a global counter and records the
  current thread id.  Start several threads that sleep in a loop. Use `kill` to
  send the signal to the process. Verify that the handler ran exactly once (it
  doesn't matter which thread ran it).
  * Same, but use `tgkill` and verify that the targeted thread ran it.
* Arrange for a `read` on a socket and/or pipe to be blocked. From another
  thread, unblock the operation (by writing to the other end), and then send a
  signal to *interrupt* the operation. No data should be lost. Either the
  operation is interrupted but subsequent reads correctly pick up where the
  last successful read left off, or the operation completes (and the signal is
  still handled). I'm not sure to what extent the latter is legal - e.g. when
  the target thread runs again is it legal to run the handler for the pending
  signal and then return successfully from the syscall?

# Questions/TODO

* What happens when to a thread-directed signal if that same signal is pending
  for the process? I'm guessing both could be delivered, potentially to the
  same thread, one via the process level siginfo and one via the thread level
  siginfo?

# Other notes

process-directed vs thread-directed signals in `signal(7)`:

> A process-directed signal is  one  that  is
targeted  at (and thus pending for) the process as a whole.  A signal may be
process-directed be‐ cause it was generated by the kernel for reasons other
than a hardware exception, or  because  it was  sent using  kill(2)  or
sigqueue(3).  A thread-directed signal is one that is targeted at a specific
thread.  A signal may be thread-directed because it was generated as  a
consequence  of executing  a  specific machine-language  instruction  that
triggered a hardware exception (e.g., SIGSEGV for an invalid memory access, or
SIGFPE for a math error), or because it was it was  tar‐ geted at a specific
thread using interfaces such as `tgkill(2)` or `pthread_kill(3)`.

> A process-directed signal may be delivered to any one of the threads that
does not currently have the signal blocked.  If more than one of the threads has the signal
unblocked,  then  the  kernel chooses an arbitrary thread to which to
deliver the signal.
