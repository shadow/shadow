/*
 * test_shmemcabinet.c
 *
 *  Created on: Jan 24, 2011
 *      Author: rob
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "shmcabinet_internal.h"
#include "shmcabinet.h"

typedef struct cabtest_s {
	int one;
	int two;
	char a;
	int slot_id;
	char data[300];
} cabtest_t, *cabtest_tp;

void shmcabinet_print(shmcabinet_tp cabinet) {
	for(int i = 0; i < cabinet->num_slots; i++) {
		shmcabinet_slot_tp slot = shmcabinet_ID_TO_SLOT(cabinet, i);
		if(slot != NULL) {
			int* theint = (int*)(((char*)slot) + sizeof(shmcabinet_slot_t));
			printf("%i\n", *theint);
		} else {
			printf("NULL\n");
		}
	}
}

void test_create(enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	shmcabinet_tp cab = shmcabinet_create(1, 1, cab_lock_type, slot_lock_type);
	assert(cab != NULL);
	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);


	cab = shmcabinet_create(10, sizeof(cabtest_t), cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	char buf[300] = "01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678";

	cabtest_tp mycts[10];
	for(int i = 0; i < 10; i++) {
		mycts[i] = shmcabinet_allocate(cab);
		assert(mycts[i] != NULL);
		assert(cab->num_slots_allocated == i+1);

		assert(shmcabinet_writelock(cab, mycts[i]) == SHMCABINET_SUCCESS);

		mycts[i]->one = 1;
		mycts[i]->two = 2;
		mycts[i]->a = 'z';

		memcpy(mycts[i]->data, buf, 300);

		assert(shmcabinet_writeunlock(cab, mycts[i]) == SHMCABINET_SUCCESS);
	}

	for(int i = 0; i < 10; i++) {
		assert(shmcabinet_readlock(cab, mycts[i]) == SHMCABINET_SUCCESS);
		assert(mycts[i]->a == 'z');
		assert(mycts[i]->one == 1);
		assert(mycts[i]->two == 2);
		assert(memcmp(mycts[i]->data, buf, 300) == 0);
		assert(shmcabinet_readunlock(cab, mycts[i]) == SHMCABINET_SUCCESS);
	}
	for(int i = 0; i < 10; i++) {
		assert(shmcabinet_close(cab, mycts[i]) == SHMCABINET_SUCCESS);
	}

	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);
}

void test_map(enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	assert(shmcabinet_map(0, 0, 0) == NULL);

	shmcabinet_tp cab = shmcabinet_create(1, 1, cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	shmcabinet_tp mappedcab = shmcabinet_map(cab->pid, cab->id, cab->size);
	assert(mappedcab != NULL);
	assert(cab->num_opened == 2);

	assert(mappedcab != NULL);
	assert(cab->id == mappedcab->id);
	assert(cab->pid == mappedcab->pid);
	assert(cab->num_opened == mappedcab->num_opened);
	assert(cab->num_slots == mappedcab->num_slots);
	assert(cab->num_slots_allocated == mappedcab->num_slots_allocated);
	assert(cab->size == mappedcab->size);
	assert(cab->slot_size == mappedcab->slot_size);

	void* payload1 = shmcabinet_allocate(cab);
	assert(payload1 != NULL);

	uint32_t id = shmcabinet_get_id(cab, payload1);
	assert(id != SHMCABINET_ERROR);

	void* payload2 = shmcabinet_open(mappedcab, id);
	assert(payload2 != NULL);

	*((int*)payload1) = 123456;
	assert(*((int*)payload1) == 123456);
	assert(*((int*)payload2) == 123456);

	shmcabinet_close(mappedcab, payload2);
	shmcabinet_close(cab, payload1);

	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);
	assert(shmcabinet_unmap(mappedcab) == SHMCABINET_SUCCESS);
}

void test_unmap(enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	assert(shmcabinet_unmap(NULL) == SHMCABINET_ERROR);

	shmcabinet_tp cab = shmcabinet_create(1, 1, cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	shmcabinet_tp mappedcab = shmcabinet_map(cab->pid, cab->id, cab->size);
	assert(mappedcab != NULL);

	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);
	assert(shmcabinet_unmap(mappedcab) == SHMCABINET_SUCCESS);

	/* unmap the other way around */

	cab = shmcabinet_create(1, 1, cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	mappedcab = shmcabinet_map(cab->pid, cab->id, cab->size);
	assert(mappedcab != NULL);

	assert(shmcabinet_unmap(mappedcab) == SHMCABINET_SUCCESS);
	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);

	/* todo: really should assert that no zombies /dev/shm/dvn-shmcabinet-* exist */
}

