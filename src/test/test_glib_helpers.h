#ifndef SHD_TEST_GLIB_HELPERS_H
#define SHD_TEST_GLIB_HELPERS_H

#include <errno.h>
#include <glib.h>
#include <unistd.h>

#define assert_true_errstring(c, s)                                            \
    if (!(c)) {                                                                \
        g_error("!(%s): %s", #c, (s));                                         \
        g_test_fail();                                                         \
        exit(EXIT_FAILURE);                                                    \
    }

// Similar to g_assert_true, but include stringified errno on failure.
#define assert_true_errno(c) assert_true_errstring(c, strerror(errno))
#define assert_nonnull_errno(p) assert_true_errno((p) != NULL)
#define assert_nonneg_errno(p) assert_true_errno((p) >= 0)

// Assert that errno is the expected value.
#define assert_errno_is(e)                                                     \
    {                                                                          \
        int _errno = errno;                                                    \
        int _e = e;                                                            \
        if (_e != _errno) {                                                    \
            g_error("Got errno %d (%s) instead of %d (%s)", _errno,            \
                    strerror(_errno), _e, strerror(_e));                       \
            g_test_fail();                                                     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    }


#endif
