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

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <string.h>

#include "vcrypto_intercept.h"
#include "vsystem.h"

void intercept_AES_encrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key) {
	/* Processing delays already include crypto and are added during reads/writes */
	/* TODO: do we need to advance the key here? */
	return;
}

void intercept_AES_decrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key) {
	/* Processing delays already include crypto and are added during reads/writes */
	/* TODO: do we need to advance the key here? */
	return;
}

int intercept_EVP_Cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, unsigned int inl) {
	/* Processing delays already include crypto and are added during reads/writes */
	/* TODO: do we need to advance the key here? */
	memmove(out, in, (size_t)inl);
	return 1;
}