void test_alloc(enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	assert(shmcabinet_allocate(NULL) == NULL);

	shmcabinet_tp cab = shmcabinet_create(1, 1, cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	void* payload1 = shmcabinet_allocate(cab);
	assert(payload1 != NULL);
	assert(cab->num_slots_allocated == 1);

	void* payload2 = shmcabinet_allocate(cab);
	assert(payload2 == NULL);
	assert(cab->num_slots_allocated == 1);
	assert(shmcabinet_get_id(cab, payload2) == SHMCABINET_ERROR);
	assert(shmcabinet_readunlock(cab, payload2) == SHMCABINET_ERROR);
	assert(shmcabinet_writeunlock(cab, payload2) == SHMCABINET_ERROR);

	shmcabinet_close(cab, payload1);
	payload2 = NULL;

	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);
}

void test_open_close(enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	assert(shmcabinet_open(NULL, 0) == NULL);
	assert(shmcabinet_close(NULL, 0) == SHMCABINET_ERROR);

	shmcabinet_tp cab = shmcabinet_create(1, sizeof(int), cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	void* payload1 = shmcabinet_allocate(cab);
	assert(payload1 != NULL);

	uint32_t id = shmcabinet_get_id(cab, payload1);
	assert(id != SHMCABINET_ERROR);

	assert(shmcabinet_open(cab, 100) == NULL);
	assert(shmcabinet_open(cab, 1) == NULL);

	void* payload2 = shmcabinet_open(cab, id);
	assert(payload2 != NULL);

	void* payload3 = shmcabinet_open(cab, id);
	assert(payload3 != NULL);

	*((int*)payload2) = 123456;
	assert(*((int*)payload2) == 123456);
	assert(*((int*)payload3) == 123456);

	assert(cab->num_slots_allocated == 1);
	assert(shmcabinet_close(cab, payload1) == SHMCABINET_SUCCESS);
	assert(cab->num_slots_allocated == 1);
	assert(shmcabinet_close(cab, payload2) == SHMCABINET_SUCCESS);
	assert(cab->num_slots_allocated == 1);
	assert(shmcabinet_close(cab, payload3) == SHMCABINET_SUCCESS);
	assert(cab->num_slots_allocated == 0);
	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);

	payload3 = NULL;
}

void test_lock_unlock(enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	assert(shmcabinet_readlock(NULL, NULL) == SHMCABINET_ERROR);
	assert(shmcabinet_readunlock(NULL, NULL) == SHMCABINET_ERROR);
	assert(shmcabinet_writelock(NULL, NULL) == SHMCABINET_ERROR);
	assert(shmcabinet_writeunlock(NULL, NULL) == SHMCABINET_ERROR);

	shmcabinet_tp cab = shmcabinet_create(1, sizeof(int), cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	void* payload1 = shmcabinet_allocate(cab);
	assert(payload1 != NULL);

	/* for some strange reason, pthread rwlocks go nuts if you try to unlock
	 * a lock that is not locked...
	 */
	if(cab_lock_type != RWLOCK_MGR_TYPE_PTHREAD) {
		assert(shmcabinet_readunlock(cab, payload1) == SHMCABINET_SUCCESS);
		assert(shmcabinet_writeunlock(cab, payload1) == SHMCABINET_SUCCESS);
	}
	assert(shmcabinet_readlock(cab, payload1) == SHMCABINET_SUCCESS);
	assert(shmcabinet_readunlock(cab, payload1) == SHMCABINET_SUCCESS);
	assert(shmcabinet_writelock(cab, payload1) == SHMCABINET_SUCCESS);
	assert(shmcabinet_writeunlock(cab, payload1) == SHMCABINET_SUCCESS);

	shmcabinet_close(cab, payload1);

	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);
}

