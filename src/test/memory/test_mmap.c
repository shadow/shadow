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

static void _test_mmap_file() {
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

static void _test_mmap_anon() {
    unsigned char* buf;
    // Don't worry about the system page size, but make it big enough to span multiple pages.
    size_t initial_size = 100 * 1024;
    assert_true_errno((buf = mmap(NULL, initial_size, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) != MAP_FAILED);

    // Set each byte to its KB offset.
    for (int i = 0; i < initial_size; ++i) {
        buf[i] = i / 1024;
    }

    // Grow the buffer.
    size_t grown_size = 2 * initial_size;
    {
        // We have to allow the buffer to move to potentially guarantee the allocation succeeds.
        assert_true_errno((buf = mremap(buf, initial_size, grown_size, MREMAP_MAYMOVE)) !=
                          MAP_FAILED);
    }

    // Fill the new portion of the buffer.
    for (int i = initial_size; i < grown_size; ++i) {
        buf[i] = i / 1024;
    }

    // Validate the whole contents of the buffer.
    for (int i = 0; i < grown_size; ++i) {
        g_assert_cmpint(buf[i], ==, i / 1024);
    }

    // Shrink the buffer.
    size_t shrunk_size = initial_size / 2;
    {
        char* shrunk_buf;
        assert_true_errno((shrunk_buf = mremap(buf, grown_size, shrunk_size, 0)) != MAP_FAILED);
        // Shouldn't have moved.
        g_assert_cmpint((int64_t)buf, ==, (int64_t)shrunk_buf);
    }

    // Validate the whole contents of the buffer.
    for (int i = 0; i < shrunk_size; ++i) {
        g_assert_cmpint(buf[i], ==, i / 1024);
    }
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/memory/mmap_file", _test_mmap_file);
    g_test_add_func("/memory/mmap_anon", _test_mmap_anon);
    g_test_run();
    return 0;
}
