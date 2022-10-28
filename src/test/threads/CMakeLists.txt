add_linux_tests(BASENAME pthreads COMMAND sh -c "../../target/debug/test_pthreads --libc-passing")
add_shadow_tests(BASENAME pthreads)

add_linux_tests(BASENAME threads-noexit COMMAND sh -c "../../target/debug/test_threads_noexit")
add_shadow_tests(BASENAME threads-noexit CHECK_RETVAL FALSE)

add_linux_tests(BASENAME threads-group-leader-exits COMMAND sh -c "../../target/debug/test_threads_group_leader_exits")
add_shadow_tests(BASENAME threads-group-leader-exits)
