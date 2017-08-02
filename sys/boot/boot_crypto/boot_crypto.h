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

#ifndef _BOOT_CRYPTO_H_
#define _BOOT_CRYPTO_H_

#include "boot_crypto_types.h"
#include "boot_crypto_aes.h"

/* We want all the codes from cryptodev, but not the defs.  Maybe
 * these should be moved out to a separate file to allow them to be
 * included separately?
 */

/* Hash values */
#define	NULL_HASH_LEN		16
#define	MD5_HASH_LEN		16
#define	SHA1_HASH_LEN		20
#define	RIPEMD160_HASH_LEN	20
#define	SHA2_256_HASH_LEN	32
#define	SHA2_384_HASH_LEN	48
#define	SHA2_512_HASH_LEN	64
#define	MD5_KPDK_HASH_LEN	16
#define	SHA1_KPDK_HASH_LEN	20
#define	AES_GMAC_HASH_LEN	16
/* Maximum hash algorithm result length */
#define	HASH_MAX_LEN		SHA2_512_HASH_LEN /* Keep this updated */

/* HMAC values */
#define	NULL_HMAC_BLOCK_LEN		64
#define	MD5_HMAC_BLOCK_LEN		64
#define	SHA1_HMAC_BLOCK_LEN		64
#define	RIPEMD160_HMAC_BLOCK_LEN	64
#define	SHA2_256_HMAC_BLOCK_LEN	64
#define	SHA2_384_HMAC_BLOCK_LEN	128
#define	SHA2_512_HMAC_BLOCK_LEN	128
/* Maximum HMAC block length */
#define	HMAC_MAX_BLOCK_LEN	SHA2_512_HMAC_BLOCK_LEN /* Keep this updated */
#define	HMAC_IPAD_VAL			0x36
#define	HMAC_OPAD_VAL			0x5C
/* HMAC Key Length */
#define	NULL_HMAC_KEY_LEN		0
#define	MD5_HMAC_KEY_LEN		16
#define	SHA1_HMAC_KEY_LEN		20
#define	RIPEMD160_HMAC_KEY_LEN		20
#define	SHA2_256_HMAC_KEY_LEN		32
#define	SHA2_384_HMAC_KEY_LEN		48
#define	SHA2_512_HMAC_KEY_LEN		64
#define	AES_128_GMAC_KEY_LEN		16
#define	AES_192_GMAC_KEY_LEN		24
#define	AES_256_GMAC_KEY_LEN		32

/* Encryption algorithm block sizes */
#define	NULL_BLOCK_LEN		4	/* IPsec to maintain alignment */
#define	DES_BLOCK_LEN		8
#define	DES3_BLOCK_LEN		8
#define	BLOWFISH_BLOCK_LEN	8
#define	SKIPJACK_BLOCK_LEN	8
#define	CAST128_BLOCK_LEN	8
#define	RIJNDAEL128_BLOCK_LEN	16
#define	AES_BLOCK_LEN		16
#define	AES_ICM_BLOCK_LEN	1
#define	ARC4_BLOCK_LEN		1
#define	CAMELLIA_BLOCK_LEN	16
#define	EALG_MAX_BLOCK_LEN	AES_BLOCK_LEN /* Keep this updated */

/* IV Lengths */

#define	ARC4_IV_LEN		1
#define	AES_GCM_IV_LEN		12
#define	AES_XTS_IV_LEN		8
#define	AES_XTS_ALPHA		0x87	/* GF(2^128) generator polynomial */

/* Min and Max Encryption Key Sizes */
#define	NULL_MIN_KEY		0
#define	NULL_MAX_KEY		256 /* 2048 bits, max key */
#define	DES_MIN_KEY		8
#define	DES_MAX_KEY		DES_MIN_KEY
#define	TRIPLE_DES_MIN_KEY	24
#define	TRIPLE_DES_MAX_KEY	TRIPLE_DES_MIN_KEY
#define	BLOWFISH_MIN_KEY	5
#define	BLOWFISH_MAX_KEY	56 /* 448 bits, max key */
#define	CAST_MIN_KEY		5
#define	CAST_MAX_KEY		16
#define	SKIPJACK_MIN_KEY	10
#define	SKIPJACK_MAX_KEY	SKIPJACK_MIN_KEY
#define	RIJNDAEL_MIN_KEY	16
#define	RIJNDAEL_MAX_KEY	32
#define	AES_MIN_KEY		RIJNDAEL_MIN_KEY
#define	AES_MAX_KEY		RIJNDAEL_MAX_KEY
#define	AES_XTS_MIN_KEY		(2 * AES_MIN_KEY)
#define	AES_XTS_MAX_KEY		(2 * AES_MAX_KEY)
#define	ARC4_MIN_KEY		1
#define	ARC4_MAX_KEY		32
#define	CAMELLIA_MIN_KEY	8
#define	CAMELLIA_MAX_KEY	32

