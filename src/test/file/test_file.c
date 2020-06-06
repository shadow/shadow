/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

// TODO: Implement fwrite

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "test/test_glib_helpers.h"

// For use in conjunction with g_auto for a file that will delete itself on
// function exit.
typedef struct {
    const char* filename;
} TmpFile;

// Configure g_auto(TmpFile) to delete the file on function exit.
void tmpfile_delete(TmpFile* f) { unlink(f->filename); }
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(TmpFile, tmpfile_delete);

static TmpFile tmpfile_make(const char* filename, const char* contents) {
    TmpFile tf = {.filename = filename};
    FILE* f = fopen(filename, "w");
    g_assert(f);
    g_assert(fwrite(contents, 1, strlen(contents), f) == strlen(contents));
    fclose(f);
    return tf;
}

static void _test_newfile() {
    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "w"));
    fclose(file);
    unlink("testfile");
}

static void _test_write(){
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r+"));

    int filed;
    assert_nonneg_errno(filed = fileno(file));
    assert_nonneg_errno(write(filed, "test", 4));

    fclose(file);
}

static void _test_read() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r"));

    int filed;
    assert_nonneg_errno(filed = fileno(file));

    char buf[5] = {0};
    assert_nonneg_errno(read(filed, buf, 4));
    g_assert_cmpstr(buf, ==, "test");

    fclose(file);
}

static void _test_fwrite() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r+"));

    const char msg[] = "test";
    assert_nonneg_errno(fwrite(msg, sizeof(char), sizeof(msg)/sizeof(char), file));

    fclose(file);
}

static void _test_fread() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r"));

    char buf[5] = {0};
    assert_nonneg_errno(fread(buf, sizeof(char), 4, file));
    g_assert_cmpstr(buf, ==, "test");

    fclose(file);
}

static void _test_iov() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    const char* fpath = "iov_test_file";

    FILE* file;
    assert_nonnull_errno(file = fopen(fpath, "w+"));

    int filed;
    assert_nonneg_errno(filed = fileno(file));

    struct iovec iov[UIO_MAXIOV];

    int rv = 0;
    int expected_errno = 0;
    int expected_rv = 0;

    g_assert_cmpint(readv(filed, iov, -1), ==, -1);
    assert_errno_is(EINVAL);

    g_assert_cmpint(readv(filed, iov, UIO_MAXIOV+1), ==, -1);
    assert_errno_is(EINVAL);

    // Invalid fd
    g_assert_cmpint(readv(1923, iov, UIO_MAXIOV+1), ==, -1);
    assert_errno_is(EBADF);

    // '0' iovcnt
    assert_nonneg_errno(readv(filed, iov, 0));

