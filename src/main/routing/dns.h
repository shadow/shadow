/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DNS_H_
#define SHD_DNS_H_

#include <glib.h>
#include <netinet/in.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/routing/address.h"

typedef struct _DNS DNS;

DNS* dns_new();
void dns_free(DNS* dns);

/* Address must be in network byte order. */
void dns_register(DNS* dns, HostId id, const gchar* name, in_addr_t requestedIP);
void dns_deregister(DNS* dns, in_addr_t ip);

/* Address must be in network byte order. */
Address* dns_resolveIPToAddress(DNS* dns, in_addr_t ip);
Address* dns_resolveNameToAddress(DNS* dns, const gchar* name);

/* Returns a string path to a file containing (ip,name) information for all
 * currently registered pairs. The format of the file follows the format
 * used in /etc/hosts (see `man 5 hosts`). The returned path is a new string
 * that is owned and should be freed by the caller.
 *
 * The file is created lazily when this function is called and becomes invalid
 * if dns_register() is called after this function returns; once it becomes
 * invalid, a new file is created upon a subsequent call to this function. */
gchar* dns_getHostsFilePath(DNS* dns);

#endif /* SHD_DNS_H_ */
