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
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <shadow-plugin-interface.h>

#if 1 /* #ifdef DEBUG */
#define TEST_MAGIC 0xABBABAAB
#define TEST_ASSERT(obj) g_assert(obj && (obj->magic == TEST_MAGIC))
#else
#define TEST_MAGIC 0
#define TEST_ASSERT(obj)
#endif

#define test_error(...)     if(test && test->logf){test->logf(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__);}
#define test_critical(...)  if(test && test->logf){test->logf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, __VA_ARGS__);}
#define test_warning(...)   if(test && test->logf){test->logf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, __VA_ARGS__);}
#define test_message(...)   if(test && test->logf){test->logf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, __VA_ARGS__);}
#define test_info(...)      if(test && test->logf){test->logf(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, __VA_ARGS__);}
#define test_debug(...)     if(test && test->logf){test->logf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__);}

#define TEST_LISTEN_PORT 8998

typedef struct _Test Test;

Test* test_new(gint argc, gchar* argv[], ShadowLogFunc logf, ShadowCreateCallbackFunc callf);
void test_free(Test* test);
void test_activate(Test* test);

#endif /* SHD_TEST_H_ */
