/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
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
