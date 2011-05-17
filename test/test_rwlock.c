/*
 * test_rwlock.c
 *
 *  Created on: Feb 1, 2011
 *      Author: jansen
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>

#include "rwlock.h"

static void concurrent_locking(rwlock_tp lock) {
	int num_runs = 1000000;
	int num_reads_between_write = 100;

	for(int i = 0; i < num_runs; i++) {
		for(int j = 0; j < num_reads_between_write; j++) {
			rwlock_readlock(lock);
		}
		for(int j = 0; j < num_reads_between_write; j++) {
			rwlock_readunlock(lock);
		}
		rwlock_writelock(lock);
		assert(lock->writers_active == 1);
		rwlock_writeunlock(lock);
	}
}

static void test_concurrent() {
	/* put our rwlock in anonymous shared memory */
	rwlock_tp lock = mmap(NULL, sizeof(rwlock_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	assert(rwlock_init(lock, 1) == RWLOCK_SUCCESS);

	pid_t parent_pid = getpid();
	pid_t pid = fork();
	if(pid == -1) {
		perror("test_concurrent fork");
		exit(EXIT_FAILURE);
	}
	if(pid == 0) {
		/* child process */
		uint32_t child_pid = getpid();
		printf("child %u spawned\n", child_pid);

		printf("child %u working\n", child_pid);
		concurrent_locking(lock);

		printf("child %u exiting\n", child_pid);
		exit(EXIT_SUCCESS);
	} else {
		/* parent process */
		pid_t child_pid = pid;

		printf("parent %u working\n", parent_pid);
		concurrent_locking(lock);

		printf("parent %u waiting for child %u to exit\n", parent_pid, child_pid);

		int status = 0;
		int result = 0;
		result = waitpid(child_pid, &status, 0);
		assert(result == child_pid);
		assert(WIFEXITED(status));

		printf("parent %u done\n", parent_pid);
	}

	assert(rwlock_destroy(lock) == RWLOCK_SUCCESS);
	munmap(lock, sizeof(rwlock_t));
}

static void test_time() {
	int num_runs = 10000000;
	double t = 0;

	pthread_rwlockattr_t plockattr;
	pthread_rwlockattr_init(&plockattr);
	pthread_rwlockattr_setpshared(&plockattr, PTHREAD_PROCESS_SHARED);

	pthread_rwlock_t plock;
	pthread_rwlock_init(&plock, &plockattr);

	struct timeval tstart, tend;
	gettimeofday(&tstart, NULL);
	for(int i = 0; i < num_runs; i++) {
		pthread_rwlock_rdlock(&plock);
		pthread_rwlock_unlock(&plock);
		pthread_rwlock_wrlock(&plock);
		pthread_rwlock_unlock(&plock);
	}
	gettimeofday(&tend, NULL);

	pthread_rwlockattr_destroy(&plockattr);
	pthread_rwlock_destroy(&plock);


	t = (tend.tv_sec - tstart.tv_sec) + (tend.tv_usec - tstart.tv_usec) / 1e6;
	printf("pthread time for %i [readlock,readunlock,writelock,writeunlock] in %.6f\n", num_runs, t);

	rwlock_t rwlock;
	rwlock_init(&rwlock, 1);

	gettimeofday(&tstart, NULL);
	for(int i = 0; i < num_runs; i++) {
		rwlock_readlock(&rwlock);
		rwlock_readunlock(&rwlock);
		rwlock_writelock(&rwlock);
		rwlock_writeunlock(&rwlock);
	}
	gettimeofday(&tend, NULL);

	rwlock_destroy(&rwlock);

	t = (tend.tv_sec - tstart.tv_sec) + (tend.tv_usec - tstart.tv_usec) / 1e6;
	printf("rwlock time for %i [readlock,readunlock,writelock,writeunlock] in %.6f\n", num_runs, t);
}

int main(int argc, char **argv) {
	rwlock_t l;
	assert(rwlock_init(&l, 0) == RWLOCK_SUCCESS);
	assert(rwlock_destroy(&l) == RWLOCK_SUCCESS);

	assert(rwlock_init(&l, 0) == RWLOCK_SUCCESS);
	assert(rwlock_readunlock(&l) == RWLOCK_SUCCESS);
	assert(rwlock_writeunlock(&l) == RWLOCK_SUCCESS);

	assert(rwlock_readlock(&l) == RWLOCK_SUCCESS);
	assert(rwlock_readlock(&l) == RWLOCK_SUCCESS);
	assert(l.readers_active == 2);

	assert(rwlock_readunlock(&l) == RWLOCK_SUCCESS);
	assert(rwlock_readunlock(&l) == RWLOCK_SUCCESS);
	assert(l.readers_active == 0);

	assert(rwlock_writelock(&l) == RWLOCK_SUCCESS);
	assert(l.writers_active == 1);
	assert(rwlock_writeunlock(&l) == RWLOCK_SUCCESS);
	assert(l.writers_active == 0);

	int result = 0;
	result = rwlock_destroy(&l);
	assert(result == RWLOCK_SUCCESS);

	test_concurrent();
	test_time();

	printf("All tests successful.\n");

	return 0;
}
