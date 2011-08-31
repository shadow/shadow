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

#include <glib.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "global.h"
#include "nbdf.h"

/* #define NBDF_FLUSH_STDOUT */

/* a nice little shortcut for the nbdf_construct function */
#define NBDF_CHECK_BUFFER(needed) do {\
							while( nb->consumed + (needed) > nb->avail ) { nb->avail*=2; nb->data=realloc(nb->data,nb->avail); } \
						}while(0)


nbdf_tp nbdf_construct(gchar * format, ...) {
	va_list ap;
	gchar * str;
	gchar buf[32];
	nbdf_tp nb, nb_in;
	ptime_t pt;
	guint i,j, msize=0, bin_len=0;
	nbdf_tp *nb_arr;

	nb = malloc(sizeof(*nb));

	nb->avail = NBDF_DEFAULT_AVAILABLE;
	nb->data = malloc(nb->avail);
	nb->consumed=4; /* first 4 bytes are the length of this nbdf! */

	va_start(ap, format);
	for(i=0; format[i]; i++) {
		switch(format[i]) {
			case 'i': /* 4 byte gint */
				NBDF_CHECK_BUFFER(4);
				*((guint32*)(&nb->data[nb->consumed])) = htonl(va_arg(ap, gint));
				nb->consumed += 4;
				break;

			case 'j': /* 2 byte gint */
				NBDF_CHECK_BUFFER(2);
				*((guint16*)(&nb->data[nb->consumed])) = htons((guint16)va_arg(ap, gint));
				nb->consumed += 2;
				break;

			case 'c': /* 1 byte gchar */
				NBDF_CHECK_BUFFER(1);
				nb->data[nb->consumed++] = (gchar)va_arg(ap, gint);
				break;

			case 'b': /* binary chunk */
				bin_len = va_arg(ap, guint);
				str = va_arg(ap, gchar*);

				if(str && bin_len) {
					NBDF_CHECK_BUFFER(4 + bin_len);
					*((guint32*)(&nb->data[nb->consumed])) = htonl(bin_len);
					nb->consumed += 4;

					memcpy(&nb->data[nb->consumed], str, bin_len);
					nb->consumed += bin_len;
				} else {
					NBDF_CHECK_BUFFER(4);
					*((guint32*)(&nb->data[nb->consumed])) = 0;
					nb->consumed+=4;
				}
				break;

			case 's': /* string */
				str = va_arg(ap, gchar*);

				if(str) {
					bin_len = strlen(str);
					NBDF_CHECK_BUFFER(4 + bin_len);

					*((guint32*)(&nb->data[nb->consumed])) = htonl(bin_len);
					nb->consumed += 4;

					if(bin_len > 0) {
						memcpy(&nb->data[nb->consumed], str, bin_len);
						nb->consumed += bin_len;
					}
				} else {
					NBDF_CHECK_BUFFER(4);
					*((guint32*)(&nb->data[nb->consumed])) = 0;
					nb->consumed+=4;
				}

				break;

			case 'a': /* in_addr_t */
				NBDF_CHECK_BUFFER(4);
				*((guint32*)(&nb->data[nb->consumed])) = htonl(va_arg(ap, in_addr_t));
				nb->consumed += 4;
				break;

			case 'p': /* in_port_t */
				NBDF_CHECK_BUFFER(2);
				*((guint16*)(&nb->data[nb->consumed])) = htons((in_port_t)va_arg(ap, gint));
				nb->consumed += 2;
				break;

			case 'd': /* gdouble - transmit as a ascii-string. slow, but effective. */
				snprintf(buf, sizeof(buf), "%f", va_arg(ap, gdouble)); buf[sizeof(buf)-1] = 0;
				bin_len = strlen(buf);

				NBDF_CHECK_BUFFER(4 + bin_len);
				*((guint32*)(&nb->data[nb->consumed])) = htonl(bin_len);
				nb->consumed += 4;

				memcpy(&nb->data[nb->consumed], buf, bin_len);
				nb->consumed += bin_len;
				break;

			case 'n': /* precoded nbdf */
				nb_in = va_arg(ap, nbdf_tp);
				if(nb_in) {
					NBDF_CHECK_BUFFER(nb_in->consumed);
					memcpy(&nb->data[nb->consumed], nb_in->data, nb_in->consumed);
					nb->consumed += nb_in->consumed;
				} else {
					NBDF_CHECK_BUFFER(4);
					*((guint32*)(&nb->data[nb->consumed])) = 0;
					nb->consumed+=4;
				}
				break;

			case 'm':
				msize = va_arg(ap, guint);
				nb_arr = va_arg(ap, nbdf_tp*);

				NBDF_CHECK_BUFFER(4);
				*((guint32*)(&nb->data[nb->consumed])) = htonl(msize);
				nb->consumed+=4;
				for(j=0; j<msize; j++) {
					if(nb_arr[j]) {
						NBDF_CHECK_BUFFER(nb_arr[j]->consumed);
						memcpy(&nb->data[nb->consumed], nb_arr[j]->data, nb_arr[j]->consumed);
						nb->consumed += nb_arr[j]->consumed;
					} else {
						NBDF_CHECK_BUFFER(4);
						*((guint32*)(&nb->data[nb->consumed])) = 0;
						nb->consumed+=4;
					}
				}

				break;

			case 't': { /* ptime_t */
				pt = va_arg(ap, ptime_t);
				NBDF_CHECK_BUFFER(8);

				if(BYTE_ORDER == BIG_ENDIAN) {
					// already in network byte order
				} else {
					// flip er
#ifdef __bswap_64
					pt = __bswap_64(pt);
#else
					pt = ((((pt) & 0xff00000000000000ull) >> 56)
						  | (((pt) & 0x00ff000000000000ull) >> 40)
						  | (((pt) & 0x0000ff0000000000ull) >> 24)
						  | (((pt) & 0x000000ff00000000ull) >> 8)
						  | (((pt) & 0x00000000ff000000ull) << 8)
						  | (((pt) & 0x0000000000ff0000ull) << 24)
						  | (((pt) & 0x000000000000ff00ull) << 40)
						  | (((pt) & 0x00000000000000ffull) << 56));
#endif
				}

				memcpy(&nb->data[nb->consumed], &pt, 8);
				nb->consumed += 8;

				break;
			}
		}
	}
	va_end(ap);

	*((guint32*)(nb->data)) = htonl(nb->consumed-4);

	return nb;
}

