/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PACKET_MINIMAL_H_
#define SHD_PACKET_MINIMAL_H_

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
// typedef needs to come after above enum definition to make our cpp code compile in
// tcp_retransmit_tally.cc.
typedef enum _PacketDeliveryStatusFlags PacketDeliveryStatusFlags;

typedef struct _PacketTCPHeader PacketTCPHeader;

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

#endif