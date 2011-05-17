/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <assert.h>
#include <sys/sem.h>
#include "global.h"
#include "ipcsync.h"

ipcsync_tp ipcsync_create(int num_mutex, int num_cond) {
	ipcsync_tp is;
	union ipcsync_semun semopts;

	is = malloc(sizeof(*is));
	if(!is)
		printfault(EXIT_NOMEM, "ipcsync_create: Out of memory");

	is->cnt_mutex = num_mutex;
	is->cnt_cond = num_cond;
	is->cnt = (num_cond * 2) + num_mutex;

	/* create the underlying semaphores */
	is->semid = semget(IPC_PRIVATE, is->cnt, 0666);

	/* init all sems - first num_mutex are mutex, rest are cond */
	for(int i = 0; i < is->cnt; i++) {
		if(i < num_mutex) {
			semopts.val = 1;
			semctl( is->semid, i, SETVAL, semopts );
		} else {
			semopts.val = 0;
			semctl( is->semid, i, SETVAL, semopts );
		}
	}

	return is;
}

void ipcsync_destroy(ipcsync_tp is) {
	semctl(is->semid, 0, IPC_RMID);

	free(is);
}

void ipcsync_mutex_lock(ipcsync_tp is, int mutex_num) {
	struct sembuf op;

	assert(mutex_num < is->cnt_mutex);

	op.sem_num = mutex_num;
	op.sem_flg = 0;
	op.sem_op = -1;
	while(semop(is->semid, &op, 1) == -1);
}

int ipcsync_mutex_trylock(ipcsync_tp is, int mutex_num) {
	struct sembuf op;

	assert(mutex_num < is->cnt_mutex);

	op.sem_num = mutex_num;
	op.sem_flg = IPC_NOWAIT;
	op.sem_op = -1;

	if(semop(is->semid, &op, 1) == -1)
		return 0;

	return 1;
}


void ipcsync_mutex_unlock(ipcsync_tp is, int mutex_num) {
	struct sembuf op;

	assert(mutex_num < is->cnt_mutex);

	op.sem_num = mutex_num;
	op.sem_flg = 0;
	op.sem_op = 1;
	while(semop(is->semid, &op, 1) == -1);
}

void ipcsync_cond_wait(ipcsync_tp is, int mutex_num, int cond_num) {
	struct sembuf semops[3];

	/* indicate waiting process */
	//(is->cond_waiters[cond_num])++;
	
	/* unlock the mutex */
	semops[0].sem_num = mutex_num;
	semops[0].sem_flg = 0;
	semops[0].sem_op = 1;

	/* add 1 to the counter for this cond var */ 
	semops[1].sem_num = is->cnt_mutex + (cond_num*2) + 1;
	semops[1].sem_flg = 0;
	semops[1].sem_op = 1;
	while(semop(is->semid, semops, 2) == -1);

	/* wait on condition */
	semops[0].sem_num = is->cnt_mutex + (cond_num*2);
	semops[0].sem_flg = 0;
	semops[0].sem_op = -1;

	/* lock mutex */
	semops[1].sem_num = mutex_num;
	semops[1].sem_flg = 0;
	semops[1].sem_op = -1;

	/* rid our counter */
	semops[2].sem_num = is->cnt_mutex + (cond_num*2) + 1;
	semops[2].sem_flg = 0;
	semops[2].sem_op = -1;

	while(semop(is->semid, semops, 3) == -1);

}

void ipcsync_cond_signal(ipcsync_tp is, int cond_num) {
	struct sembuf op;
	int waiting_procs;

	/* get the count */
	waiting_procs = semctl(is->semid, is->cnt_mutex + (cond_num*2) + 1, GETVAL);

	if(waiting_procs == 0)
		return;

	/* signal one of them */
	op.sem_num = is->cnt_mutex + (cond_num*2);
	op.sem_flg = 0;
	op.sem_op = 1;
	while(semop(is->semid, &op, 1) == -1);
}

void ipcsync_cond_bcast(ipcsync_tp is, int cond_num) {
	struct sembuf op;
	int waiting_procs;

	/* get the count */
	waiting_procs = semctl(is->semid, is->cnt_mutex + (cond_num*2) + 1, GETVAL);

	if(waiting_procs == 0)
		return;

	op.sem_num = is->cnt_mutex + (cond_num*2);
	op.sem_flg = 0;
	op.sem_op = waiting_procs;
	while(semop(is->semid, &op, 1) == -1);
}