nbdf_tp nbdf_dup(nbdf_tp nb) {
	nbdf_tp rv = malloc(sizeof(*rv));
	if(!rv)
		abort();

	rv->avail = rv->consumed = nb->consumed;
	rv->data = malloc(rv->avail);
	memcpy(rv->data, nb->data, rv->avail);

	return rv;
}

nbdf_tp nbdf_import_frame_pipecloud(pipecloud_tp pipecloud) {
	guint32 framesize, h_framesize;
	nbdf_tp nb;

	if(pipecloud_read(pipecloud, (gchar*)&framesize, 4) == 0)
		return NULL;

	h_framesize = ntohl(framesize);

	if(h_framesize == 0)
		return NULL;

	nb = malloc(sizeof(*nb));
	nb->data = malloc(h_framesize + 4);
	nb->avail = nb->consumed = h_framesize + 4;

	memcpy(nb->data, &framesize, 4);
	if(pipecloud_read(pipecloud, nb->data + 4, nb->consumed - 4) == 0) 
		printfault(EXIT_UNKNOWN, "nbdf_import_frame_pipecloud: pipecloud_read() failed");

	return nb;
}

nbdf_tp nbdf_import_frame(socket_tp s) {
	guint32 framesize;
	nbdf_tp nb;

	if(!socket_peek(s, (gchar*)&framesize, 4))
		return NULL;
	framesize = ntohl(framesize);

	if(socket_data_incoming(s) < framesize + 4)
		return NULL;

	if(framesize == 0) {
		/* throw away the size... */
		socket_read(s, (gchar*)&framesize,4);

		return NULL;
	}

	nb = malloc(sizeof(*nb));
	nb->data = malloc(framesize + 4);
	nb->avail = nb->consumed = framesize + 4;

	if(!socket_read(s, nb->data, nb->consumed)) {
		free(nb->data); free(nb);
		return NULL;
	}

	/*debugf( "NBDF imported %i byte frame\n", nb->consumed);*/

	return nb;
}

