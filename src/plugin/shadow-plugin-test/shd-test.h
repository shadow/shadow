/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TEST_H_
#define SHD_TEST_H_

#include <glib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#if 1 /* #ifdef DEBUG */
#define TEST_MAGIC 0xABBABAAB
#define TEST_ASSERT(obj) g_assert(obj && (obj->magic == TEST_MAGIC))
#else
#define TEST_MAGIC 0
#define TEST_ASSERT(obj)
#endif

#define TEST_LOG_ERROR 1
#define TEST_LOG_WARNING 2
#define TEST_LOG_INFO 3
#define TEST_LOG_DEBUG 4

#define test_error(...)     _test_log(TEST_LOG_ERROR, __FUNCTION__, __VA_ARGS__)
#define test_warning(...)   _test_log(TEST_LOG_WARNING, __FUNCTION__, __VA_ARGS__)
#define test_info(...)      _test_log(TEST_LOG_INFO, __FUNCTION__, __VA_ARGS__)
#define test_debug(...)     _test_log(TEST_LOG_DEBUG, __FUNCTION__, __VA_ARGS__)

#define TEST_LISTEN_PORT 8998

typedef struct _Test Test;

#endif /* SHD_TEST_H_ */
