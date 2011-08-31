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

#ifndef RWLOCK_MGR_H_
#define RWLOCK_MGR_H_

#include <glib.h>
#include <stdint.h>

enum rwlock_mgr_status {
	RWLOCK_MGR_SUCCESS=0, RWLOCK_MGR_ERROR=-1,
	RWLOCK_MGR_ERR_INVALID_MGR, RWLOCK_MGR_ERR_INVALID_TYPE, RWLOCK_MGR_ERR_INVALID_COMMAND
};

enum rwlock_mgr_type {
	RWLOCK_MGR_TYPE_CUSTOM, RWLOCK_MGR_TYPE_PTHREAD, RWLOCK_MGR_TYPE_SEMAPHORE
};

enum rwlock_mgr_command {
	RWLOCK_MGR_COMMAND_READLOCK, RWLOCK_MGR_COMMAND_READUNLOCK,
	RWLOCK_MGR_COMMAND_WRITELOCK, RWLOCK_MGR_COMMAND_WRITEUNLOCK
};

typedef struct rwlock_mgr_s {
	enum rwlock_mgr_type type;
	gchar lock[];
} rwlock_mgr_t, *rwlock_mgr_tp;

rwlock_mgr_tp rwlock_mgr_create(enum rwlock_mgr_type type, guint8 is_process_shared);
enum rwlock_mgr_status rwlock_mgr_destroy(rwlock_mgr_tp lmgr);
enum rwlock_mgr_status rwlock_mgr_init(rwlock_mgr_tp lmgr, enum rwlock_mgr_type type, guint8 is_process_shared);
enum rwlock_mgr_status rwlock_mgr_uninit(rwlock_mgr_tp lmgr);
ssize_t rwlock_mgr_sizeof(enum rwlock_mgr_type type);
enum rwlock_mgr_status rwlock_mgr_lockcontrol(rwlock_mgr_tp lmgr, enum rwlock_mgr_command command);

#define rwlock_mgr_readlock(lmgr) rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READLOCK)
#define rwlock_mgr_readunlock(lmgr) rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_READUNLOCK)
#define rwlock_mgr_writelock(lmgr) rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITELOCK)
#define rwlock_mgr_writeunlock(lmgr) rwlock_mgr_lockcontrol(lmgr, RWLOCK_MGR_COMMAND_WRITEUNLOCK)

#endif /* RWLOCK_MGR_H_ */
