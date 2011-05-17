/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include "global.h"
#include "socket.h"

static int socket_signal_status = 0;
static int socket_is_async = 0;

#if defined (HAVE_SIGNAL_H) && defined (USE_SIGNALS)
#include <signal.h>
#endif

static int socket_configure(int fd, int socket_options) {
	int flags;
	int pgrp = getpid();
	int use_async =  socket_is_async && (socket_options & SOCKET_OPTION_NONBLOCK);

	flags = (socket_options & SOCKET_OPTION_NONBLOCK ? O_NONBLOCK : 0);

	if(use_async) {
#if defined (O_ASYNC)
		flags |= O_ASYNC;
#elif defined (FASYNC)
		flags |= FASYNC;
#else
#warning "Unable to set sockets in async mode"
		use_async = 0;
#endif
	}

	if((socket_options & SOCKET_OPTION_TCP) && (socket_options & SOCKET_OPTION_UDP))
		return 0;

#if defined (F_SETOWN)
	if(use_async && fcntl(fd, F_SETOWN, pgrp) < 0) {
		perror("fcntl(F_SETOWN)");
		close(fd);
		return 0;
	}
//#elif defined (FIOSETOWN)
#else
	if(use_async && ioctl(fd, FIOSETOWN, (char*)&pgrp) < 0) {
		perror("ioctl(FIOSETOWN)");
		close(fd);
		return 0;
	}
#endif

	if(flags && fcntl(fd, F_SETFL, flags) < 0) {
		perror("fcntl");
		close(fd);
		return 0;
	}

/*	if(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sock_buf_size, sizeof(sock_buf_size)) != 0) {
		close(fd);
		return 0;
	} */

	return 1;
}

//#ifdef DEBUG

/** @returns true if the given socket is valid and connected */
int socket_isvalid(socket_tp s) { return s!=NULL && s->sock != -1; }

/** @returns true if the given socket has waiting outgoing data */
size_t socket_data_outgoing(socket_tp s) { return s->total_outgoing_size; }

/** @returns true if the given socket has waiting incoming data */
size_t socket_data_incoming(socket_tp s) { return s->total_incoming_size; } 

/* returns the actual FD associated with this socket */
int socket_getfd(socket_tp s) { return s->sock; }

char * socket_gethost(socket_tp s) { return inet_ntoa(s->remoteaddr.sin_addr); }

short socket_getport(socket_tp s) { return ntohs(s->remoteaddr.sin_port); }

int socket_islisten(socket_tp s) { return s->state & SOCKET_STATE_LISTEN; }

/* sets the internal blocksize of this socket. defaults to 16k */
void socket_set_blocksize(socket_tp s,size_t bsize) { s->block_size = bsize; }

//#endif


static void socket_init_struct(socket_tp s,int fd, int state, int socket_options) {
	s->sock = fd;
	s->options = socket_options;
	s->total_incoming_size = s->total_outgoing_size = 0;
	s->block_size = SOCKET_DEFAULT_BLOCKSIZE;
	s->incoming_buffer = s->incoming_buffer_end = s->outgoing_buffer = s->outgoing_buffer_end = 0;
	s->state = state;
	s->incoming_buffer_d = s->outgoing_buffer_d = NULL;
	s->incoming_buffer_d_allocated = s->incoming_buffer_d_size = 0;
	s->outgoing_buffer_d_allocated = s->outgoing_buffer_d_size = 0;
	memset(&s->remoteaddr, 0, sizeof(s->remoteaddr));
}

socket_tp socket_create_from(int fd, int socket_options) {
	socket_tp s;

	if(socket_options & SOCKET_OPTION_TCP) {
		if(!socket_configure(fd, socket_options))
			return NULL;

		s = malloc(sizeof(*s));
		socket_init_struct(s, fd, SOCKET_STATE_CONNECTED, socket_options);
	} else
		return NULL;

	return s;
}

socket_tp socket_create (int socket_options) {
	int fd;
	socket_tp s;

	if(socket_options & SOCKET_OPTION_TCP) {
		fd = socket(PF_INET, SOCK_STREAM | DVN_CORE_SOCKET, 0);

		if(fd < 0)
			return NULL;

		if(!socket_configure(fd, socket_options))
			return NULL;
	} else if(socket_options & SOCKET_OPTION_UDP){
		fd = socket(PF_INET, SOCK_DGRAM | DVN_CORE_SOCKET, 0);

		if(fd < 0)
			return NULL;

		if(!socket_configure(fd, socket_options))
			return NULL;
	} else
		return NULL;

	s = malloc(sizeof(*s));
	socket_init_struct(s, fd, SOCKET_STATE_IDLE, socket_options);

	return s;
}


void socket_ignore_sigpipe(void) {
#if defined (HAVE_SIGNAL_H) && defined (USE_SIGNALS)
	struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);
