/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

// TODO: Implement fwrite

#include <dirent.h>
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

// For use in conjunction with g_auto so that the files/dirs will delete themselves on function
// exit.
#define AUTOFILE_NAME_MAXLEN 24
typedef struct {
    int fd;
    char name[AUTOFILE_NAME_MAXLEN];
} AutoDeleteFile;

// Generate a unique file/dir name to avoid race conditions when we are running
// this test multiple times in parallel.
static AutoDeleteFile _create_auto_file() {
    AutoDeleteFile adf = {0};
    assert_nonneg_errno(snprintf(adf.name, AUTOFILE_NAME_MAXLEN, "%s", "autodelete-file-XXXXXX"));
    assert_nonneg_errno(adf.fd = mkstemp(adf.name));
    return adf;
}
static AutoDeleteFile _create_auto_dir() {
    AutoDeleteFile adf = {0};
    assert_nonneg_errno(snprintf(adf.name, AUTOFILE_NAME_MAXLEN, "%s", "autodelete-dir-XXXXXX"));
    assert_nonnull_errno(mkdtemp(adf.name));
    assert_nonneg_errno(adf.fd = open(adf.name, O_RDONLY | O_DIRECTORY));
    return adf;
}
void _delete_auto(AutoDeleteFile* f) {
    if (f) {
        if (f->fd) {
            close(f->fd);
        }
        unlink(f->name);
        rmdir(f->name);
    }
}
// Configure g_auto to delete the auto files and dirs on function exit.
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(AutoDeleteFile, _delete_auto);

static void _set_contents(AutoDeleteFile* adf, const char* contents, size_t len) {
    assert_nonneg_errno(write(adf->fd, contents, len));
}

static void _test_open() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    int fd;
    assert_nonneg_errno(fd = open(adf.name, O_RDONLY));
    close(fd); // not testing close yet so don't assert here
}

static void _test_close() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    int fd;
    assert_nonneg_errno(fd = open(adf.name, O_RDONLY));
    assert_nonneg_errno(close(fd));
}

static void _test_write() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    const char wbuf[] = "test file write";
    int fd, rv;
    assert_nonneg_errno(fd = open(adf.name, O_WRONLY));
    assert_nonneg_errno(rv = write(fd, wbuf, sizeof(wbuf)));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    assert_nonneg_errno(rv = write(fd, "asdf", 0)); // check that 0 bytes is allowed
    g_assert_cmpint(rv, ==, 0);
    assert_nonneg_errno(close(fd));
}

static void _test_read() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    const char wbuf[] = "test file read";
    char rbuf[sizeof(wbuf)] = {0};
    int fd, rv;
    _set_contents(&adf, wbuf, sizeof(wbuf));
    assert_nonneg_errno(fd = open(adf.name, O_RDONLY));
    assert_nonneg_errno(rv = read(fd, rbuf, sizeof(wbuf)));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    g_assert_cmpstr(rbuf, ==, wbuf);
    assert_nonneg_errno(close(fd));
}

static void _test_lseek() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    const char wbuf[] = "test file lseek";
    char rbuf[sizeof(wbuf)] = {0};
    int fd, rv;
    assert_nonneg_errno(fd = open(adf.name, O_RDWR));
    assert_nonneg_errno(rv = write(fd, wbuf, sizeof(wbuf)));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    assert_nonneg_errno(lseek(fd, 0, SEEK_SET));
    assert_nonneg_errno(rv = read(fd, rbuf, sizeof(wbuf)));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    g_assert_cmpstr(rbuf, ==, wbuf);
    assert_nonneg_errno(close(fd));
}

static void _test_fopen() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    FILE* file;
    assert_nonnull_errno(file = fopen(adf.name, "r"));
    fclose(file); // not testing fclose yet so don't assert here
}

static void _test_fclose() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    FILE* file;
    assert_nonnull_errno(file = fopen(adf.name, "r"));
    assert_nonneg_errno(fclose(file));
}

static void _test_fileno() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    FILE* file;
    assert_nonnull_errno(file = fopen(adf.name, "r"));
    assert_nonneg_errno(fileno(file));
    assert_nonneg_errno(fclose(file));
}

static void _test_fwrite() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    const char wbuf[] = "test file fwrite";
    FILE* file;
    size_t rv;
    assert_nonnull_errno(file = fopen(adf.name, "r+"));
    assert_nonneg_errno(rv = fwrite(wbuf, sizeof(char), sizeof(wbuf) / sizeof(char), file));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    assert_nonneg_errno(fclose(file));
}

static void _test_fread() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    const char wbuf[] = "test file fread";
    char rbuf[sizeof(wbuf)] = {0};
    FILE* file;
    size_t rv;
    _set_contents(&adf, wbuf, sizeof(wbuf));
    assert_nonnull_errno(file = fopen(adf.name, "r"));
    assert_nonneg_errno(rv = fread(rbuf, sizeof(char), sizeof(wbuf), file));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    g_assert_cmpstr(rbuf, ==, wbuf);
    assert_nonneg_errno(fclose(file));
}

