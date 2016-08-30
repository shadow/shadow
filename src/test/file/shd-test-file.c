/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

//TODO: Implement fwrite

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <errno.h>

static int _test_newfile() {
    FILE* file = fopen("testfile", "w");
    if(file == NULL) {
        fprintf(stdout, "error: could not create new file\n");
        return -1;
    }

    fclose(file);
    /* success! */
    return 0;
}

static int _test_write(){
    FILE* file = fopen("testfile", "r+");
    if(file == NULL) {
        fprintf(stdout, "error: could not open file\n");
        return -1;
    }

    int filed = fileno(file);
    if(filed <  0) {
        fprintf(stdout, "error: fileno did not receive valid stream");
        fclose(file);
        return -1;
    }

    if(write(filed, "test", 4) < 0) {
        fprintf(stdout, "error: write failed\n");
        fclose(file);
        return -1;
    }

    /* success */
    fclose(file);
    return 0;
}

static int _test_read() {
    FILE* file = fopen("testfile", "r");
    if(file == NULL) {
        fprintf(stdout, "error: could not open file\n");
        return -1;
    }

    int filed = fileno(file);
    if(filed <  0) {
        fprintf(stdout, "error: fileno did not receive valid stream");
        fclose(file);
        return -1;
    }

    char buf[5];
    memset(buf, '\0', sizeof(buf));
    if(read(filed, buf, 4) < 0) {
        fprintf(stdout, "error: read failed\n");
        fclose(file);
        return -1;
    }

    if(strncmp(buf, "test", 4) != 0) {
        fprintf(stdout, "error: buf: %s\n", buf);
        fclose(file);
        return -1;
    }

    /* succes */
    fclose(file);
    return 0;
}

static int _test_fwrite() {
    FILE* file = fopen("testfile", "r+");
    if(file == NULL) {
        fprintf(stdout, "error: could not open file\n");
        return -1;
    }

    const char* msg = "test";
    if(fwrite(msg, sizeof(char), sizeof(msg)/sizeof(char), file) <= 0) {
        fprintf(stdout, "error: fwrite failed\n");
        fclose(file);
        return -1;
    }

    /* succes */
    fclose(file);
    return 0;
}

static int _test_fread() {
    FILE* file = fopen("testfile", "r");
    if(file == NULL) {
        fprintf(stdout, "error: could not open file\n");
        return -1;
    }

    char buf[5];
    memset(buf, '\0', sizeof(buf));
    if(fread(buf, sizeof(char), 4, file) <= 0) {
        fprintf(stdout, "error: fread failed\n");
        fclose(file);
        return -1;
    }

    if(strncmp(buf, "test", 4) != 0) {
        fprintf(stdout, "error: buf: %s\n", buf);
        fclose(file);
        return -1;
    }

    /* succes */
    fclose(file);
    return 0;
}

