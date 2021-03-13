add_linux_tests(BASENAME pthreads COMMAND sh -c "../target/debug/test_pthreads --libc-passing")
add_shadow_tests(BASENAME pthreads METHODS hybrid ptrace)

add_linux_tests(BASENAME threads-noexit COMMAND sh -c "../target/debug/test_threads_noexit")
add_shadow_tests(BASENAME threads-noexit METHODS hybrid ptrace CHECK_RETVAL FALSE)

## FIXME: Enable in all configurations. See https://github.com/shadow/shadow/issues/892
## FIXME: Thread-related syscalls are not currently emulated in Shadow's preload mode.
##        `clone()` is currently unimplemented, which means the clone runs natively, bypassing Shadow.
##        The interposed libc does not handle this either, because it does not cover the thread apis.
##add_shadow_tests(BASENAME pthreads METHODS preload CONFIGURATIONS ilibc)
##add_shadow_tests(BASENAME threads-noexit METHODS preload CONFIGURATIONS ilibc)