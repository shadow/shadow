add_executable(test-sysv-shm test_sysv_shm.c)

add_linux_tests(BASENAME sysv-shm-basic COMMAND test-sysv-shm basic)
add_linux_tests(
    BASENAME sysv-shm-multiprocess
    COMMAND shadow-test-launcher test-sysv-shm writer 23057 : test-sysv-shm reader 23057
)
add_linux_tests(
    BASENAME sysv-shm-owner-perms
    COMMAND test-sysv-shm owner-perms 23059
)

add_shadow_tests(BASENAME sysv-shm)
add_shadow_tests(BASENAME sysv-shm-isolation)
add_shadow_tests(BASENAME sysv-shm-owner-perms)
