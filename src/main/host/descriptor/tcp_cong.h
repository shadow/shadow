#ifndef SHD_TCP_CONG_H_
#define SHD_TCP_CONG_H_

#include <stdbool.h>

#include "main/host/descriptor/tcp.h"

// congestion event hooks

typedef void (*TCPCongDelete)(TCP *tcp);
typedef void (*TCPCongDuplicateAckEv)(TCP *tcp);
typedef bool (*TCPCongFastRecovery)(TCP *tcp);
typedef void (*TCPCongNewAckEv)(TCP *tcp, guint32 n);
typedef void (*TCPCongTimeoutEv)(TCP *tcp);
typedef guint32 (*TCPCongSSThresh)(TCP *tcp);
typedef const char* (*TCPCongNameStr)();

typedef struct TCPCongHooks_ {
    TCPCongDelete tcp_cong_delete;
    TCPCongDuplicateAckEv tcp_cong_duplicate_ack_ev;
    TCPCongFastRecovery tcp_cong_fast_recovery;
    TCPCongNewAckEv tcp_cong_new_ack_ev;
    TCPCongTimeoutEv tcp_cong_timeout_ev;
    TCPCongSSThresh tcp_cong_ssthresh;
    TCPCongNameStr tcp_cong_name_str;
} TCPCongHooks;

typedef struct TCPCong_ {
    guint32 cwnd;
    const TCPCongHooks *hooks;
    void *ca;
} TCPCong;

const char* tcpcong_nameStr(const TCPCong *cong);

#endif // SHD_TCP_CONG_H_
