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

#ifndef VCRYPTO_INTERCEPT_H_
#define VCRYPTO_INTERCEPT_H_

#include <glib.h>
#include <openssl/aes.h>
#include <openssl/evp.h>

void intercept_AES_encrypt(const guchar *in, guchar *out, const AES_KEY *key);
void intercept_AES_decrypt(const guchar *in, guchar *out, const AES_KEY *key);
gint intercept_EVP_Cipher(EVP_CIPHER_CTX *ctx, guchar *out, const guchar *in, guint inl);

#endif /* VCRYPTO_INTERCEPT_H_ */
