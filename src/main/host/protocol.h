/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROTOCOL_H_
#define SHD_PROTOCOL_H_

typedef enum _ProtocolType ProtocolType;
enum _ProtocolType {
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
    PTCP_RST =  1 << 1,
    PTCP_SYN =  1 << 2,
    PTCP_ACK =  1 << 3,
    PTCP_SACK = 1 << 4,
    PTCP_FIN =  1 << 5,
    PTCP_DUPACK =  1 << 6,
};

#endif /* SHD_PROTOCOL_H_ */