#endif
}

void socket_enable_async(void) {
#if defined (HAVE_SIGNAL_H) && defined (USE_SIGNALS)

	struct sigaction sa;
	sa.sa_handler = socket_sigio_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGIO, &sa, NULL);

	socket_is_async = 1;
#endif
}

void socket_disable_async(void) {
#if defined (HAVE_SIGNAL_H) && defined (USE_SIGNALS)
	struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGIO, &sa, NULL);

	socket_is_async = 0;
#endif
}

void socket_sigio_handler(int a) {
	socket_disable_async();
	socket_is_async = 1;
	socket_signal_status++;
}

int socket_needs_servicing(void) {
	return socket_signal_status;
}

void socket_reset_servicing_status(void) {
	socket_signal_status = 0;
	socket_enable_async();
}

socket_tp socket_create_child ( socket_tp mommy, int socket_options ) {
	socket_tp child;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in address;

	if(mommy->state != SOCKET_STATE_LISTEN)
		return NULL;

	int fd = accept(mommy->sock, (struct sockaddr*)&address, &addrlen);

	if(fd < 0)
		return NULL;

	if(!socket_configure(fd, socket_options)) {
		close(fd);
		return NULL;
	}

	child = malloc(sizeof(*child));
	socket_init_struct(child, fd, SOCKET_STATE_CONNECTED, socket_options);

	return child;
}


int socket_listen (socket_tp s, int port, int waiting_size) {
	struct sockaddr_in ctl_address;
	int o_true = 1;

	if(s->state != SOCKET_STATE_IDLE || !(s->options & SOCKET_OPTION_TCP))
		return 0;

	if(setsockopt(s->sock, SOL_SOCKET, SO_REUSEADDR,(char *)&o_true, sizeof(o_true)) < 0)
		return 0;

	ctl_address.sin_family = AF_INET;
	ctl_address.sin_addr.s_addr = INADDR_ANY; /* meh */
	ctl_address.sin_port = htons(port);
	memset(&(ctl_address.sin_zero), 0, 8);

	if (bind(s->sock, (struct sockaddr *)&ctl_address, sizeof(ctl_address))<0)
		return 0;

	if (listen(s->sock, waiting_size) < 0)
		return 0;

	s->remoteaddr = ctl_address;
	s->state = SOCKET_STATE_LISTEN;

	return 1;
}

int socket_connect (socket_tp s, char * dest_addr, int port) {
	struct hostent * he = gethostbyname(dest_addr);
	int rv = 1;
	int flags = 0;
	int sas = socket_is_async;

	if(!he || !he->h_addr)
		return 0;

	if(s->state != SOCKET_STATE_IDLE || !(s->options & SOCKET_OPTION_TCP))
		return 0;

	s->remoteaddr.sin_family = AF_INET;
	s->remoteaddr.sin_port = htons(port);
	s->remoteaddr.sin_addr.s_addr = *((in_addr_t*)he->h_addr);
	memset(&(s->remoteaddr.sin_zero), 0, 8);

	if(sas)
		socket_disable_async();

	if(s->options & SOCKET_OPTION_NONBLOCK) {
		flags = fcntl(s->sock, F_GETFL);
		flags = flags ^ O_NONBLOCK;
		fcntl(s->sock, F_SETFL, flags);
	}

	if(connect(s->sock, (struct sockaddr *)&s->remoteaddr, sizeof(s->remoteaddr)) < 0 &&
			errno != EINPROGRESS)
		rv = 0;

	if(s->options & SOCKET_OPTION_NONBLOCK) {
		flags = flags | O_NONBLOCK;
		fcntl(s->sock, F_SETFL, flags);
	}

	if(sas)
		socket_enable_async();

	return rv;
}

void socket_set_nonblock(socket_tp s) {
	if(!s)
		return;
	fcntl(s->sock, F_SETFL, O_NONBLOCK);
	s->options |= SOCKET_OPTION_NONBLOCK;
}

int socket_issue_read (socket_tp s) {
	struct SOCKET_BUFFERLINK * sbl;
	int to_read;
	char keep_reading = 1;
	int vread;

	if(s->state != SOCKET_STATE_CONNECTED)
		return 0;

	do {
		vread = -1; sbl = NULL;
		if(s->incoming_buffer != NULL) {
			to_read = SOCKET_DEFAULT_BLOCKSIZE - s->incoming_buffer_end->offset;

			if(to_read != 0)
				sbl = s->incoming_buffer_end;
			else
				to_read = SOCKET_DEFAULT_BLOCKSIZE;

		} else
			to_read = SOCKET_DEFAULT_BLOCKSIZE;

		if(sbl == NULL) {
			sbl = malloc(sizeof(*sbl));
			sbl->next = NULL;
			sbl->offset = sbl->sock_offset = 0;

			if(s->incoming_buffer_end != NULL)
				s->incoming_buffer_end->next = sbl;
			s->incoming_buffer_end = sbl;

			if(s->incoming_buffer == NULL)
				s->incoming_buffer = sbl;
		}

		vread = read(s->sock, sbl->buffer + sbl->offset, to_read);

		if(vread == 0) {
			return 0;

		} else if(vread < 0) {
			switch(errno) {
				case EAGAIN:
				case EINTR:
					break;
				default:
					return 0;
			}
			keep_reading = 0;

		} else {
			s->total_incoming_size += vread;
			sbl->offset += vread;

			if(vread != to_read)
				keep_reading = 0;
		}
	}while(keep_reading);

	return 1;
}

