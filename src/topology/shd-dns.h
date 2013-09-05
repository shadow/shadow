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

#endif /* SHD_DNS_H_ */
