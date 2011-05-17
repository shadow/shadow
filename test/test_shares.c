/*
 * test_shares.c
 *
 *  Created on: Jan 17, 2011
 *      Author: rob
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define NUMITER 1000000
#define MSGSIZE 1500
#define BYTES_PER_GIGABYTE 1073741824

typedef struct mem_msg_s {
	char buf[MSGSIZE];
} mem_msg_t, *mem_msg_tp;

/* fills buffer with size random characters */
static void fill_char_buffer(char* buffer, int size) {
	for(int i = 0; i < size; i++) {
		int n = rand() % 26;
		buffer[i] = 'a' + n;
	}
}

static void report(char* msg, struct timeval *tstart, struct timeval *tend) {
    double t = (tend->tv_sec - tstart->tv_sec) + (tend->tv_usec - tstart->tv_usec) / 1e6;
    printf("%s in %.6f seconds\n", msg, t);
}

static void sysv_shm(void** msgbuf, int num_elements, int element_size) {
	for(int i = 0; i < NUMITER; i++) {
		int shmid = shmget(IPC_PRIVATE, element_size, IPC_CREAT | IPC_EXCL | 0644);
		mem_msg_tp msg = (mem_msg_tp) shmat(shmid, NULL, 0);
		memcpy(msg->buf, msgbuf[i%num_elements], element_size);
		shmdt(msg);
		shmctl(shmid, IPC_RMID, NULL);
	}
}

static void mmapped_anon(void** msgbuf, int num_elements, int element_size) {
	for(int i = 0; i < NUMITER; i++) {
		mem_msg_tp msg = (mem_msg_tp) mmap(NULL, element_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		memcpy(msg->buf, msgbuf[i%num_elements], element_size);
		munmap(msg, element_size);
	}
}

static void mmapped_posix(void** msgbuf, int num_elements, int element_size) {
	for(int i = 0; i < NUMITER; i++) {
		int fd = shm_open("/dvn-shm-1", O_RDWR|O_CREAT|O_TRUNC, 0666);
		ftruncate(fd, element_size);
		mem_msg_tp msg = (mem_msg_tp) mmap(NULL, element_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		memcpy(msg->buf, msgbuf[i%num_elements], element_size);
		munmap(msg, element_size);
		shm_unlink("/dvn-shm-1");
		close(fd);
	}
}

static void std_malloc(void** msgbuf, int num_elements, int element_size) {
	for(int i = 0; i < NUMITER; i++) {
		mem_msg_tp msg = (mem_msg_tp) malloc(element_size);
		memcpy(msg->buf, msgbuf[i%num_elements], element_size);
		free(msg);
	}
}

int main(int argc, char **argv) {
	struct timeval tstart, tend;

	int element_size = sizeof(mem_msg_t);
	printf("getting array of messages\n");
	int num_elements = BYTES_PER_GIGABYTE / element_size;
	void** msgbuf = malloc(num_elements * sizeof(void*));
	for(int i = 0; i < num_elements; i++) {
		msgbuf[i] = malloc(element_size);
		fill_char_buffer(msgbuf[i], element_size);
	}

	printf("running std_malloc\n");
	gettimeofday(&tstart, NULL);
	std_malloc(msgbuf, num_elements, element_size);
	gettimeofday(&tend, NULL);
	report("std_malloc", &tstart, &tend);

	printf("running mmapped_posix\n");
	gettimeofday(&tstart, NULL);
	mmapped_posix(msgbuf, num_elements, element_size);
	gettimeofday(&tend, NULL);
	report("mmapped_posix", &tstart, &tend);

	printf("running mmapped_anon\n");
	gettimeofday(&tstart, NULL);
	mmapped_anon(msgbuf, num_elements, element_size);
	gettimeofday(&tend, NULL);
	report("mmapped_anon", &tstart, &tend);

	printf("running sysv_shm\n");
	gettimeofday(&tstart, NULL);
	sysv_shm(msgbuf, num_elements, element_size);
	gettimeofday(&tend, NULL);
	report("sysv_shm", &tstart, &tend);

	for(int i = 0; i < num_elements; i++) {
		free(msgbuf[i]);
	}

	return 0;
}