static int _test_iov(const char* fpath) {
#undef LOG
#define LOG(fmt, ...)                                                   \
    do {                                                                \
        fprintf(stdout, "line: %d: " fmt "\n",                          \
                __LINE__, ##__VA_ARGS__);                               \
    } while (0)

#define LOG_ERROR_AND_RETURN(fmt, ...)                                  \
    do {                                                                \
        LOG("error: " fmt, ##__VA_ARGS__);                              \
        fclose(file);                                                   \
        return -1;                                                      \
    } while (0)


    FILE* file = fopen(fpath, "w+");
    if(file == NULL) {
        LOG("error: could not open file");
        return -1;
    }

    int filed = fileno(file);
    if(filed <  0) {
        LOG_ERROR_AND_RETURN("fileno did not receive valid stream");
    }

    struct iovec iov[UIO_MAXIOV];

    int rv = 0;
    int expected_errno = 0;
    int expected_rv = 0;

    rv = readv(filed, iov, -1);
    if (rv != -1) {
        LOG_ERROR_AND_RETURN("should fail on an invalid arg");
    }
    expected_errno = EINVAL;
    if (errno != expected_errno) {
        LOG_ERROR_AND_RETURN("expected errno: %d, actual: %d",
                             expected_errno, errno);
    }

    rv = readv(filed, iov, UIO_MAXIOV+1);
    if (rv != -1) {
        LOG_ERROR_AND_RETURN("should fail on an invalid arg");
    }
    expected_errno = EINVAL;
    if (errno != expected_errno) {
        LOG_ERROR_AND_RETURN("expected errno: %d, actual: %d",
                             expected_errno, errno);
    }

    rv = readv(1923, iov, UIO_MAXIOV+1);
    if (rv != -1) {
        LOG_ERROR_AND_RETURN("should fail on an invalid fd");
    }
    expected_errno = EBADF;
    if (errno != expected_errno) {
        LOG_ERROR_AND_RETURN("expected errno: %d, actual: %d",
                             expected_errno, errno);
    }

    rv = readv(filed, iov, 0);
    if (rv == -1) {
        LOG_ERROR_AND_RETURN("should not fail when passing '0' as the iovcnt");
    }

#define ARRAY_LENGTH(arr)  (sizeof (arr) / sizeof ((arr)[0]))

    // make all bases point to a string but all len to 0
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = "REAL DATA";
        iov[i].iov_len = 0;
    }

    // should write 0 bytes
    rv = writev(filed, iov, ARRAY_LENGTH(iov));
    expected_rv = 0;
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

    // make all bases share the same buf
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = NULL;
        iov[i].iov_len = 80;
    }

    // should read 0 bytes
    rv = readv(filed, iov, ARRAY_LENGTH(iov));
    expected_rv = 0;
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

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

    rv = writev(filed, iov, ARRAY_LENGTH(iov));
    expected_rv = strlen(block_1_data) + strlen(block_2_data);
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

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
    rv = readv(filed, iov, ARRAY_LENGTH(iov));
    expected_rv = 0;
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

    // make sure our shared buf have not been touched
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        if (iov[i].iov_len != 0) {
            LOG_ERROR_AND_RETURN("just BAD!");
        }
    }
    if (memcmp(sharedreadbuf, compare_buf, sizeof compare_buf)) {
        LOG_ERROR_AND_RETURN("WHAT DID YOU DO!!!");
    }

    /****
     **** read into one base
     ****/

    // to contain data read by readv(). "- 1" to discount the
    // nul-terminator
    const size_t num_real_bytes = (sizeof block_1_data - 1) + (sizeof block_2_data - 1);
    char readbuf[num_real_bytes + 5] = {[0 ... num_real_bytes + 5 - 1] = 'z'};
    iov[1023].iov_base = readbuf;
    iov[1023].iov_len = sizeof readbuf;

    rv = readv(filed, iov, ARRAY_LENGTH(iov));

    // verify
    expected_rv = strlen(block_1_data) + strlen(block_2_data);
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        if (i == 1023) {
            // readv should not have touched the iov_len
            const size_t expected_len = sizeof readbuf;
            if (iov[i].iov_len != expected_len) {
                LOG_ERROR_AND_RETURN(
                    "readv produces wrong iov_len: %zu, expected: %zu",
                    iov[i].iov_len, expected_len);
            }
        } else {
            if (iov[i].iov_len != 0) {
                LOG_ERROR_AND_RETURN("just BAD");
            }
            if (memcmp(iov[i].iov_base, compare_buf, sizeof compare_buf)) {
                LOG_ERROR_AND_RETURN("WHAT DID YOU DO!!!");
            }
        }
    }

    if (memcmp(readbuf, block_1_data, strlen(block_1_data))) {
        LOG_ERROR_AND_RETURN("read data has incorrect bytes");
    }
    if (memcmp(readbuf + strlen(block_1_data), block_2_data, strlen(block_2_data))) {
        LOG_ERROR_AND_RETURN("read data has incorrect bytes");
    }
    if (memcmp(readbuf + strlen(block_1_data) + strlen(block_2_data), "zzzzz", 5)) {
        LOG_ERROR_AND_RETURN("readv() touched more memory than it should have");
    }

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

    rv = readv(filed, iov, ARRAY_LENGTH(iov));

    // verify
    expected_rv = (sizeof buf1) + (sizeof buf2);
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        if (i == 441) {
            if (iov[i].iov_len != sizeof buf1) {
                LOG_ERROR_AND_RETURN("BAD");
            }
            if (memcmp(iov[i].iov_base, "hellloo o 12 ", sizeof buf1)) {
                LOG_ERROR_AND_RETURN("BAD");
            }
        } else if (i == 442) {
            if (iov[i].iov_len != sizeof buf2) {
                LOG_ERROR_AND_RETURN("BAD");
            }
            if (memcmp(iov[i].iov_base, " o .", sizeof buf2)) {
                LOG_ERROR_AND_RETURN("BAD");
            }
        } else {
            if (iov[i].iov_len != 0) {
                LOG_ERROR_AND_RETURN("BAD");
            }
        }
    }

    /* success */
    fclose(file);
    return 0;
