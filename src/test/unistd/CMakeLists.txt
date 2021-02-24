add_linux_tests(BASENAME unistd COMMAND sh -c "\
../target/debug/test_unistd \"$(uname -s)\" \"$(uname -n)\" \"$(uname -r)\" \"$(uname -v)\" \"$(uname -m)\" \
")
add_shadow_tests(BASENAME unistd METHODS hybrid ptrace preload)