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

#include <string.h>
#include "hash.h"

uint32_t jenkins32int_hash(uint32_t a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

int adler32_hash(char * txt) {
	/* adler32 */
	int a=1,b=1,slen=strlen(txt),i;

	for(i=0;i<slen;i++) {
		a += txt[i];
		if(i)
			b += b;
		b += txt[i];
	}

	return (a % 65521) + (b % 65521)*65536;
}

/* data is the location of the data in physical memory and len is the length of the data in bytes */
unsigned long adler32_hash2(unsigned char *data, int len) {
    unsigned long a = 1, b = 0;
    int index;

    /* Process each byte of the data in order */
    for (index = 0; index < len; ++index)
    {
        a = (a + data[index]) % 65521;
        b = (b + a) % 65521;
    }

    return (b << 16) | a;
}

unsigned int twouint_hash(uint32_t n1, uint32_t n2) {
	size_t size = sizeof(uint32_t);

	unsigned char buffer[2*size];

	memcpy(buffer, (void*)&n1, size);
	memcpy(buffer+size, (void*)&n2, size);

	return adler32_hash2(buffer, 2*size);
}

