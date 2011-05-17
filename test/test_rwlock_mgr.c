/*
 * test_rwlock_mgr.c
 *
 * TODO refactor.
 *
 *  Created on: Feb 6, 2011
 *      Author: rob
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include "rwlock_mgr.h"

void test_init_uninit() {
	rwlock_mgr_tp lmgr = NULL;

	/* test all valid inputs */
	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_CUSTOM));
	assert(rwlock_mgr_init(lmgr, RWLOCK_MGR_TYPE_CUSTOM, 1) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_uninit(lmgr) == RWLOCK_MGR_SUCCESS);
	free(lmgr);

	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_CUSTOM));
	assert(rwlock_mgr_init(lmgr, RWLOCK_MGR_TYPE_CUSTOM, 0) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_uninit(lmgr) == RWLOCK_MGR_SUCCESS);
	free(lmgr);

	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_PTHREAD));
	assert(rwlock_mgr_init(lmgr, RWLOCK_MGR_TYPE_PTHREAD, 1) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_uninit(lmgr) == RWLOCK_MGR_SUCCESS);
	free(lmgr);

	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_PTHREAD));
	assert(rwlock_mgr_init(lmgr, RWLOCK_MGR_TYPE_PTHREAD, 0) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_uninit(lmgr) == RWLOCK_MGR_SUCCESS);
	free(lmgr);

	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_SEMAPHORE));
	assert(rwlock_mgr_init(lmgr, RWLOCK_MGR_TYPE_SEMAPHORE, 1) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_uninit(lmgr) == RWLOCK_MGR_SUCCESS);
	free(lmgr);

	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_SEMAPHORE));
	assert(rwlock_mgr_init(lmgr, RWLOCK_MGR_TYPE_SEMAPHORE, 0) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_uninit(lmgr) == RWLOCK_MGR_SUCCESS);
	free(lmgr);

	/* test invalid inputs */
	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_CUSTOM));
	assert(rwlock_mgr_init(lmgr, 99999, 1) == RWLOCK_MGR_ERR_INVALID_TYPE);
	free(lmgr);

	lmgr = malloc(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_CUSTOM));
	assert(rwlock_mgr_init(lmgr, 99999, 0) == RWLOCK_MGR_ERR_INVALID_TYPE);
	free(lmgr);

	assert(rwlock_mgr_init(NULL, 99999, 0) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_uninit(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
}

void test_create_destroy() {
	rwlock_mgr_tp lmgr = NULL;

	/* test valid inputs */
	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_CUSTOM, 1);
	assert(lmgr != NULL);
	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_CUSTOM, 0);
	assert(lmgr != NULL);
	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_PTHREAD, 1);
	assert(lmgr != NULL);
	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_PTHREAD, 0);
	assert(lmgr != NULL);
	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_SEMAPHORE, 1);
	assert(lmgr != NULL);
	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_SEMAPHORE, 0);
	assert(lmgr != NULL);
	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	/* test invalid inputs */
	lmgr = rwlock_mgr_create(99999, 1);
	assert(lmgr == NULL);
	assert(rwlock_mgr_destroy(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
}

void test_sizeof() {
	assert(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_CUSTOM) != -1);
	assert(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_PTHREAD) != -1);
	assert(rwlock_mgr_sizeof(RWLOCK_MGR_TYPE_SEMAPHORE) != -1);
	assert(rwlock_mgr_sizeof(99999) == -1);
}

void test_lockcontrol() {
	rwlock_mgr_tp lmgr = NULL;

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_CUSTOM, 1);
	assert(lmgr != NULL);

	assert(rwlock_mgr_readlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_readunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READLOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_writelock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_writeunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITELOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITEUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_readlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_readunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writelock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writeunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_lockcontrol(lmgr, 99999) == RWLOCK_MGR_ERR_INVALID_COMMAND);
	assert(rwlock_mgr_lockcontrol(NULL, 99999) == RWLOCK_MGR_ERR_INVALID_MGR);

	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_CUSTOM, 0);
	assert(lmgr != NULL);

	assert(rwlock_mgr_readlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_readunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READLOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_writelock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_writeunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITELOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITEUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_readlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_readunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writelock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writeunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_lockcontrol(lmgr, 99999) == RWLOCK_MGR_ERR_INVALID_COMMAND);
	assert(rwlock_mgr_lockcontrol(NULL, 99999) == RWLOCK_MGR_ERR_INVALID_MGR);

	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_PTHREAD, 1);
	assert(lmgr != NULL);

	assert(rwlock_mgr_readlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_readunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READLOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_writelock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_writeunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITELOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITEUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_readlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_readunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writelock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writeunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_lockcontrol(lmgr, 99999) == RWLOCK_MGR_ERR_INVALID_COMMAND);
	assert(rwlock_mgr_lockcontrol(NULL, 99999) == RWLOCK_MGR_ERR_INVALID_MGR);

	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_PTHREAD, 0);
	assert(lmgr != NULL);

	assert(rwlock_mgr_readlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_readunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READLOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_writelock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_writeunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITELOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITEUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_readlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_readunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writelock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writeunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_lockcontrol(lmgr, 99999) == RWLOCK_MGR_ERR_INVALID_COMMAND);
	assert(rwlock_mgr_lockcontrol(NULL, 99999) == RWLOCK_MGR_ERR_INVALID_MGR);

	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_SEMAPHORE, 1);
	assert(lmgr != NULL);

	assert(rwlock_mgr_readlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_readunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READLOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_writelock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_writeunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITELOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITEUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_readlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_readunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writelock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writeunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_lockcontrol(lmgr, 99999) == RWLOCK_MGR_ERR_INVALID_COMMAND);
	assert(rwlock_mgr_lockcontrol(NULL, 99999) == RWLOCK_MGR_ERR_INVALID_MGR);

	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);

	lmgr = rwlock_mgr_create(RWLOCK_MGR_TYPE_SEMAPHORE, 0);
	assert(lmgr != NULL);

	assert(rwlock_mgr_readlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_readunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READLOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_writelock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_writeunlock(lmgr) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITELOCK) == RWLOCK_MGR_SUCCESS);
	assert(rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITEUNLOCK) == RWLOCK_MGR_SUCCESS);

	assert(rwlock_mgr_readlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_readunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writelock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_writeunlock(NULL) == RWLOCK_MGR_ERR_INVALID_MGR);
	assert(rwlock_mgr_lockcontrol(lmgr, 99999) == RWLOCK_MGR_ERR_INVALID_COMMAND);
	assert(rwlock_mgr_lockcontrol(NULL, 99999) == RWLOCK_MGR_ERR_INVALID_MGR);

	assert(rwlock_mgr_destroy(lmgr) == RWLOCK_MGR_SUCCESS);
}

int main(int argc, char **argv) {
	test_init_uninit();
	test_create_destroy();
	test_sizeof();
	test_lockcontrol();

	printf("All tests successful.\n");

	return 0;
}
