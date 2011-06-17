/**
 * Scallion - plug-in for The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Scallion.
 *
 * Scallion is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scallion is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scallion.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <sys/types.h>

# ifndef __daddr_t_defined
typedef __daddr_t daddr_t;
typedef __caddr_t caddr_t;
#  define __daddr_t_defined
# ifndef __u_char_defined
# endif
typedef __u_char u_char;
typedef __u_short u_short;
typedef __u_int u_int;
typedef __u_long u_long;
typedef __quad_t quad_t;
typedef __u_quad_t u_quad_t;
typedef __fsid_t fsid_t;
#  define __u_char_defined
# endif

#include <errno.h>

#include "vtor.h"
#include <shd-plugin.h>

#undef NDEBUG

#include "orconfig.h"
#include "src/or/or.h"
#include "src/common/util.h"
#include "src/common/address.h"
#include "src/common/compat_libevent.h"
#include "src/common/compat.h"
#include "src/common/container.h"
#include "src/common/ht.h"
#include "src/common/memarea.h"
#include "src/common/mempool.h"
#include "src/common/torlog.h"
#include "src/common/tortls.h"
#include "src/or/buffers.h"
#include "src/or/config.h"
#include "src/or/cpuworker.h"
#include "src/or/dirserv.h"
#include "src/or/dirvote.h"
#include "src/or/hibernate.h"
#include "src/or/rephist.h"
#include "src/or/router.h"
#include "src/or/routerparse.h"
#include "src/or/onion.h"
#include "src/or/control.h"
#include "src/or/networkstatus.h"
#include "src/common/OpenBSD_malloc_Linux.h"
#include "src/or/dns.h"
#include "src/or/circuitlist.h"
#include "src/or/policies.h"
#include "src/or/geoip.h"
#include <openssl/bn.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <time.h>
