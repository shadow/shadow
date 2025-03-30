/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#define _POSIX_C_SOURCE 199309L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

static int test_randomOpenRead(const char *filename) {
    unsigned char buf[1];
    buf[0] = 0;

    int fd = open(filename, O_RDONLY);
    if(fd < 0) {
        return EXIT_FAILURE;
    }

    ssize_t sz = read(fd, buf, 1);
    if(sz != 1) {
        return EXIT_FAILURE;
    }

    fprintf(stdout, "%s\t: %X\n", filename, *buf);
    close(fd);

    return EXIT_SUCCESS;
}

static int test_randomFOpenFRead(const char *filename) {
    unsigned char buf[1];
    buf[0] = 0;

    FILE *fp = fopen(filename, "r");
    if(fp == NULL) {
        return EXIT_FAILURE;
    }

    size_t sz = fread(buf, 1, 1, fp);
    if(sz != 1) {
        return EXIT_FAILURE;
    }

    fprintf(stdout, "%s\t: %X\n", filename, *buf);

    fclose(fp);

    return EXIT_SUCCESS;
}

static int _test_fopen() {
    /* this should result in deterministic behavior */
    if(test_randomFOpenFRead("/dev/random") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    if(test_randomFOpenFRead("/dev/urandom") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
//    if(test_randomFOpenFRead("/dev/srandom") == EXIT_FAILURE) {
//        return EXIT_FAILURE;
//    }
    return EXIT_SUCCESS;
}

static int _test_open() {
    /* this should result in deterministic behavior */
    if(test_randomOpenRead("/dev/random") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    if(test_randomOpenRead("/dev/urandom") == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
//    if(test_randomOpenRead("/dev/srandom") == EXIT_FAILURE) {
//        return EXIT_FAILURE;
//    }
    return EXIT_SUCCESS;
}

static int _test_aux_at_random() {
    uint8_t* at_random = (void*)getauxval(AT_RANDOM);
    if (at_random == NULL) {
        fprintf(stdout, "getauxval(AT_RANDOM) is NULL\n");
        return EXIT_FAILURE;
    }
    printf("*AT_RANDOM (via libc): ");
    for (int i = 0; i < 16; ++i) {
        printf(" %02x", at_random[i]);
    }
    printf("\n");
    return EXIT_SUCCESS;
}

typedef struct _ThreadPIDs ThreadPIDs;
struct _ThreadPIDs {
    int pid;
    int ppid;
    int tid;
};

static void* _test_runThread(void* arg) {
    ThreadPIDs* tids = (ThreadPIDs*)arg;
    tids->pid = (int)getpid();
    tids->ppid = (int)getppid();
    tids->tid = (int)syscall(SYS_gettid);
    return NULL;
}

static int _test_getPID() {
#define NUMTHREADS 2
    pthread_t threads[NUMTHREADS];
    ThreadPIDs tids[NUMTHREADS];

    memset(&threads[0], 0, NUMTHREADS*sizeof(pthread_t));
    memset(&tids[0], 0, NUMTHREADS*sizeof(ThreadPIDs));

    for(int i = 0; i < NUMTHREADS; i++) {
        int retval = pthread_create(&threads[i], NULL, _test_runThread, (void*)&tids[i]);
        if(retval < 0) {
            fprintf(stdout, "error %i in pthread_create: %s\n",
                    errno, gai_strerror(errno));
            return EXIT_FAILURE;
        }

        fprintf(stdout, "created thread %i\n", i);
    }

    for(int i = 0; i < NUMTHREADS; i++) {
        int retval = pthread_join(threads[i], NULL);
        if(retval < 0) {
            fprintf(stdout, "error %i in pthread_join: %s\n",
                    errno, gai_strerror(errno));
            return EXIT_FAILURE;
        }

        fprintf(stdout, "joined thread %i\n", i);
    }

    ThreadPIDs myPIDs;
    myPIDs.pid = (int)getpid();
    myPIDs.ppid = (int)getppid();
    myPIDs.tid = (int)syscall(SYS_gettid);

    fprintf(stdout, "PIDS: Main: pid=%i, ppid=%i, tid=%i "
            "Thread1: pid=%i, ppid=%i, tid=%i "
            "Thread2: pid=%i, ppid=%i, tid=%i\n",
            myPIDs.pid, myPIDs.ppid, myPIDs.tid,
            tids[0].pid, tids[0].ppid, tids[0].tid,
            tids[1].pid, tids[1].ppid, tids[1].tid);

    return EXIT_SUCCESS;
}

static int _test_nameAddress() {
    /* first get our hostname */
    char hostname[1024];
    memset(hostname, 0, 1024);

    int result = gethostname(hostname, 1023);
    if(result < 0) {
        fprintf(stdout, "gethostname() returned %i with errno=%i: %s\n",
                result, errno, gai_strerror(errno));
        return EXIT_FAILURE;
    }

    fprintf(stdout, "gethostname() returned hostname %s\n", hostname);

    /* now get our IP address */
    struct addrinfo* info = NULL;

    /* this call does the network query */
    result = getaddrinfo((char*) hostname, NULL, NULL, &info);

    if (result < 0) {
        fprintf(stdout, "getaddrinfo() returned %i with errno=%i: %s\n",
                result, errno, gai_strerror(errno));
        return EXIT_FAILURE;
    }

    in_addr_t ip = ((struct sockaddr_in*) (info->ai_addr))->sin_addr.s_addr;
    freeaddrinfo(info);

    /* convert the ip to a string so we can log it */
    char netbuf[INET_ADDRSTRLEN+1];
    memset(netbuf, 0, INET_ADDRSTRLEN+1);
    const char* netresult = inet_ntop(AF_INET, &ip, netbuf, INET_ADDRSTRLEN);

    fprintf(stdout, "getaddrinfo() returned ip address %s\n", netresult);

    /* now test a reverse dns lookup */
    struct sockaddr_in addrbuf;
    memset(&addrbuf, 0, sizeof(struct sockaddr_in));
    addrbuf.sin_addr.s_addr = ip;
    addrbuf.sin_family = AF_INET;

    char namebuf[256];
    memset(namebuf, 0, 256);

    /* this call does the network query */
    result = getnameinfo((struct sockaddr*)&addrbuf, (socklen_t) sizeof(struct sockaddr_in),
            namebuf, (socklen_t) 255, NULL, 0, 0);

    if (result < 0) {
        fprintf(stdout, "getnameinfo() returned %i with errno=%i: %s\n",
                result, errno, gai_strerror(errno));
        return EXIT_FAILURE;
    }

    fprintf(stdout, "getnameinfo() returned name %s\n", namebuf);

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## determinism test starting ##########\n");

    fprintf(stdout, "starting _test_open()\n");
    if (_test_open() < 0) {
        fprintf(stdout, "########## _test_open() on random devices failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_open() passed\n");

    fprintf(stdout, "starting _test_fopen()\n");
    if (_test_fopen() < 0) {
        fprintf(stdout, "########## _test_fopen() on random devices failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_fopen() passed\n");

    fprintf(stdout, "starting _test_getPID()\n");
    if (_test_getPID() < 0) {
        fprintf(stdout, "########## _test_getPID() failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_getPID() passed\n");

    fprintf(stdout, "starting _test_nameAddress()\n");
    if (_test_nameAddress() < 0) {
        fprintf(stdout, "########## _test_nameAddress() failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_nameAddress() passed\n");

    fprintf(stdout, "starting _test_aux_at_random()\n");
    if (_test_aux_at_random() < 0) {
        fprintf(stdout, "########## _test_aux_at_random() failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "_test_aux_at_random() passed\n");

    fprintf(stdout, "########## determinism test passed! ##########\n");

    return EXIT_SUCCESS;
}
