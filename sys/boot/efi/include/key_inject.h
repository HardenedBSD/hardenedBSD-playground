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

#ifndef _KEY_INJECT_H_
#define _KEY_INJECT_H_

/* Registering a client with the name KERNEL and an ID structure as
 * shown below sets the injection point for keys into the kernel.
 */
#define KERNEL_CLIENT_NAME "KERNEL"
#define KERNEL_CLIENT_NAME_LEN 6

#define KERNEL_KEY_INJECTOR_GUID                                        \
  { 0x53badd16, 0x1e9c, 0x493b, { 0x9d, 0x22, 0xe0, 0xab, 0x24, 0xb1, 0xc0, 0x11 } }

extern EFI_KMS_KEY_ATTRIBUTE * const key_attr_service_id_geli;
extern EFI_KMS_KEY_ATTRIBUTE * const key_attr_service_id_passphrase;

/* Structure used as client ID for the "KERNEL" client */
typedef struct {
        void *keybuf;
        size_t nents;
} kernel_client_id_t;

#endif
