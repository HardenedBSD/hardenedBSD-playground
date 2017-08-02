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

int decrypt_symmetric(const symmetric_alg_t *alg, u_char *data, size_t datalen,
                      const u_char *key, size_t keylen, u_char *iv)
{
        symmetric_alg_ctx_t ctx;
        int res;

        res = alg->ctxinit(&ctx, 0, key, keylen, iv);

        if(0 != res) {
                return (res);
        } else {
                return alg->decrypt(&ctx, data, datalen);
        }
}

int encrypt_symmetric(const symmetric_alg_t *alg, u_char *data, size_t datalen,
                      const u_char *key, size_t keylen, u_char *iv)
{
        symmetric_alg_ctx_t ctx;
        int res;

        res = alg->ctxinit(&ctx, 1, key, keylen, iv);

        if(0 != res) {
                return (res);
        } else {
                return alg->encrypt(&ctx, data, datalen);
        }
}

const symmetric_alg_t* get_symmetric_alg(int alg) {
        switch(alg) {
        case CRYPTO_AES_XTS: return &alg_aes_xts;
        case CRYPTO_AES_CBC: return &alg_aes_cbc;
        default: return NULL;
        }
}
