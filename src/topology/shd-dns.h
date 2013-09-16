/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_DNS_H_
#define SHD_DNS_H_

#include "shadow.h"

typedef struct _DNS DNS;

DNS* dns_new();
void dns_free(DNS* dns);

Address* dns_register(DNS* dns, GQuark id, gchar* name, gchar* requestedIP);
void dns_deregister(DNS* dns, Address* address);

Address* dns_resolveIPToAddress(DNS* dns, guint32 ip);
Address* dns_resolveNameToAddress(DNS* dns, gchar* name);
guint32 dns_resolveNameToIP(DNS* dns, gchar* name);
const gchar* dns_resolveIPToName(DNS* dns, guint32 ip);

#endif /* SHD_DNS_H_ */