void test_getinfo(enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	/* nothing should happen */
	assert(shmcabinet_get_info(NULL, NULL) == SHMCABINET_ERROR);

	shmcabinet_tp cab = shmcabinet_create(1, 1, cab_lock_type, slot_lock_type);
	assert(cab != NULL);

	shmcabinet_info_t info;
	shmcabinet_get_info(cab, &info);

	assert(cab->pid == info.process_id);
	assert(cab->id == info.cabinet_id);
	assert(cab->size == info.cabinet_size);

	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);
}

void concurrent_locking(shmcabinet_tp cab, cabtest_tp* ctptrs, int num_items) {
	for(int i = 0; i < 1000; i++) {
		int j = i % num_items;
		assert(ctptrs[j] != NULL);
		assert(shmcabinet_writelock(cab, ctptrs[j]) == SHMCABINET_SUCCESS);
		ctptrs[j]->one = i;
		assert(shmcabinet_writeunlock(cab, ctptrs[j]) == SHMCABINET_SUCCESS);
		assert(shmcabinet_readlock(cab, ctptrs[j]) == SHMCABINET_SUCCESS);
		assert(shmcabinet_readunlock(cab, ctptrs[j]) == SHMCABINET_SUCCESS);
	}
}

void concurrent_alloc(shmcabinet_tp cab, int num_items, pid_t pid) {
	if(num_items >= 2){
		int num_allocs = num_items/2;
		cabtest_tp ctptrs[num_allocs];
		for(int i = 0; i < 1000; i++) {
			for(int k = 0; k < num_allocs; k++) {
				ctptrs[k] = shmcabinet_allocate(cab);
				assert(ctptrs[k] != NULL);
				assert(shmcabinet_readlock(cab, ctptrs[k]) == SHMCABINET_SUCCESS);
			}
			for(int k = 0; k < num_allocs; k++) {
				assert(shmcabinet_readunlock(cab, ctptrs[k]) == SHMCABINET_SUCCESS);
				assert(shmcabinet_close(cab, ctptrs[k]) == SHMCABINET_SUCCESS);
			}
		}
	}
}