int socket_issue_write (socket_tp s) {
	int write_qty, written ;
	struct SOCKET_BUFFERLINK * sbl;

	if(s->state != SOCKET_STATE_CONNECTED)
		return 0;

	while(s->outgoing_buffer != NULL) {
		sbl = s->outgoing_buffer;
		write_qty = sbl->offset - sbl->sock_offset;

		written = write(s->sock, &sbl->buffer[sbl->sock_offset], write_qty);

		if(write_qty == written) {
			/* keep tryin! */
			s->outgoing_buffer = s->outgoing_buffer->next;
			free(sbl);
			s->total_outgoing_size -= write_qty;
		} else if(written == -1) {
			switch(errno) {
				case EAGAIN:
				case EINTR:
					break;
				default:
					return 0;
			}
			break;
		} else if(written != 0) {
			/* wrote some quantity of the current SBL */
			sbl->sock_offset += written;
			s->total_outgoing_size -= written;
			break;
		} else
			break;
	}

	if(s->outgoing_buffer == NULL)
		s->outgoing_buffer_end = NULL;

	return 1;
}

int socket_write_to (socket_tp s, char * dest_addr, int dest_port, char * buffer, unsigned int size) {
	struct sockaddr_in remoteaddr;
	struct hostent * he = gethostbyname(dest_addr);
	int written = -1;

	if(s->state != SOCKET_STATE_IDLE || !(s->options & SOCKET_OPTION_UDP) || !buffer || !size)
		return 0;

	remoteaddr.sin_family = AF_INET;
	remoteaddr.sin_port = htons(dest_port);
	remoteaddr.sin_addr.s_addr = *((in_addr_t*)he->h_addr);
	memset(&(s->remoteaddr.sin_zero), 0, 8);

	if(s->total_outgoing_size == 0) {
		written = sendto(s->sock, buffer, size, 0, (struct sockaddr*)&remoteaddr, sizeof(remoteaddr));
		switch(errno) {
			case EAGAIN:
				break;
			default:
				return 0;
		}
	}

	if(written < 0) {
		/* buffer the packet */
		if(!s->outgoing_buffer_d) {
			s->outgoing_buffer_d_allocated = 8;
			s->outgoing_buffer_d = malloc(sizeof(*s->outgoing_buffer_d)*s->outgoing_buffer_d_allocated);
		} else if(s->outgoing_buffer_d_allocated == s->outgoing_buffer_d_size) {
			s->outgoing_buffer_d_allocated*=2;
			s->outgoing_buffer_d = realloc(s->outgoing_buffer_d,sizeof(*s->outgoing_buffer_d)*s->outgoing_buffer_d_allocated);
		}

		s->outgoing_buffer_d[s->outgoing_buffer_d_size].data = malloc(size);
		memcpy(s->outgoing_buffer_d[s->outgoing_buffer_d_size].data, buffer, size);
		s->outgoing_buffer_d[s->outgoing_buffer_d_size].size = size;
		s->outgoing_buffer_d[s->outgoing_buffer_d_size++].remoteaddr = remoteaddr;

		s->total_outgoing_size += size;
	}

	return 1;
}

