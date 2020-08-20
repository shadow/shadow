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

// Writes data to a file and reads it back in, to validate that Shadow accesses
// the given memory correctly.
static void _validate_shadow_access(unsigned char* buf, size_t size) {
    GError* error = NULL;
    char* name = NULL;
    int fd = g_file_open_tmp("test_mmap.XXXXXX", &name, &error);
    g_assert_no_error(error);
    close(fd);

    g_file_set_contents(name, (gchar*)buf, size, &error);
    g_assert_no_error(error);

    gchar* file_contents;
    gsize file_size;
    g_file_get_contents(name, &file_contents, &file_size, &error);
    g_assert_no_error(error);

    g_assert_cmpmem(file_contents, file_size, buf, size);

    unlink(name);

    g_free(name);
    g_free(file_contents);
}

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

static size_t page_size() { return sysconf(_SC_PAGE_SIZE); }

static void init_buf(unsigned char* buf, size_t size) {
    for (int i = 0; i < size; ++i) {
        buf[i] = i / page_size();
    }
}

static unsigned char* mmap_and_init_buf(size_t size) {
    unsigned char* buf;
    assert_true_errno((buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                                  -1, 0)) != MAP_FAILED);
    init_buf(buf, size);
    return buf;
}

static void _test_mmap_anon() {
    size_t initial_size = 2 * page_size();
    unsigned char* buf = mmap_and_init_buf(initial_size);
    _validate_shadow_access(buf, initial_size);

    // Grow the buffer.
    size_t grown_size = 2 * initial_size;
    {
        // We have to allow the buffer to move to potentially guarantee the allocation succeeds.
        assert_true_errno((buf = mremap(buf, initial_size, grown_size, MREMAP_MAYMOVE)) !=
                          MAP_FAILED);
    }

    // Validate that initial contents are still there.
    for (int i = 0; i < initial_size; ++i) {
        g_assert_cmpint(buf[i], ==, i / page_size());
    }

    _validate_shadow_access(buf, initial_size);

    // Fill the new portion of the buffer.
    init_buf(buf, grown_size);

    // Validate the whole contents of the buffer.
    for (int i = 0; i < grown_size; ++i) {
        g_assert_cmpint(buf[i], ==, i / page_size());
    }

    _validate_shadow_access(buf, grown_size);

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
        g_assert_cmpint(buf[i], ==, i / page_size());
    }

    _validate_shadow_access(buf, shrunk_size);
}

static void _test_mremap_clobber() {
    unsigned char* bigbuf = mmap_and_init_buf(3 * page_size());

    unsigned char* smallbuf = mmap_and_init_buf(page_size());

    // mremap smallbuf into the middle of bigbuf, clobbering it.
    {
        unsigned char* requested_new_address = &bigbuf[page_size()];
        unsigned char* actual_new_address;

        assert_true_errno((actual_new_address = mremap(smallbuf, page_size(), page_size(),
                                                       MREMAP_MAYMOVE | MREMAP_FIXED,
                                                       requested_new_address)) != MAP_FAILED);
        g_assert_cmpint((int64_t)actual_new_address, ==, (int64_t)requested_new_address);
        smallbuf = actual_new_address;
    }
    // First page of bigbuf should be untouched.
    for (int i = 0; i < page_size(); ++i) {
        g_assert_cmpint(bigbuf[i], ==, 0);
    }
    _validate_shadow_access(&bigbuf[0], page_size());

    // Next page should have been overwritten by smallbuf
    for (int i = page_size(); i < (2 * page_size()); ++i) {
        g_assert_cmpint(bigbuf[i], ==, 0);
    }
    _validate_shadow_access(&bigbuf[page_size()], page_size());

    // Last page should be untouched.
    for (int i = (2 * page_size()); i < (3 * page_size()); ++i) {
        g_assert_cmpint(bigbuf[i], ==, 2);
    }
    _validate_shadow_access(&bigbuf[2 * page_size()], page_size());

    // Validate Shadow access of the whole buffer (which crosses mmap'd regions) at once.
    _validate_shadow_access(bigbuf, 3 * page_size());
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/memory/mmap_file", _test_mmap_file);
    g_test_add_func("/memory/mmap_anon", _test_mmap_anon);
    g_test_add_func("/memory/mremap_clobber", _test_mremap_clobber);
    g_test_run();
    return 0;
}
