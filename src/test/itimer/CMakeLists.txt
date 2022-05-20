add_linux_tests(BASENAME itimer COMMAND sh -c "../target/debug/test_itimer")
# TODO: add shadow tests once getitimer and setitimer are implemented