int socket_write (socket_tp s, char * buffer, unsigned int size) {
	int written =0;
	int left_to_copy;
	unsigned int avail_space;
	struct SOCKET_BUFFERLINK * sbl = s->outgoing_buffer_end;

	if(s->state != SOCKET_STATE_CONNECTED)
		return 0;

	if(s->outgoing_buffer == NULL) {
		/* attempt to write directly ... */
		written = write(s->sock, buffer, size);

		/*debugf( "Socket: Writing %i bytes total - ", size);*/

		if(written == -1) {
			switch(errno) {
				case EAGAIN:
				case EINTR:
					left_to_copy = size;
					written = 0;
					break;
				default:
					/*debugf( " failed.\n");*/
					return 0;
			}
		} else if(written != size) /* some of the buffer was written, but not all */
			left_to_copy = size - written;

		else {/* entire buffer was written */
			return 1;
		}

		sbl = s->outgoing_buffer = s->outgoing_buffer_end = malloc(sizeof(*sbl));
		sbl->offset = sbl->sock_offset = 0;
		sbl->next = NULL;
	} else
		left_to_copy = size;

	s->total_outgoing_size += left_to_copy;
	/*debugf( "%i bytes buffered in userspace. (Socket total: %i)\n", left_to_copy, s->total_outgoing_size);*/

	while(left_to_copy) {
		avail_space = SOCKET_DEFAULT_BLOCKSIZE - sbl->offset;

		if(avail_space >= left_to_copy) {
			memcpy(&sbl->buffer[sbl->offset], &buffer[written], left_to_copy);
			sbl->offset += left_to_copy;
			break;

		} else if( avail_space ) {
			memcpy(&sbl->buffer[sbl->offset], &buffer[written], avail_space);
			sbl->offset = SOCKET_DEFAULT_BLOCKSIZE;
			written += avail_space;
			left_to_copy -= avail_space;
		}

		sbl->next = malloc(sizeof(*sbl));
		sbl = s->outgoing_buffer_end = sbl->next;
		sbl->offset = sbl->sock_offset = 0;
		sbl->next = NULL;
	}

	return 1;
}

int socket_read (socket_tp s, char * buffer, unsigned int size) {
	struct SOCKET_BUFFERLINK * sbl = s->incoming_buffer;
	int pull_amt;

	if(size == 0)
		return 1;

	if(size > s->total_incoming_size)
		return 0;

	s->total_incoming_size -= size;
	while( size ){
		pull_amt = sbl->offset - sbl->sock_offset;

		if( pull_amt > size ) { /* the data remaining can all be pulled from this bufferlink */
			pull_amt = size;

			memcpy(buffer, sbl->buffer + sbl->sock_offset, pull_amt);
			sbl->sock_offset += pull_amt;
			break;

		} else { /* data remaining takes more than one bufferlink, or this one fully */
			memcpy(buffer, sbl->buffer + sbl->sock_offset, pull_amt);
			size -= pull_amt;
			buffer += pull_amt;

			s->incoming_buffer = s->incoming_buffer->next;
			free(sbl);
			sbl = s->incoming_buffer;
		}
	}

	if(s->incoming_buffer == NULL)
		s->incoming_buffer_end =NULL;

	return 1;
}

static int socket_peek_tcp(socket_tp s, char * buffer, unsigned int size){
	struct SOCKET_BUFFERLINK * sbl = s->incoming_buffer;
	int pull_amt;

	if(s->total_incoming_size < size)
		return 0;

	while( size ){
		pull_amt = sbl->offset - sbl->sock_offset;

		if( pull_amt > size ) { /* the data remaining can all be pulled from this bufferlink */
			pull_amt = size;

			memcpy(buffer, sbl->buffer + sbl->sock_offset, pull_amt);
			break;

		} else { /* data remaining takes more than one bufferlink, or this one fully */
			memcpy(buffer, sbl->buffer + sbl->sock_offset, pull_amt);
			size -= pull_amt;
			buffer += pull_amt;

			sbl = sbl->next;
		}
	}

	return size;
}

static int socket_peek_udp(socket_tp s, char * buffer, unsigned int size) {
	return 0;
}

int socket_peek (socket_tp s, char * buffer, unsigned int size) {
	if(size == 0)
		return 1;

	if(!s || !buffer || size > s->total_incoming_size)
		return 0;

	if(s->options & SOCKET_OPTION_TCP)
		return socket_peek_tcp(s, buffer, size);
	else
		return socket_peek_udp(s, buffer, size);
}

void socket_close (socket_tp s) {
	if(s && s->sock != -1) {
		close(s->sock);
		s->state = SOCKET_STATE_DEAD;
		s->sock = -1;
	}
}

void socket_destroy (socket_tp s) {
	if(s) {
		if(s->sock != -1)
			close(s->sock);

		while(s->incoming_buffer) {
			s->incoming_buffer_end = s->incoming_buffer->next;
			free(s->incoming_buffer);
			s->incoming_buffer = s->incoming_buffer_end;
		}

		while(s->outgoing_buffer) {
			s->outgoing_buffer_end = s->outgoing_buffer->next;
			free(s->outgoing_buffer);
			s->outgoing_buffer = s->outgoing_buffer_end;
		}

		if(s->incoming_buffer_d) {
			for(int i=0; i<s->incoming_buffer_d_size; i++) {
				if(s->incoming_buffer_d[i].data)
					free(s->incoming_buffer_d[i].data);
			}

			free(s->incoming_buffer_d);
		}

		if(s->outgoing_buffer_d) {
			for(int i=0; i<s->outgoing_buffer_d_size; i++) {
				if(s->outgoing_buffer_d[i].data)
					free(s->outgoing_buffer_d[i].data);
			}

			free(s->outgoing_buffer_d);
		}

		free(s);
	}
}






