/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_LIB_SHIM_SHIM_API_H_
#define SRC_LIB_SHIM_SHIM_API_H_

#include <ifaddrs.h>
#include <netdb.h>
#include <stdarg.h>

/// This module defines functions that can be called by external (preloaded)
/// libraries that are linked to the shim. Those libraries should only call
/// functions defined here after including this header file.

// The entry point for handling an intercepted syscall. This function remaps the
// return value into errno upon error so that errno will be set correctly upon
// returning control to the managed process. Be careful not to do something that
// would overwrite errno after this function returns.
long shim_api_syscall(long n, ...);

// Same as `shim_api_syscall()`, but allows a variable arguments list.
long shim_api_syscallv(long n, va_list args);

// Shim implementation of `man 3 getaddrinfo`.
int shim_api_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                         struct addrinfo** res);

// Shim implementation of `man 3 freeaddrinfo`.
void shim_api_freeaddrinfo(struct addrinfo* res);

// Shim implementation of `man 3 getifaddrs`.
int shim_api_getifaddrs(struct ifaddrs** ifap);

// Shim implementation of `man 3 freeifaddrs`.
void shim_api_freeifaddrs(struct ifaddrs* ifa);

#endif // SRC_LIB_SHIM_SHIM_API_H_