#undef LOG
}

static int _test_fprintf() {
    FILE* file = fopen("testfile", "r+");
    if(file == NULL) {
        fprintf(stdout, "error: could not open filed\n");
        return -1;
    }

    if(fprintf(file, "canwrite") < 0) {
        fprintf(stdout, "error: could not fprintf to file\n");
        fclose(file);
        return -1;
    }

    /* success! */
    fclose(file);
    return 0;
}

static int _test_fscanf() {
    FILE* file = fopen("testfile", "r");
    if(file == NULL) {
        fprintf(stdout, "error: could not open file\n");
        return -1;
    }
    
    char buf[10];
    memset(buf, '\0', sizeof(buf));

    /* read through the file */
    fscanf(file, "%s", buf);

    /* check that fscanf read correctly */
    if(strncmp(buf, "canwrite", 8) != 0) {
        fprintf(stdout, "error: buf: %s\n", buf);
        fclose(file);
        return -1;
    }

    /* success! */
    fclose(file);
    return 0;
}

static int _test_chmod() {
    FILE* file = fopen("testfile", "r+");
    if(file == NULL) {
        fprintf(stdout, "error: could not open filed\n");
        return -1;
    }

    int filed = fileno(file);
    if(filed <  0) {
        fprintf(stdout, "error: fileno did not receive valid stream\n");
        fclose(file);
        return -1;
    }

    /* set permissions to owner user/group only */
    if(fchmod(filed, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0) {
        fprintf(stdout, "error: could not change permissions of file\n");
        fclose(file);
        return -1;
    }

    /* success! */
    fclose(file);
    return 0;
}

static int _test_fstat() {
    FILE* file = fopen("testfile", "r+");
    if(file == NULL) {
        fprintf(stdout, "error: could not open file\n");
        return -1;
    }

    int filed = fileno(file);
    if(filed <  0) {
        fprintf(stdout, "error: fileno did not receive valid stream\n");
        fclose(file);
        return -1;
    }

    struct stat filestat;
    memset(&filestat, 0, sizeof(filestat));

    if(fstat(filed, &filestat) < 0){
        fprintf(stdout, "error: fstat failed\n");
        fclose(file);
        return -1;
    }

    if((filestat.st_mode & S_IXOTH) != 0) {
        fprintf(stdout, "error: S_IXOTH flag still set\n");
        fclose(file);
        return -1;
    }

    if((filestat.st_mode & S_IWOTH) != 0) {
        fprintf(stdout, "error: S_IWOTH flag still set\n");
        fclose(file);
        return -1;
    }

    if((filestat.st_mode & S_IROTH) != 0) {
        fprintf(stdout, "error: S_IROTH flag still set\n");
        fclose(file);
        return -1;
    }

    /* success! */
    fclose(file);
    return 0;
}

static int _test_open_close() {
    int filed = open("testfile", O_RDONLY);
    if(filed < 0) {
        fprintf(stdout, "error: could not open testfile\n");
        return -1;
    }

    if(close(filed) < 0) {
        fprintf(stdout, "error: close on testfile failed\n");
        return -1;
    }

    /* success! */
    return 0;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## file-io test starting ##########\n");

    if(_test_newfile() < 0) {
        fprintf(stdout, "########## _test_newfile() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_open_close() < 0) {
        fprintf(stdout, "########## _test_open_close() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_write() < 0) {
        fprintf(stdout, "########## _test_write() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_read() < 0) {
        fprintf(stdout, "########## _test_read() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_fwrite() < 0) {
        fprintf(stdout, "########## _test_fwrite() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_fread() < 0) {
        fprintf(stdout, "########## _test_fread() failed\n");
        unlink("testfile");
        return -1;
    }

    const char* iov_test_file = "iov_test_file";
    if(_test_iov(iov_test_file) < 0) {
        fprintf(stdout, "########## _test_iov() failed\n");
        unlink(iov_test_file);
        return -1;
    }

    if(_test_fprintf() < 0) {
        fprintf(stdout, "########## _test_fprintf() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_fscanf() < 0) {
        fprintf(stdout, "########## _test_fscanf() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_chmod() < 0) {
        fprintf(stdout, "########## _test_fchmod() failed\n");
        unlink("testfile");
        return -1;
    }

    if(_test_fstat() < 0) {
        fprintf(stdout, "########## _test_fstat() failed\n");
        unlink("testfile");
        return -1;
    }
    unlink("testfile");
    fprintf(stdout, "########## file-io test passed! ##########\n");
    return 0;
}
