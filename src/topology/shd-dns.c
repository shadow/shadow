/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _DNS {

	MAGIC_DECLARE;
};

DNS* dns_new() {
	DNS* dns = g_new0(DNS, 1);
	MAGIC_INIT(dns);

	return dns;
}

void dns_free(DNS* dns) {
	MAGIC_ASSERT(dns);

	MAGIC_CLEAR(dns);
	g_free(dns);
}