/* Maximum hash algorithm result length */
#define	AALG_MAX_RESULT_LEN	64 /* Keep this updated */

#define	CRYPTO_ALGORITHM_MIN	1
#define	CRYPTO_DES_CBC		1
#define	CRYPTO_3DES_CBC		2
#define	CRYPTO_BLF_CBC		3
#define	CRYPTO_CAST_CBC		4
#define	CRYPTO_SKIPJACK_CBC	5
#define	CRYPTO_MD5_HMAC		6
#define	CRYPTO_SHA1_HMAC	7
#define	CRYPTO_RIPEMD160_HMAC	8
#define	CRYPTO_MD5_KPDK		9
#define	CRYPTO_SHA1_KPDK	10
#define	CRYPTO_RIJNDAEL128_CBC	11 /* 128 bit blocksize */
#define	CRYPTO_AES_CBC		11 /* 128 bit blocksize -- the same as above */
#define	CRYPTO_ARC4		12
#define	CRYPTO_MD5		13
#define	CRYPTO_SHA1		14
#define	CRYPTO_NULL_HMAC	15
#define	CRYPTO_NULL_CBC		16
#define	CRYPTO_DEFLATE_COMP	17 /* Deflate compression algorithm */
#define	CRYPTO_SHA2_256_HMAC	18
#define	CRYPTO_SHA2_384_HMAC	19
#define	CRYPTO_SHA2_512_HMAC	20
#define	CRYPTO_CAMELLIA_CBC	21
#define	CRYPTO_AES_XTS		22
#define	CRYPTO_AES_ICM		23 /* commonly known as CTR mode */
#define	CRYPTO_AES_NIST_GMAC	24 /* cipher side */
#define	CRYPTO_AES_NIST_GCM_16	25 /* 16 byte ICV */
#define	CRYPTO_AES_128_NIST_GMAC 26 /* auth side */
#define	CRYPTO_AES_192_NIST_GMAC 27 /* auth side */
#define	CRYPTO_AES_256_NIST_GMAC 28 /* auth side */
#define	CRYPTO_ALGORITHM_MAX	28 /* Keep updated - see below */

#define	CRYPTO_ALGO_VALID(x)	((x) >= CRYPTO_ALGORITHM_MIN && \
				 (x) <= CRYPTO_ALGORITHM_MAX)

/* Algorithm flags */
#define	CRYPTO_ALG_FLAG_SUPPORTED	0x01 /* Algorithm is supported */
#define	CRYPTO_ALG_FLAG_RNG_ENABLE	0x02 /* Has HW RNG for DH/DSA */
#define	CRYPTO_ALG_FLAG_DSA_SHA		0x04 /* Can do SHA on msg */

struct symmetric_alg_t {
        int (*ctxinit)(symmetric_alg_ctx_t *ctx, int enc, const u_char *key,
                       size_t keylen, u_char *iv);
        int (*encrypt)(symmetric_alg_ctx_t *ctx, u_char *data, size_t len);
        int (*decrypt)(symmetric_alg_ctx_t *ctx, u_char *data, size_t len);
};

union symmetric_alg_ctx_t {
        struct aes_xts_ctx aes_xts;
        struct aes_cbc_ctx aes_cbc;
};

/* Initalize a key and decrypt data */
extern int decrypt_symmetric(const symmetric_alg_t *alg, u_char *data,
                             size_t datalen, const u_char *key, size_t keylen,
                             u_char *iv);
extern int encrypt_symmetric(const symmetric_alg_t *alg, u_char *data,
                             size_t datalen, const u_char *key, size_t keylen,
                             u_char *iv);
extern const symmetric_alg_t* get_symmetric_alg(int alg);

#endif
