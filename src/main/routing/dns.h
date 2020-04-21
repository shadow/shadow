/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DNS_H_
#define SHD_DNS_H_

#include <glib.h>
#include <netinet/in.h>

#include "routing/address.h"

typedef struct _DNS DNS;

DNS* dns_new();
void dns_free(DNS* dns);

Address* dns_register(DNS* dns, GQuark id, gchar* name, gchar* requestedIP);
void dns_deregister(DNS* dns, Address* address);

Address* dns_resolveIPToAddress(DNS* dns, in_addr_t ip);
Address* dns_resolveNameToAddress(DNS* dns, const gchar* name);

#endif /* SHD_DNS_H_ */