gint nbdf_frame_avail(socket_tp s) {
	guint32 framesize;

	if(!socket_peek(s, (gchar*)&framesize, 4))
		return 0;

	framesize = ntohl(framesize);

	if(socket_data_incoming(s) < framesize + 4)
		return 0;

	return 1;
}

void nbdf_send_pipecloud(nbdf_tp nb, gint destination_mbox, pipecloud_tp pc) {
	if(nb && nb->data && nb->consumed) {
		pipecloud_write(pc, destination_mbox, nb->data, nb->consumed);
	} else {
		pipecloud_write(pc, destination_mbox, "\0\0\0\0", 4);
	}
}

void nbdf_send(nbdf_tp nb, socket_tp s) {
	if(nb && nb->data && nb->consumed)
		socket_write(s, nb->data, nb->consumed);
	else {
		/* write out the "null" nbdf encoding */
		socket_write(s, "\0\0\0\0", 4);
	}
}

/**
 * if using code 'b' (for a binary chunk without malloc), you MUST pass in two
 * arguments: an guint and a gchar * to a buffer for the data. the unsigned
 * gint should contain the size of the buffer you are passing in to avoid overflowing
 * your buffer. this means if you give a buffer too small, you might lose data
 * from the frame.
 */
void nbdf_read(nbdf_tp nb, gchar * format, ...) {
	va_list ap;
	guint i,j, cpos=4, dsize, msize;
	guint32 framesize;
	guint * iptr;
	unsigned short * siptr;
	gchar * cptr;
	gchar ** dptr;
	nbdf_tp ** nbdptr;
	nbdf_tp * nbptr;
	gchar buf[32];
	ptime_t * ptptr;
	in_addr_t * inaptr;
	in_port_t * inpptr;
	gdouble * dubptr;

	/* TODO: fix this so smaller user-provided buffers will not mis-align
	 * all future reads */

	if(format == NULL)
		return;

	va_start(ap, format);
	for(i=0; format[i]; i++) {
		switch(format[i]) {
			case 'i': /* 4 byte gint */
				iptr = va_arg(ap, guint*);
				*iptr = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;
				break;

			case 'j': /* 2 byte gint */
				siptr = va_arg(ap, unsigned short*);
				*siptr = ntohs(*((guint16*)&nb->data[cpos]));
				cpos+=2;
				break;

			case 'c': /* 1 byte gchar */
				cptr = va_arg(ap, gchar*);
				*cptr = nb->data[cpos++];
				break;

			case 'M':
				iptr = va_arg(ap, guint *);
				nbdptr = va_arg(ap, nbdf_tp **);

				msize = *iptr = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;

				if(msize) {
					nbptr = *nbdptr = malloc(sizeof(*nbptr) * msize);

					for(j=0; j<msize;j++) {
						framesize = ntohl(*((guint32*)&nb->data[cpos]));

						if(framesize == 0) {
							nbptr[j] = NULL;
							cpos += 4;
						} else {
							nbptr[j] = malloc(sizeof(*nb));
							nbptr[j]->avail = nbptr[j]->consumed = framesize + 4;
							nbptr[j]->data = malloc(nbptr[j]->consumed);

							memcpy(nbptr[j]->data, &nb->data[cpos], nbptr[j]->consumed);
							cpos += nbptr[j]->consumed;
						}
					}
				} else {
					*nbdptr = NULL;
				}
				break;

			case 'm':
				iptr = va_arg(ap, guint *);
				nbptr = va_arg(ap, nbdf_tp *);

				dsize = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;

				/* use the smaller of the two..*/
				msize = *iptr = (*iptr > dsize ? dsize : *iptr);

				for(j=0; j<msize;j++) {
					framesize = ntohl(*((guint32*)&nb->data[cpos]));

					if(framesize == 0) {
						nbptr[j] = NULL;
						cpos += 4;
					} else {
						nbptr[j] = malloc(sizeof(*nb));
						nbptr[j]->avail = nbptr[j]->consumed = framesize + 4;
						nbptr[j]->data = malloc(nbptr[j]->consumed);

						memcpy(nbptr[j]->data, &nb->data[cpos], nbptr[j]->consumed);
						cpos += nbptr[j]->consumed;
					}
				}
				break;

			case 'B': /* binary chunk with mallocation */
				iptr = va_arg(ap, guint *);
				dptr = va_arg(ap,gchar**);

				dsize = *iptr = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;

				if(dsize) {
					cptr = *dptr = malloc(dsize);
					memcpy(cptr, &nb->data[cpos], dsize);
					cpos += dsize;
				} else {
					*dptr = NULL;
				}

				break;

			case 'b': /* binary chunk */
				iptr = va_arg(ap, guint *);
				cptr = va_arg(ap,gchar*);

				dsize = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;

				/* use the smaller of the two..*/
				*iptr = dsize = *iptr > dsize ? dsize : *iptr;

				if(dsize) {
					memcpy(cptr, &nb->data[cpos], dsize);
					cpos += dsize;
				}
				break;

			case 'S': /* string  with mallocation*/
				dptr = va_arg(ap,gchar**);

				dsize = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;

				cptr = *dptr = malloc(dsize + 1);
				if(dsize)
					memcpy(cptr, &nb->data[cpos], dsize);
				cptr[dsize] = 0;
				cpos += dsize;

				break;

			case 's': /* string */
				msize = va_arg(ap, guint);
				cptr = va_arg(ap,gchar*);

				dsize = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;

				dsize = msize > (dsize+1) ? (dsize+1) : msize ;

				if(dsize-1)
					memcpy(cptr, &nb->data[cpos], dsize-1);
				cptr[dsize-1] = 0;

				cpos += dsize-1;
				break;

			case 'd': /* gdouble */
				dubptr = va_arg(ap,gdouble*);

				dsize = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;

				dsize = sizeof(buf) > (dsize+1) ? (dsize+1) : sizeof(buf) ;

				memcpy(buf, &nb->data[cpos], dsize-1);
				buf[dsize-1] = 0;
				*dubptr = atof(buf);

				cpos += dsize-1;
				break;

			case 'a': /* in_addr_t */
				inaptr = va_arg(ap,in_addr_t*);

				*inaptr = ntohl(*((guint32*)&nb->data[cpos]));
				cpos+=4;
				break;

			case 'p': /* in_port_t */
				inpptr = va_arg(ap, in_port_t*);
				*inpptr = ntohs(*((guint16*)&nb->data[cpos]));
				cpos+=2;
				break;

			case 'n': /* precoded nbdf */
				nbptr = va_arg(ap, nbdf_tp*);
				framesize = ntohl(*((guint32*)&nb->data[cpos]));

				if(framesize == 0) {
					*nbptr = NULL;
					cpos += 4;
				} else {
					*nbptr = malloc(sizeof(**nbptr));
					(*nbptr)->avail = (*nbptr)->consumed = framesize + 4;
					(*nbptr)->data = malloc((*nbptr)->consumed);

					memcpy((*nbptr)->data, &nb->data[cpos], (*nbptr)->consumed);
					cpos += (*nbptr)->consumed;
				}
				break;

			case 't': /* ptime_t */
				ptptr = va_arg(ap, ptime_t *);

				memcpy(ptptr, &nb->data[cpos], 8);
				cpos += 8;

				if(BYTE_ORDER != BIG_ENDIAN) {
					// flip er
#ifdef __bswap_64
					*ptptr = __bswap_64(*ptptr);
#else
					*ptptr = ((((*ptptr) & 0xff00000000000000ull) >> 56)
						  | (((*ptptr) & 0x00ff000000000000ull) >> 40)
						  | (((*ptptr) & 0x0000ff0000000000ull) >> 24)
						  | (((*ptptr) & 0x000000ff00000000ull) >> 8)
						  | (((*ptptr) & 0x00000000ff000000ull) << 8)
						  | (((*ptptr) & 0x0000000000ff0000ull) << 24)
						  | (((*ptptr) & 0x000000000000ff00ull) << 40)
						  | (((*ptptr) & 0x00000000000000ffull) << 56));
#endif
				}

				break;
		}
	}
	va_end(ap);
}

void nbdf_free(nbdf_tp nbdf) {
	if(nbdf) {
		if(nbdf->data)
			free(nbdf->data);
		free(nbdf);
	}
}


