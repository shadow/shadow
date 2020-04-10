/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ADDRESS_H_
#define SHD_ADDRESS_H_

#include <glib.h>
#include <netinet/in.h>

#include "core/support/shd-definitions.h"

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
 * @param mac the mac address to associate with this Address structure
 * @param ip the IP address to associate with this Address structure,
 * in network order
 * @param name the name to associate with this Address structure
 * @return a new Address structure,
 *
 * @see address_free()
 */
Address* address_new(GQuark hostID, guint mac, guint32 ip, const gchar* name, gboolean isLocal);

ShadowID address_getID(Address* address);
void address_ref(Address* address);
void address_unref(Address* address);
gboolean address_isLocal(Address* address);

/**
 * Checks if the given addresses are equal. This function is NULL safe, so
 * so either or both addresses may be NULL.
 * @param a a valid, non-NULL Address structure previously created
 * with address_new()
 * @param b a valid, non-NULL Address structure previously created
 * with address_new()
 * @return TRUE if both addresses are NULL or if the IP associated with both
 * addresses are equal, FALSE otherwise
 */
gboolean address_isEqual(Address* a, Address* b);

/**
 * Retrieve the host-order integer version of this address
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return the host-order IP
 */
guint32 address_toHostIP(Address* address);

/**
 * Retrieves the dot-and-decimal string representation of the host-order version
 * of this address. The caller does not own and should not modify or free the
 * string.
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return an address pointing to the internal memory holding the string
 */
gchar* address_toHostIPString(Address* address);

/**
 * Retrieve the network-order integer version of this address
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return the network-order IP
 */
guint32 address_toNetworkIP(Address* address);

/**
 * Retrieves the hostname of this address. The caller does not own and should
 * not modify or free the string.
 * @param address a valid, non-NULL Address structure previously created
 * with address_new()
 * @return an address pointing to the internal memory holding the string
 */
gchar* address_toHostName(Address* address);

/**
 * Turns the IPv4 address into a newly allocated string that should be freed by the caller.
 */
gchar* address_ipToNewString(in_addr_t ip);

in_addr_t address_stringToIP(const gchar* ipString);

gchar* address_toString(Address* address);

#endif /* SHD_ADDRESS_H_ */
