/*-
 * Copyright (c) 2016 Eric McCorkle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stddef.h>
#include <string.h>

#include "boot_crypto.h"
#include "boot_crypto_aes.h"

static int
aes_xts_ctxinit(symmetric_alg_ctx_t *ptr, int enc __unused,
    const u_char *key, size_t keylen, u_char *iv)
{
        struct aes_xts_ctx *ctx = &ptr->aes_xts;
        u_int64_t blocknum;
	size_t xts_len = keylen << 1;
	u_int i;

        rijndael_set_key(&(ctx->key1), key, xts_len / 2);
        rijndael_set_key(&(ctx->key2), key + (xts_len / 16), xts_len / 2);
	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	bcopy(iv, &blocknum, AES_XTS_IVSIZE);
	for (i = 0; i < AES_XTS_IVSIZE; i++) {
		ctx->tweak[i] = blocknum & 0xff;
		blocknum >>= 8;
	}
	/* Last 64 bits of IV are always zero */
	bzero(ctx->tweak + AES_XTS_IVSIZE, AES_XTS_IVSIZE);
	rijndael_encrypt(&ctx->key2, ctx->tweak, ctx->tweak);

        return (0);
}

static int
aes_xts_decrypt_block(symmetric_alg_ctx_t *ptr, u_char *data)
{
        struct aes_xts_ctx *ctx = &ptr->aes_xts;
	u_int8_t block[AES_XTS_BLOCKSIZE];
	u_int i, carry_in, carry_out;

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		block[i] = data[i] ^ ctx->tweak[i];

        rijndael_decrypt(&ctx->key1, block, data);

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		data[i] ^= ctx->tweak[i];

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < AES_XTS_BLOCKSIZE; i++) {
		carry_out = ctx->tweak[i] & 0x80;
		ctx->tweak[i] = (ctx->tweak[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		ctx->tweak[0] ^= AES_XTS_ALPHA;
	bzero(block, sizeof(block));

        return (0);
}

static int
aes_xts_decrypt(symmetric_alg_ctx_t *ctx, u_char *data, size_t len)
{
        u_int i;

        for (i = 0; i < len; i += AES_XTS_BLOCKSIZE) {
                aes_xts_decrypt_block(ctx, data + i);
        }

        return (0);
}

static int
aes_xts_encrypt_block(symmetric_alg_ctx_t *ptr, u_char *data)
{
        struct aes_xts_ctx *ctx = &ptr->aes_xts;
	u_int8_t block[AES_XTS_BLOCKSIZE];
	u_int i, carry_in, carry_out;

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		block[i] = data[i] ^ ctx->tweak[i];

        rijndael_encrypt(&ctx->key1, block, data);

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		data[i] ^= ctx->tweak[i];

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < AES_XTS_BLOCKSIZE; i++) {
		carry_out = ctx->tweak[i] & 0x80;
		ctx->tweak[i] = (ctx->tweak[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		ctx->tweak[0] ^= AES_XTS_ALPHA;
	bzero(block, sizeof(block));

        return (0);
}

static int
aes_xts_encrypt(symmetric_alg_ctx_t *ctx, u_char *data, size_t len)
{
        u_int i;

        for (i = 0; i < len; i += AES_XTS_BLOCKSIZE) {
                aes_xts_encrypt_block(ctx, data + i);
        }

        return (0);
}

static int
aes_cbc_ctxinit(symmetric_alg_ctx_t *ptr, int enc, const u_char *key,
    size_t keylen, u_char *iv)
{
        struct aes_cbc_ctx *ctx = &ptr->aes_cbc;
	int err;

        err = rijndael_makeKey(&ctx->aeskey, !enc, keylen,
                               (const char *)key);
        if (err < 0) {
                return (err);
        }

        err = rijndael_cipherInit(&ctx->cipher, MODE_CBC, iv);
        if (err < 0) {
                return (err);
        }

	return (0);
}

static int
aes_cbc_decrypt(symmetric_alg_ctx_t *ptr, u_char *data, size_t len)
{
        struct aes_cbc_ctx *ctx = &ptr->aes_cbc;
        int blks;

        blks = rijndael_blockDecrypt(&ctx->cipher, &ctx->aeskey, data,
                                     len * 8, data);

        if (len != (blks / 8)) {
                return (1);
        } else {
                return (0);
        }
}

static int
aes_cbc_encrypt(symmetric_alg_ctx_t *ptr, u_char *data, size_t len)
{
        struct aes_cbc_ctx *ctx = &ptr->aes_cbc;
        int blks;

        blks = rijndael_blockEncrypt(&ctx->cipher, &ctx->aeskey, data,
                                     len * 8, data);

        if (len != (blks / 8)) {
                return (1);
        } else {
                return (0);
        }
}

const symmetric_alg_t alg_aes_xts = {
        .ctxinit = aes_xts_ctxinit,
        .decrypt = aes_xts_decrypt,
        .encrypt = aes_xts_encrypt
};

const symmetric_alg_t alg_aes_cbc = {
        .ctxinit = aes_cbc_ctxinit,
        .decrypt = aes_cbc_decrypt,
        .encrypt = aes_cbc_encrypt
};
