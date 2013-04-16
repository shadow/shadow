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


#ifndef SHD_FILETRANSFER_DEFS_H_
#define SHD_FILETRANSFER_DEFS_H_

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif

#define FT_STR_SIZE 256
#define FT_BUF_SIZE 16384

#define FT_HTTP_200 "HTTP/1.1 200 OK\r\n"
#define FT_HTTP_200_LEN 17
#define FT_HTTP_404 "HTTP/1.1 404 NOT FOUND\r\n"
#define FT_HTTP_404_LEN 24

#define FT_2CRLF "\r\n\r\n"
#define FT_2CRLF_LEN 4
#define FT_CONTENT "Content-Length: "
#define FT_CONTENT_LEN 16

/* version 5, one supported auth method, no auth */
#define FT_SOCKS_INIT "\x05\x01\x00"
#define FT_SOCKS_INIT_LEN 3
/* version 5, auth choice (\xFF means none supported) */
#define FT_SOCKS_CHOICE "\x05\x01"
#define FT_SOCKS_CHOICE_LEN 2
/* v5, TCP conn, reserved, IPV4, ip_addr (4 bytes), port (2 bytes) */
#define FT_SOCKS_REQ_HEAD "\x05\x01\x00\x01"
#define FT_SOCKS_REQ_HEAD_LEN 4
/* v5, status, reserved, IPV4, ip_addr (4 bytes), port (2 bytes) */
#define FT_SOCKS_RESP_HEAD "\x05\x00\x00\x01"
#define FT_SOCKS_RESP_HEAD_LEN 4

#define FT_HTTP_GET_FMT "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n"
#define FT_HTTP_200_FMT "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n"

/* make files that use CDF happy */
#define MAGIC_VALUE
#define MAGIC_DECLARE
#define MAGIC_INIT(object)
#define MAGIC_ASSERT(object)
#define MAGIC_CLEAR(object)

#endif /* SHD_FILETRANSFER_DEFS_H_ */
