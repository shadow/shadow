# regression test for https://github.com/shadow/shadow/issues/2210
add_shadow_tests(
  BASENAME regression-2210
  # python should die with signal 9, giving exitcode 128+9=137
  POST_CMD "test `cat hosts/*/*.1000.exitcode` -eq 137"
  # shadow should return failure
  EXPECT_ERROR TRUE
  CHECK_RETVAL FALSE)
