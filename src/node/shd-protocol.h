/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
