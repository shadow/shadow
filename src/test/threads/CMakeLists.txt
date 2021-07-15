add_linux_tests(BASENAME pthreads COMMAND sh -c "../target/debug/test_pthreads --libc-passing")
add_shadow_tests(BASENAME pthreads)

add_linux_tests(BASENAME threads-noexit COMMAND sh -c "../target/debug/test_threads_noexit")
add_shadow_tests(BASENAME threads-noexit METHODS hybrid ptrace CHECK_RETVAL FALSE)

add_linux_tests(BASENAME threads-group-leader-exits COMMAND sh -c "../target/debug/test_threads_group_leader_exits")
add_shadow_tests(BASENAME threads-group-leader-exits METHODS hybrid ptrace)

## FIXME: Thread-related syscalls are not currently emulated in Shadow's preload mode.
##        `clone()` is currently unimplemented, which means the clone runs natively, bypassing Shadow.
##        The interposed libc does not handle this either, because it does not cover the thread apis.
##        See https://github.com/shadow/shadow/issues/1454.
##add_shadow_tests(BASENAME pthreads METHODS preload)
##add_shadow_tests(BASENAME threads-noexit METHODS preload)
