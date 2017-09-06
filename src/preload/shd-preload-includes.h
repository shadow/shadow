/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_PRELOAD_SHD_PRELOAD_INCLUDES_H_
#define SRC_PRELOAD_SHD_PRELOAD_INCLUDES_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <malloc.h>
#include <ifaddrs.h>
#include <sys/epoll.h>
//#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <linux/sockios.h>
#include <features.h>

#include <malloc.h>
#include <pthread.h>

#if !defined __USE_LARGEFILE64
typedef off_t off64_t;
#endif

#endif /* SRC_PRELOAD_SHD_PRELOAD_INCLUDES_H_ */
