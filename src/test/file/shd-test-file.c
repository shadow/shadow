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