void test_concurrent(int num_items, enum rwlock_mgr_type cab_lock_type, enum rwlock_mgr_type slot_lock_type) {
	shmcabinet_tp cab = shmcabinet_create(num_items, sizeof(cabtest_t), cab_lock_type, slot_lock_type);
	assert(cab != NULL);
	uint32_t cab_size = cab->size;
	/* TODO: fragile - we assume shmcabinet increments cab id */
	uint32_t cab_id = cab->id + 1;
	assert(shmcabinet_unmap(cab) == SHMCABINET_SUCCESS);
	cab = NULL;

	/* both named sems start locked */
	char* parentlockname = "shmcabinet-test-parentlock";
	sem_t* parentlock = sem_open(parentlockname, O_CREAT, 0600, 0);
	assert(parentlock != SEM_FAILED);
	char* childlockname = "shmcabinet-test-childlock";
	sem_t* childlock = sem_open(childlockname, O_CREAT, 0600, 0);
	assert(childlock != SEM_FAILED);

	char buf[300] = "01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678";

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

		/* wait until parent has created the shm object*/
		assert(sem_wait(parentlock) == 0);

		/* we attach to the parents cabinet */
		shmcabinet_tp childcab = shmcabinet_map(parent_pid, cab_id, cab_size);
		assert(childcab != NULL);
		assert(childcab->num_opened == 2);

		printf("child %u mapped cabinet id %u\n", child_pid, childcab->id);

		/* now open all slots to get handles */
		cabtest_tp ctptrs[num_items];
		for(int i = 0; i < num_items; i++) {
			ctptrs[i] = shmcabinet_open(childcab, i);
			assert(ctptrs[i] != NULL);
		}

		/* verify we have the same info as parent */
		for(int i = 0; i < num_items; i++) {
			assert(shmcabinet_readlock(childcab, ctptrs[i]) == SHMCABINET_SUCCESS);
			assert(ctptrs[i] != NULL);
			assert(ctptrs[i]->a == 'z');
			assert(ctptrs[i]->one == 1);
			assert(ctptrs[i]->two == 2);
			assert(ctptrs[i]->slot_id == i);
			assert(memcmp(ctptrs[i]->data, buf, 300) == 0);
			assert(shmcabinet_readunlock(childcab, ctptrs[i]) == SHMCABINET_SUCCESS);
		}

		printf("child %u verified cabinet id %u\n", child_pid, childcab->id);

		/* ready to work, wait for parent */
		assert(sem_post(childlock) == 0);
		assert(sem_wait(parentlock) == 0);

		printf("child %u starting concurrent_locking with cabinet id %u\n", child_pid, childcab->id);

		/* do work */
		concurrent_locking(childcab, ctptrs, num_items);

		printf("child %u finished concurrent_locking with cabinet id %u\n", child_pid, childcab->id);

		/* close slots */
		for(int i = 0; i < num_items; i++) {
			assert(shmcabinet_close(childcab, ctptrs[i]) == SHMCABINET_SUCCESS);
		}

		/* done working, wait for parent */
		assert(sem_post(childlock) == 0);
		assert(sem_wait(parentlock) == 0);

		/* wait for next test */
		assert(sem_wait(parentlock) == 0);

		printf("child %u started concurrent_alloc with cabinet id %u\n", child_pid, childcab->id);

		concurrent_alloc(childcab, num_items, child_pid);

		printf("child %u finished concurrent_alloc with cabinet id %u\n", child_pid, childcab->id);

		/* done working, wait for parent */
		assert(sem_post(childlock) == 0);
		assert(sem_wait(parentlock) == 0);

		printf("child %u unmapping...\n", child_pid);

		assert(shmcabinet_unmap(childcab) == SHMCABINET_SUCCESS);

		printf("child %u exiting\n", child_pid);
		exit(EXIT_SUCCESS);
	} else {
		/* parent process */

		pid_t child_pid = pid;

		shmcabinet_tp parentcab = shmcabinet_create(num_items, sizeof(cabtest_t), cab_lock_type, slot_lock_type);
		assert(parentcab != NULL);
		assert(parentcab->num_opened == 1);

		printf("parent %u created cabinet id %u with %u slots\n", parent_pid, parentcab->id, parentcab->num_slots);

		/* allocate */
		cabtest_tp ctptrs[num_items];
		for(int i = 0; i < num_items; i++) {
			ctptrs[i] = shmcabinet_allocate(parentcab);
			assert(ctptrs[i] != NULL);
			assert(parentcab->num_slots_allocated == i+1);

			assert(shmcabinet_writelock(parentcab, ctptrs[i]) == SHMCABINET_SUCCESS);
			ctptrs[i]->one = 1;
			ctptrs[i]->two = 2;
			ctptrs[i]->a = 'z';
			ctptrs[i]->slot_id = i;
			memcpy(ctptrs[i]->data, buf, 300);

			assert(shmcabinet_writeunlock(parentcab, ctptrs[i]) == SHMCABINET_SUCCESS);
		}

		/* now that the cab is created, its ok for child to open the shm */
		assert(sem_post(parentlock) == 0);

		/* ready to work, wait for child */
		assert(sem_post(parentlock) == 0);
		assert(sem_wait(childlock) == 0);

		printf("parent %u started concurrent_locking with cabinet id %u\n", parent_pid, parentcab->id);

		/* do work */
		concurrent_locking(parentcab, ctptrs, num_items);

		printf("parent %u finished concurrent_locking with cabinet id %u\n", parent_pid, parentcab->id);

		/* close slots */
		for(int i = 0; i < num_items; i++) {
			assert(shmcabinet_close(parentcab, ctptrs[i]) == SHMCABINET_SUCCESS);
		}

		/* my work is done, wait for child */
		assert(sem_post(parentlock) == 0);
		assert(sem_wait(childlock) == 0);

		/* get ready for next test */
		assert(parentcab->num_slots_allocated == 0);
		/* make sure I can still traverse the list */
		for(int i = 0; i < num_items; i++) {
			ctptrs[i] = shmcabinet_allocate(parentcab);
			assert(ctptrs[i] != NULL);
		}
		for(int i = 0; i < num_items; i++) {
			assert(shmcabinet_close(parentcab, ctptrs[i]) == SHMCABINET_SUCCESS);
		}

		printf("parent %u cabinet id %u intact\n", parent_pid, parentcab->id);

		/* next test ready */
		assert(sem_post(parentlock) == 0);

		printf("parent %u started concurrent_alloc with cabinet id %u\n", parent_pid, parentcab->id);

		concurrent_alloc(parentcab, num_items, parent_pid);

		printf("parent %u finished concurrent_alloc with cabinet id %u\n", parent_pid, parentcab->id);

		/* my work is done, wait for child */
		assert(sem_post(parentlock) == 0);
		assert(sem_wait(childlock) == 0);

		printf("parent %u unmapping...\n", parent_pid);
		assert(shmcabinet_unmap(parentcab) == SHMCABINET_SUCCESS);

		printf("parent %u waiting for child to exit\n", parent_pid);

		int status = 0;
		int result = 0;
		result = waitpid(child_pid, &status, 0);
		assert(result == child_pid);
		assert(WIFEXITED(status));

		printf("parent %u done\n", parent_pid);
	}

	/* POSIX sems should be de-allocated and unlinked */
	assert(sem_destroy(childlock) == 0);
	assert(sem_unlink(childlockname) == 0);
	assert(sem_destroy(parentlock) == 0);
	assert(sem_unlink(parentlockname) == 0);
	childlock = NULL;
	parentlock = NULL;
}

