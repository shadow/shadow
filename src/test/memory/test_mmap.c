/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <sys/mman.h>

#include "test/test_glib_helpers.h"

#define MAPLEN 16

static void _test_mmap() {
    /* Get a file that we can mmap and write into. */
    FILE* temp = NULL;
    assert_nonnull_errno(temp = tmpfile());

    int tempFD = 0;
    assert_nonneg_errno(tempFD = fileno(temp));

    /* Make sure there is enough space to write after the mmap. */
    {
        int rv = posix_fallocate(tempFD, 0, MAPLEN);
        assert_true_errstring(rv == 0, strerror(rv));
    }

    /* Init a msg to write. */
    char msg[MAPLEN] = {0};
    assert_nonneg_errno(snprintf(msg, MAPLEN, "Hello world!"));

    /* Do the mmap and write the message into the resulting mem location. */
    void* mapbuf =
        mmap(NULL, MAPLEN, PROT_READ | PROT_WRITE, MAP_SHARED, tempFD, 0);
    g_assert_cmpint((long)mapbuf, !=, -1);

    assert_nonneg_errno(snprintf(mapbuf, MAPLEN, "%s", msg));

    assert_nonneg_errno(munmap(mapbuf, MAPLEN));

    /* Read the file and make sure the same message is there. */
    assert_nonneg_errno(fseek(temp, 0, SEEK_SET));

    char rdbuf[MAPLEN] = {0};
    g_assert_cmpint(fread(rdbuf, 1, MAPLEN, temp), >, 0);

    g_assert_cmpmem(msg, sizeof(msg), rdbuf, sizeof(rdbuf));

    assert_nonneg_errno(fclose(temp));
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/memory/mmap", _test_mmap);
    g_test_run();
    return 0;
}
