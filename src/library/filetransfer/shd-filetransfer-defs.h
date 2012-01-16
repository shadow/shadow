/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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
