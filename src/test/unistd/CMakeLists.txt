add_linux_tests(BASENAME unistd COMMAND sh -c "\
../../target/debug/test_unistd \"$(uname -s)\" \"$(uname -n)\" \"$(uname -r)\" \"$(uname -v)\" \"$(uname -m)\" \
")
add_shadow_tests(BASENAME unistd)

# Specifically tests Shadow's documented pid assignment behavior.
# Doesn't make sense to run natively, where we don't control PIDs.
add_shadow_tests(BASENAME pid_assignment)
