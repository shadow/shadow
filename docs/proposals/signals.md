# Milestones

* MS1: Enough to support golang's signal-based preemptive scheduling. In
  particular, support the sample program in #1549.
  * `rt_sigaction` syscall: Used to set a signal handler, particularly for `SIGURG`. Example: `rt_sigaction(SIGURG, {sa_handler=0x46e360, sa_mask=~[RTMIN RT_1], sa_flags=SA_RESTORER|SA_ONSTACK|SA_RESTART|SA_SIGINFO, sa_restorer=0x7fafd720a3c0}, NULL, 8)`
  * `sigaltstack` syscall: Used to configure an alternate stack on which to run
    signal handlers. Experimentally, a simple golang program configures its
    handlers to use an alt stack. It calls this call to set up the flag, and
    uses the `SA_ONSTACK` flag in the individual calls to `rt_sigaction`. It
    *might* work without actually switching stacks, but I suspect it has
    something to do with not breaking its user space threads, and should be
    easy to implement.
  * `tgkill` syscall needs to deliver a signal to the configured signal
    handler.
  * Configure signal mask via `rt_sigprocmask`.
* Beyond minimum, but worth doing in the near term (because easy to implement,
  likely to be used, or both).
  * Allow configuration and use of alternate signal handler stack
    (`sigaltstack`).
* 
  * Realtime signals.
  * fd-based signal APIs.

# Signals

## Relevant syscalls and how to handle them

* `kill(2)`. Send a signal to a *process* or *process group*. For each targeted
  process set the bit in the "process-signal pending bitfield", then iterate
  through threads until we find one that  hasn't masked that signal (if any)
  and schedule that thread to run. This will need to be a new event type that
  includes the signal number (and maybe a signals-handled counter...?), in case
  an already scheduled event runs the thread and handles the signal.

* `pause(2)`: suspends execution until any signal is caught. Basically want to
  return "blocked" from the signal handler and maybe set a flag on the thread.

* `pidfd_send_signal(2)`: can skip for MVP.

* `restart_syscall(2)`: Used by signal trampoline call to restart a syscall,
  updating time-related parameters if needed (for syscalls that specify a
  relative timeout). We shouldn't need to implement a handler for this syscall
  per se, but the man page is a good reference for what our equivalent
  shim-side code will need to take into account when restarting an interrupted
  system call.

* `sigaltstack(2)`: 
* `signal(2)`
* `signalfd(2)`
* `sigpending(2)`: Returns the set of signals pending for the thread (union of
  process-directed and thread-directed pending signals). Can probably be
  handled completely shim-side.
* `sigprocmask(2)`
* `sigreturn(2)`
* `sigsuspend(2)`: change signal mask, `pause(2)`, then restore signal mask.
  We'll need to save the old signal mask somewhere (Thread?).
* `sigtimedwait(2)`:
* `sigwaitinfo(2)`:
* `wait(2)`

## Other man pages
* `killpg(3)`
* `raise(3)`
* `siginterrupt(3)`
* `sigqueue(3)`
* `sigsetops(3)`
* `sigvec(3)`
* `signal(7)`
* `sigwait(3)`
* `tkill`: See `tgkill`.
* `tgkill`.

## New data structures

In per-thread shared memory:
* signal mask bitfield: per thread bitfield of masked signals. (`sigprocmask(2)`:  Each of the threads in
  a process has its own signal mask.)
* thread-signal pending siginfos: array of `siginfo_t` (see `sigaction(2)`) of
  pending thread-directed signals.  (Alternatively, to save memory this could
  probably be a bit-field in memory, and then a dynamically allocated map from
  signal number to siginfo in Shadow's memory, which could then be sent over
  the IPC pipe in the "handle signal" message).