static void _test_fprintf() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    const char wbuf[] = "test file fprintf";
    FILE* file;
    int rv;
    assert_nonnull_errno(file = fopen(adf.name, "r+"));
    assert_nonneg_errno(rv = fprintf(file, "%s", wbuf));
    g_assert_cmpint(rv, ==, sizeof(wbuf) - 1); // null byte not included in count
    assert_nonneg_errno(fclose(file));
}

static void _test_fscanf() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    const char wbuf[] = "testfilefscanf";
    char rbuf[sizeof(wbuf)] = {0};
    FILE* file;
    size_t rv;
    _set_contents(&adf, wbuf, sizeof(wbuf));
    assert_nonnull_errno(file = fopen(adf.name, "r"));
    assert_true_errno(fscanf(file, "%s", rbuf) != EOF);
    g_assert_cmpstr(rbuf, ==, "testfilefscanf"); // fails with wbuf, not sure why
    assert_nonneg_errno(fclose(file));
}

static void _test_fchmod() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    FILE* file;
    int fd;
    assert_nonnull_errno(file = fopen(adf.name, "r+"));
    assert_nonneg_errno(fd = fileno(file));
    /* set permissions to owner user/group only */
    assert_nonneg_errno(fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));
    assert_nonneg_errno(fclose(file));
}

static void _test_fstat() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();
    FILE* file;
    int fd;

    assert_nonnull_errno(file = fopen(adf.name, "r+"));

    assert_nonneg_errno(fd = fileno(file));
    assert_nonneg_errno(fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));

    struct stat filestat = {0};
    assert_nonneg_errno(fstat(fd, &filestat));

    g_assert_cmpint(filestat.st_mode & S_IXOTH, ==, 0);
    g_assert_cmpint(filestat.st_mode & S_IWOTH, ==, 0);
    g_assert_cmpint(filestat.st_mode & S_IROTH, ==, 0);

    /* success! */
    fclose(file);
}

static void _test_dir() {
    g_auto(AutoDeleteFile) adf = _create_auto_dir();
    DIR* dir;
    int dirfd;
    struct dirent *de;

    // Make the new directory and make sure we can open it.
    assert_nonneg_errno(dirfd = open(adf.name, O_RDONLY));
    assert_nonneg_errno(close(dirfd));

    // Make sure we can get the contents.
    assert_nonnull_errno(dir = opendir(adf.name));
    assert_nonnull_errno(de = readdir(dir));
    while(de) {
        g_assert_nonnull(de->d_name);
        // Get the next, now it's OK if NULL.
        de = readdir(dir);
    }

    // Close and remove the directory.
    assert_nonneg_errno(closedir(dir));
    assert_nonneg_errno(rmdir(adf.name));
}

static void _test_tmpfile() {
    const char wbuf[] = "test file tmpfile";
    char rbuf[sizeof(wbuf)] = {0};
    FILE* file;
    int fd;
    size_t rv;
    
    // Create temporary file and test i/o
    assert_nonnull_errno(file = tmpfile());
    assert_nonneg_errno(fd = fileno(file));
    assert_nonneg_errno(rv = fwrite(wbuf, sizeof(char), sizeof(wbuf)/sizeof(char), file));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    rewind(file);
    assert_nonneg_errno(rv = fread(rbuf, sizeof(char), sizeof(wbuf), file));
    g_assert_cmpint(rv, ==, sizeof(wbuf));
    g_assert_cmpstr(rbuf, ==, wbuf);

    assert_nonneg_errno(fclose(file));
}

static void _test_iov() {
    g_auto(AutoDeleteFile) adf = _create_auto_file();

    FILE* file;
    assert_nonnull_errno(file = fopen(adf.name, "w+"));

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
    file = fopen(adf.name, "r");
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
    file = fopen(adf.name, "r");
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

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);

    // These are generally ordered by increasing level of required functionality.
    // I.e., later tests use some of the functions tested in earlier tests.
    g_test_add_func("/file/open", _test_open);
    g_test_add_func("/file/close", _test_close);
    g_test_add_func("/file/write", _test_write);
    g_test_add_func("/file/read", _test_read);
    g_test_add_func("/file/lseek", _test_lseek);
    g_test_add_func("/file/fopen", _test_fopen);
    g_test_add_func("/file/fclose", _test_fclose);
    g_test_add_func("/file/fileno", _test_fileno);
    g_test_add_func("/file/fwrite", _test_fwrite);
    g_test_add_func("/file/fread", _test_fread);
    g_test_add_func("/file/fprintf", _test_fprintf);
    g_test_add_func("/file/fscanf", _test_fscanf);
    g_test_add_func("/file/chmod", _test_fchmod);
    g_test_add_func("/file/fstat", _test_fstat);

    g_test_add_func("/file/dir", _test_dir);
    g_test_add_func("/file/tmpfile", _test_tmpfile);

    //    TODO: debug and fix iov test
    //    g_test_add_func("/file/iov", _test_iov);

    g_test_run();

    return 0;
}
