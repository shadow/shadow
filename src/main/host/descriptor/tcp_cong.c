#include "main/host/descriptor/tcp_cong.h"

const char* tcpcong_nameStr(const TCPCong *cong) {
    return cong->hooks->tcp_cong_name_str();
}
