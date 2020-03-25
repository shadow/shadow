#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

// man 2 bind
int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    return syscall(SYS_bind, sockfd, addr, addrlen);
}

// man 2 recv
ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    return recvfrom(sockfd, buf, len, flags, NULL, NULL);
}

// man 2 recvfrom
ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags,
                 struct sockaddr* src_addr, socklen_t* addrlen) {
    return syscall(SYS_recvfrom, sockfd, buf, len, flags, src_addr, addrlen);
}

// man 2 recvmsg
ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags) {
    return syscall(SYS_recvmsg, sockfd, msg, flags);
}

// man 2 send
ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    return sendto(sockfd, buf, len, flags, NULL, 0);
}

// man 2 sendto
ssize_t sendto(int sockfd, const void* buf, size_t len, int flags,
               const struct sockaddr* dest_addr, socklen_t addrlen) {
    return syscall(SYS_sendto, sockfd, buf, len, flags, dest_addr, addrlen);
}

// man 2 sendmsg
ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags) {
    return syscall(SYS_sendmsg, sockfd, msg, flags);
}

// man 2 socket
int socket(int domain, int type, int protocol) {
    return syscall(SYS_socket, domain, type, protocol);
}

// man 2 connect
int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    return syscall(SYS_connect, sockfd, addr, addrlen);
}

