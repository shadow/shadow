/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ADDRESS_H_
#define SHD_ADDRESS_H_

#include <glib.h>
#include <netinet/in.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/core/definitions.h"

/**
 * An Address structure holds information used to identify nodes, allowing for
 * easy extraction of both integer and string forms of an IP address as well as
 * the string hostname associated with the IP. Address is an opaque structure and
 * should only be accessed using the functions in this class.
 */
typedef struct _Address Address;

/**
 * Create a new Address structure with the given IP and Hostname. The hostname
 * is copied and stored internally, so the caller retains its ownership.
 *
 * @param ip the IP address to associate with this Address structure,
 * in network order
 * @param name the name to associate with this Address structure
 * @return a new Address structure,
 *
 * @see address_free()
 */
Address* address_new(HostId hostID, guint32 ip, const gchar* name, gboolean isLocal);

HostId address_getID(const Address* address);
void address_ref(Address* address);
void address_unref(Address* address);
gboolean address_isLocal(const Address* address);

/**
 * Retrieve the host-order integer version of this address
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return the host-order IP
 */
guint32 address_toHostIP(const Address* address);

/**
 * Retrieves the dot-and-decimal string representation of the host-order version
 * of this address. The caller does not own and should not modify or free the
 * string.
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return an address pointing to the internal memory holding the string
 */
const gchar* address_toHostIPString(const Address* address);

/**
 * Retrieve the network-order integer version of this address
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return the network-order IP
 */
guint32 address_toNetworkIP(const Address* address);

/**
 * Retrieves the hostname of this address. The caller does not own and should
 * not modify or free the string.
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return an address pointing to the internal memory holding the string
 */
const gchar* address_toHostName(const Address* address);

#endif /* SHD_ADDRESS_H_ */