int main(int argc, char **argv) {
	printf("Running test_create.\n");
	test_create(RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	test_create(RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	test_create(RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);

	printf("Running test_map.\n");
	test_map(RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	test_map(RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	test_map(RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);

	printf("Running test_unmap.\n");
	test_unmap(RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	test_unmap(RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	test_unmap(RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);

	printf("Running test_alloc.\n");
	test_alloc(RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	test_alloc(RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	test_alloc(RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);

	printf("Running test_open_close.\n");
	test_open_close(RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	test_open_close(RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	test_open_close(RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);

	printf("Running test_lock_unlock.\n");
	test_lock_unlock(RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	test_lock_unlock(RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	test_lock_unlock(RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);

	printf("Running test_getinfo.\n");
	test_getinfo(RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	test_getinfo(RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	test_getinfo(RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);

	printf("Running test_concurrent.\n");
	for(int i = 1; i < 10; i++) {
		test_concurrent(i, RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	}
	for(int i = 10; i <= 10000; i *= 10) {
		test_concurrent(i, RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_CUSTOM);
	}

	for(int i = 1; i < 10; i++) {
		test_concurrent(i, RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	}
	for(int i = 10; i <= 10000; i *= 10) {
		test_concurrent(i, RWLOCK_MGR_TYPE_SEMAPHORE, RWLOCK_MGR_TYPE_SEMAPHORE);
	}

	for(int i = 1; i < 10; i++) {
		test_concurrent(i, RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);
	}
	for(int i = 10; i <= 10000; i *= 10) {
		test_concurrent(i, RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_PTHREAD);
	}

	printf("All tests successful.\n");

	return 0;
}
