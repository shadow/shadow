#ifndef SHD_LEGACY_PACKET_H_
#define SHD_LEGACY_PACKET_H_

#include <netinet/in.h>

// Enum typedefs below need to come after enum definitions to make our cpp code compile in
// tcp_retransmit_tally.cc.

enum _ProtocolType { PNONE, PTCP, PUDP, PMOCK };
typedef enum _ProtocolType ProtocolType;

enum _ProtocolUDPFlags {
    PUDP_NONE = 0,
};
typedef enum _ProtocolUDPFlags ProtocolUDPFlags;

enum _ProtocolTCPFlags {
    PTCP_NONE = 0,
    PTCP_RST = 1 << 1,
    PTCP_SYN = 1 << 2,
    PTCP_ACK = 1 << 3,
    PTCP_SACK = 1 << 4,
    PTCP_FIN = 1 << 5,
    PTCP_DUPACK = 1 << 6,
};
typedef enum _ProtocolTCPFlags ProtocolTCPFlags;

enum _PacketDeliveryStatusFlags {
    PDS_NONE = 0,
    PDS_SND_CREATED = 1 << 1,
    PDS_SND_TCP_ENQUEUE_THROTTLED = 1 << 2,
    PDS_SND_TCP_ENQUEUE_RETRANSMIT = 1 << 3,
    PDS_SND_TCP_DEQUEUE_RETRANSMIT = 1 << 4,
    PDS_SND_TCP_RETRANSMITTED = 1 << 5,
    PDS_SND_SOCKET_BUFFERED = 1 << 6,
    PDS_SND_INTERFACE_SENT = 1 << 7,
    PDS_INET_SENT = 1 << 8,
    PDS_INET_DROPPED = 1 << 9,
    PDS_ROUTER_ENQUEUED = 1 << 10,
    PDS_ROUTER_DEQUEUED = 1 << 11,
    PDS_ROUTER_DROPPED = 1 << 12,
    PDS_RCV_INTERFACE_RECEIVED = 1 << 13,
    PDS_RCV_INTERFACE_DROPPED = 1 << 14,
    PDS_RCV_SOCKET_PROCESSED = 1 << 15,
    PDS_RCV_SOCKET_DROPPED = 1 << 16,
    PDS_RCV_TCP_ENQUEUE_UNORDERED = 1 << 17,
    PDS_RCV_SOCKET_BUFFERED = 1 << 18,
    PDS_RCV_SOCKET_DELIVERED = 1 << 19,
    PDS_DESTROYED = 1 << 20,
    PDS_RELAY_CACHED = 1 << 21,
    PDS_RELAY_FORWARDED = 1 << 22,
};
typedef enum _PacketDeliveryStatusFlags PacketDeliveryStatusFlags;

// At most 32 bytes are available in the TCP header for selective acks. They represent ranges of
// sequence numbers that have been acked, so each is a 4-byte uint. We can include a maximum of 4
// ranges in total, where each range is `[start, end)` (start is inclusive, end is exclusive).
typedef struct _PacketSelectiveAckRange PacketSelectiveAckRange;
struct _PacketSelectiveAckRange {
    // The start (left) part of the range is inclusive.
    unsigned int start;
    // The end (right) part of the range is exclusive.
    unsigned int end;
};

typedef struct _PacketSelectiveAcks PacketSelectiveAcks;
struct _PacketSelectiveAcks {
    // The number of meaningful ranges in the ranges array. Should be <= 4.
    unsigned int len;
    // The selective ack ranges.
    PacketSelectiveAckRange ranges[4];
};

// The c bindings break the cpp build but are not needed in the cpp code.
#ifndef __cplusplus
// Just for CSimulationTime.
#include "main/bindings/c/bindings-opaque.h"
typedef struct _PacketTCPHeader PacketTCPHeader;
struct _PacketTCPHeader {
    ProtocolTCPFlags flags;

    // address is in network byte order
    in_addr_t sourceIP;
    // port is in network byte order
    in_port_t sourcePort;

    // address is in network byte order
    in_addr_t destinationIP;
    // port is in network byte order
    in_port_t destinationPort;

    unsigned int sequence;
    unsigned int acknowledgment;
    PacketSelectiveAcks selectiveACKs;
    unsigned int window;
    unsigned char windowScale;
    bool windowScaleSet;
    CSimulationTime timestampValue;
    CSimulationTime timestampEcho;
};
#endif // __cplusplus

#endif // SHD_LEGACY_PACKET_H_
