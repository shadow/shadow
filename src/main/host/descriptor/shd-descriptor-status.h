/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_STATUS_H_
#define SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_STATUS_H_

typedef enum _DescriptorStatus DescriptorStatus;
enum _DescriptorStatus {
    DS_NONE = 0,
    /* ok to notify user as far as we know, socket is ready.
     * o/w never notify user (b/c they e.g. closed the socket or did not accept
     * yet) */
    DS_ACTIVE = 1 << 0,
    /* can be read, i.e. there is data waiting for user */
    DS_READABLE = 1 << 1,
    /* can be written, i.e. there is available buffer space */
    DS_WRITABLE = 1 << 2,
    /* user already called close */
    DS_CLOSED = 1 << 3,
};

#endif /* SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_STATUS_H_ */