#define ARRAY_LENGTH(arr)  (sizeof (arr) / sizeof ((arr)[0]))

    // make all bases point to a string but all len to 0
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = "REAL DATA";
        iov[i].iov_len = 0;
    }

    // should write 0 bytes
    g_assert_cmpint(writev(filed, iov, ARRAY_LENGTH(iov)), ==, 0);

    // make all bases share the same buf
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = NULL;
        iov[i].iov_len = 80;
    }

    // should read 0 bytes
    g_assert_cmpint(readv(filed, iov, ARRAY_LENGTH(iov)), ==, 0);

    // write two real blocks
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = "REAL DATA";
        iov[i].iov_len = 0;
    }

    const char block_1_data[] = "hellloo o 12  o .<  oadsa flasll llallal";
    iov[31].iov_base = (void*)block_1_data;
    iov[31].iov_len = strlen(block_1_data);

    const char block_2_data[] = "___ = ==xll3kjf l  llxkf 0487oqlkj kjalskkkf";
    iov[972].iov_base = (void*)block_2_data;
    iov[972].iov_len = strlen(block_2_data);

    g_assert_cmpint(writev(filed, iov, ARRAY_LENGTH(iov)), ==,
                    strlen(block_1_data) + strlen(block_2_data));

    // read it back in

    // shadow doesn't implement seek, so we have to close the file
    // and reopen it
    fclose(file);
    file = fopen(fpath, "r");
    filed = fileno(file);

    char sharedreadbuf[14] = {[0 ... 13] = 'y'};
    const char compare_buf[14] = {[0 ... 13] = 'y'};
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = sharedreadbuf;
        iov[i].iov_len = 0;
    }

    // should read 0 bytes
    g_assert_cmpint(readv(filed, iov, ARRAY_LENGTH(iov)), ==, 0);

    // make sure our shared buf have not been touched
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        g_assert_cmpint(iov[i].iov_len, ==, 0);
    }
    g_assert_cmpmem(sharedreadbuf, sizeof(compare_buf), compare_buf,
                    sizeof(compare_buf));

    /****
     **** read into one base
     ****/

    // to contain data read by readv(). "- 1" to discount the
    // nul-terminator
    const size_t num_real_bytes = (sizeof(block_1_data) - 1) + (sizeof(block_2_data) - 1);
    size_t readbuf_size = num_real_bytes + 5;
    void* readbuf = calloc(1, readbuf_size);
    for(int j = 0; j < (readbuf_size); j++) {
        ((char*)readbuf)[j] = 'z';
    }
    iov[1023].iov_base = readbuf;
    iov[1023].iov_len = readbuf_size;

    // verify
    g_assert_cmpint(readv(filed, iov, ARRAY_LENGTH(iov)), ==,
                    strlen(block_1_data) + strlen(block_2_data));

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        if (i == 1023) {
            // readv should not have touched the iov_len
            g_assert_cmpint(iov[i].iov_len, ==, readbuf_size);
        } else {
            g_assert_cmpint(iov[i].iov_len, ==, 0);
            g_assert_cmpmem(iov[i].iov_base, sizeof(compare_buf), compare_buf,
                            sizeof(compare_buf));
        }
    }

    g_assert_cmpmem(readbuf, strlen(block_1_data), block_1_data,
                    strlen(block_1_data));
    g_assert_cmpmem(readbuf + strlen(block_1_data), strlen(block_2_data),
                    block_2_data, strlen(block_2_data));
    g_assert_cmpmem(readbuf + strlen(block_1_data) + strlen(block_2_data), 5,
                    "zzzzz", 5);

    free(readbuf);
    readbuf = NULL;

    /****
     **** read into two bases
     ****/

    fclose(file);
    file = fopen(fpath, "r");
    filed = fileno(file);

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = NULL;
        iov[i].iov_len = 0;
    }

    char buf1[13];
    char buf2[4];
    iov[441].iov_base = buf1;
    iov[441].iov_len = sizeof buf1;
    iov[442].iov_base = buf2;
    iov[442].iov_len = sizeof buf2;

    g_assert_cmpint(readv(filed, iov, ARRAY_LENGTH(iov)), ==,
                    (sizeof buf1) + (sizeof buf2));

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        if (i == 441) {
            g_assert_cmpmem(iov[i].iov_base, iov[i].iov_len, "hellloo o 12 ",
                            sizeof buf1);
        } else if (i == 442) {
            g_assert_cmpmem(iov[i].iov_base, iov[i].iov_len, " o .",
                            sizeof buf2);
        } else {
            g_assert_cmpint(iov[i].iov_len, ==, 0);
        }
    }

    /* success */
    fclose(file);
}

static void _test_fprintf() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r+"));
    assert_nonneg_errno(fprintf(file, "canwrite"));

    fclose(file);
}

static void _test_fscanf() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "canwrite");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r"));

    char buf[10] = {0};

    /* read through the file */
    assert_true_errno(fscanf(file, "%s", buf) != EOF);

    /* check that fscanf read correctly */
    g_assert_cmpstr(buf, ==, "canwrite");

    /* success! */
    fclose(file);
}

static void _test_chmod() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r+"));

    int filed;
    assert_nonneg_errno(filed = fileno(file));

    /* set permissions to owner user/group only */
    assert_nonneg_errno(fchmod(filed, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));

    /* success! */
    fclose(file);
}

static void _test_fstat() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    FILE* file;
    assert_nonnull_errno(file = fopen("testfile", "r+"));

    int filed;
    assert_nonneg_errno(filed = fileno(file));
    assert_nonneg_errno(fchmod(filed, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));

    struct stat filestat = {0};
    assert_nonneg_errno(fstat(filed, &filestat));

    g_assert_cmpint(filestat.st_mode & S_IXOTH, ==, 0);
    g_assert_cmpint(filestat.st_mode & S_IWOTH, ==, 0);
    g_assert_cmpint(filestat.st_mode & S_IROTH, ==, 0);

    /* success! */
    fclose(file);
}

static void _test_open_close() {
    g_auto(TmpFile) tf = tmpfile_make("testfile", "test");

    int filed;
    assert_nonneg_errno(filed = open("testfile", O_RDONLY));
    assert_nonneg_errno(close(filed));
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file/newfile", _test_newfile);
    g_test_add_func("/file/open_close", _test_open_close);
    g_test_add_func("/file/write", _test_write);
    g_test_add_func("/file/read", _test_read);
    g_test_add_func("/file/fwrite", _test_fwrite);
    g_test_add_func("/file/fread", _test_fread);
//    TODO: debug and fix iov test
//    g_test_add_func("/file/iov", _test_iov);
    g_test_add_func("/file/fprintf", _test_fprintf);
    g_test_add_func("/file/fscanf", _test_fscanf);
    g_test_add_func("/file/chmod", _test_chmod);
    g_test_add_func("/file/fstat", _test_fstat);
    g_test_run();

    return 0;
}
