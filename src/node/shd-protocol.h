/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#ifndef SHD_PROTOCOL_H_
#define SHD_PROTOCOL_H_

enum ProtocolType {
	PNONE, PLOCAL, PTCP, PUDP
};

enum ProtocolLocalFlags {
	PLOCAL_NONE = 0,
};

enum ProtocolUDPFlags {
	PUDP_NONE = 0,
};

enum ProtocolTCPFlags {
	PTCP_NONE = 0,
	PTCP_RST = 1 << 1,
	PTCP_SYN = 1 << 2,
	PTCP_ACK = 1 << 3,
	PTCP_FIN = 1 << 4,
};

/**
 * Use protocol type and 16 bit port to create a unique gint that can be used
 * as a key into a hashtable. The protocol type occupies the upper 16 bits, and
 * the port occupies the lower 16 bits.
 */
#define PROTOCOL_DEMUX_KEY(protocolType, port) ((gint)((protocolType << 16) + port))

#endif /* SHD_PROTOCOL_H_ */