In per-process shared memory (do we have such a thing now?):
* process-signal pending siginfos: array of `siginfo_t` (see `sigaction(2)`) of
  pending process-directed signals. (Or bitfield
+ map as for thread-signal pending siginfos).
* signal disposition table: per *process* table of signal number to `struct
  sigaction`, which includes disposition (`Term|Ign|Core|Stop|Cont`), pointer
  to handler, and flags. See `signal(7)`. As a flat array this will be
  large-ish (64 signals * ~36 bytes -> 2300 bytes).  Might be worth having
  shim-side only so that this can start small and grow dynamically as needed,
  but some things will be easier and more efficient if this is available
  shadow-side as well. Hopefully the shared region won't spill to more than a
  page per process anyway.

In shim:
* altstack: `stack_t` struct that pecifies an alternate stack on which to run
  signal handlers. See `sigaltstack(2)`.

## New code

### In shim

Handle a new syscall response over pipe "Handle signal". Shadow side will have already
checked the mask, so shim delivers it unconditionally. Look up disposition in sigaction table:

* Term: with interposition still disabled, use `kill` to send a real `SIGKILL`
  to self. `SIGKILL` is unblockable.
* Ign: do nothing
* Core: disable interposition and use `kill` to send a real `SIGABRT`
  to self. Relies on shadow to have not allowed default disposition of
  `SIGABRT` to have been changed. (Or set the disposition ourselves before
  calling `kill` to be sure).
* Stop: Send a Stop message to shadow, then wait for a Continue message from
  Shadow.
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
  * Call the handler. (Check `SA_SIGINFO` to determine which field of the
    sigaction has the handler and what its function signature is.) 
  * Switch back to old stack if needed (probably just return from a wrapper
    function, which will return to the old stack if applicable). Also restore
    `altstack` if it was cleared via `SS_AUTODISARM`.
  * If `SA_RESETHAND` was set in the saved `sa_flags`, then reset sigaction for
    this signal to default.
  * If `SA_RESTART` was set in the saved `sa_flags` (or current flags?
    `sigaction(2)` doesn't specify which), and we got here via a signal
    delivered while handling a syscall (vs. e.g. a SIGSEGV delivered
    synchronously and handled via our shim handler instead of via a shadow
    message)
    * then re-attempt the syscall that was in progress. If the original syscall had
      relative timeouts, may need to adjust for time passed. See `restart_syscall(2)`.
    * else return `-EINTR`

### New messages from shim to shadow

* Stop: Used by shim to handle a signal whose disposition is Stop. On the
  shadow side, just return from executing the thread without scheduling another
  event to run the thread. Do we need to set a flag to explicitly mark that
  the thread is in a stopped state? Might be useful for debugging if nothing
  else.

### New messages from shadow to shim

* Handle signal: Used by shadow to deliver a signal. Most of the logic for
  handling the signal will live shim-side.
* Continue: 

### Delivering a signal.

### Receiving a signal

## Questions/TODO

* Whether and how to handle the `Stop` and `Cont` dispositions (see `signal(7)`). Dispositions 
* How to handle `disable_interposition` counter when recursing into signal
  handler? I think we need to add a way to fetch and restore it.

* What's supposed to happen to a stopped process if it receives a signal with a
  disposition other than Cont? More broadly, what all can get a process out of
  the stopped state?

* Do we need to do anything with the `SA_RESTORER` flag or `sa_restorer` field in `struct sigaction`? I *think* we don't,
unless there's some particular use of it to use for us. According to
`sigreturn(2)`: "on contemporary Linux systems, depending on the architecture,
the signal trampoline  code  lives  either  in  the vdso(7)  or  in the C
library." I *think* we don't want to run the real trampoline code. The
equivalent code will be in the shim "signal delivery" handling code.

* Should we handle realtime signals?

* Signals generated outside of shadow: e.g. `SIGBUS` or `SIGSEGV`. MVP could
  just let them be handled natively, e.g. allowing the process to exit on
  `SIGBUS` or `SIGSEGV`. If we want to handle the case where the managed
  process wants to install a handler for such signals, we'll need to install a
  shim handler for each such signal. I suppose this could be done easily enough
  if we move the implementation of `sigaction` shim-side.

## Other notes

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
