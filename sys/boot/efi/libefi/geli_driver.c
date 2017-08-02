/*-
 * Copyright (c) 2017 Eric McCorkle
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

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include <efisec.h>
#include <sys/endian.h>
#include <sys/gpt.h>
#include <stand.h>
#include <stdbool.h>
#include <crypto/intake.h>

#include "boot_crypto.h"
#include "efi_drivers.h"
#include "key_inject.h"

#define _STRING_H_
#define _STRINGS_H_
#define _STDIO_H_
#include <geom/eli/g_eli.h>
#include <geom/eli/pkcs5v2.h>

#define MAXPWLEN 256

typedef struct {
        struct g_eli_metadata md;
        struct g_eli_softc sc;
        EFI_DISK_IO *diskio;
        EFI_BLOCK_IO *blkio;
        EFI_HANDLE dev;
} geli_info_t;

static EFI_GUID DiskIOProtocolGUID = DISK_IO_PROTOCOL;
static EFI_GUID BlockIOProtocolGUID = BLOCK_IO_PROTOCOL;
static EFI_GUID DevicePathGUID = DEVICE_PATH_PROTOCOL;
static EFI_GUID DriverBindingProtocolGUID = DRIVER_BINDING_PROTOCOL;
static EFI_GUID EfiKmsProtocolGuid = EFI_KMS_PROTOCOL;
static EFI_GUID KernelKeyInjectorGuid = KERNEL_KEY_INJECTOR_GUID;
static EFI_GUID Generic512Guid = EFI_KMS_FORMAT_GENERIC_512_GUID;
static EFI_GUID Generic2048Guid = EFI_KMS_FORMAT_GENERIC_2048_GUID;
static EFI_GUID FreeBSDGELIGUID = FREEBSD_GELI_GUID;
static EFI_DRIVER_BINDING geli_efi_driver;
static EFI_KMS_SERVICE *kms;


int
geliboot_crypt(u_int algid, int enc, u_char *data, size_t datasize,
    const u_char *key, size_t keysize, u_char *iv)
{
        const symmetric_alg_t* alg;

        alg = get_symmetric_alg(algid);

        if (alg == NULL) {
		printf("Unsupported crypto algorithm #%d\n", algid);
		return (1);
        }

        if(enc) {
                return encrypt_symmetric(alg, data, datasize, key, keysize, iv);
        } else {
                return decrypt_symmetric(alg, data, datasize, key, keysize, iv);
        }
}

static int
g_eli_crypto_cipher(u_int algo, int enc, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{
	u_char iv[keysize];

	bzero(iv, sizeof(iv));
	return (geliboot_crypt(algo, enc, data, datasize, key, keysize, iv));
}

int
g_eli_crypto_encrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{

	/* We prefer AES-CBC for metadata protection. */
	if (algo == CRYPTO_AES_XTS)
		algo = CRYPTO_AES_CBC;

	return (g_eli_crypto_cipher(algo, 1, data, datasize, key, keysize));
}

int
g_eli_crypto_decrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{

	/* We prefer AES-CBC for metadata protection. */
	if (algo == CRYPTO_AES_XTS)
		algo = CRYPTO_AES_CBC;

	return (g_eli_crypto_cipher(algo, 0, data, datasize, key, keysize));
}

static void
pwgets(char *buf, int n)
{
        int c;
        char *lp;

        for (lp = buf;;)
	        switch (c = getchar() & 0177) {
                case '\n':
                case '\r':
                        *lp = '\0';
                        putchar('\n');
                        return;
                case '\b':
                case '\177':
	                if (lp > buf) {
                                lp--;
                                putchar('\b');
                                putchar(' ');
                                putchar('\b');
                        }
                        break;
                case 'u' & 037:
                case 'w' & 037:
                        lp = buf;
                        putchar('\n');
                        break;
                default:
                        if ((n < 1) || ((lp - buf) < n - 1)) {
                                *lp++ = c;
                                putchar('*');
                        }
                }
}

static EFI_STATUS
decrypt(unsigned int algid, u_char *data, size_t datasize, u_char *key,
    size_t keysize, u_char *iv)
{
        const symmetric_alg_t *alg;

        alg = get_symmetric_alg(algid);

        if (alg == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        return decrypt_symmetric(alg, data, datasize, key, keysize, iv);
}

#define KEY_ID_SIZE 5

static unsigned int keyid = 0;

static EFI_STATUS
register_key(const struct g_eli_softc *sc, const char *passphrase,
    const u_char key[G_ELI_USERKEYLEN])
{
        EFI_STATUS status;
        char id_buf[EFI_KMS_KEY_IDENTIFIER_MAX_SIZE + 1];
        UINT16 count = 1;
        EFI_KMS_KEY_DESCRIPTOR desc;
        UINT8 key_id_size;

        snprintf(id_buf, EFI_KMS_KEY_IDENTIFIER_MAX_SIZE, "GELI%u", keyid);
        key_id_size = strlen(id_buf);
        desc.KeyIdentifierSize = key_id_size;
        desc.KeyIdentifier = id_buf;
        memcpy(&(desc.KeyFormat), &Generic512Guid, sizeof(EFI_GUID));
        desc.KeyValue = (void*)key;

        status = kms->AddKey(kms, NULL, &count, &desc, NULL, NULL);

        if (EFI_ERROR(status)) {
                printf("Failed to add key %lu\n", EFI_ERROR_CODE(status));
                return (status);
        }

        status = kms->AddKeyAttributes(kms, NULL, &key_id_size, id_buf, &count,
            key_attr_service_id_geli, NULL, NULL);

        if (EFI_ERROR(status)) {
                printf("Failed to add key attributes %lu\n",
                    EFI_ERROR_CODE(status));
                return (status);
        }

        if (passphrase != NULL) {
                snprintf(id_buf, EFI_KMS_KEY_IDENTIFIER_MAX_SIZE, "GELIPASS%u",
                    keyid);
                key_id_size = strlen(id_buf);
                desc.KeyIdentifierSize = key_id_size;
                desc.KeyIdentifier = id_buf;
                memcpy(&(desc.KeyFormat), &Generic2048Guid, sizeof(EFI_GUID));
                desc.KeyValue = (void*)passphrase;

                status = kms->AddKey(kms, NULL, &count, &desc, NULL, NULL);

                if (EFI_ERROR(status)) {
                        printf("Failed to add key %lu\n",
                            EFI_ERROR_CODE(status));
                        return (status);
                }

                status = kms->AddKeyAttributes(kms, NULL, &key_id_size, id_buf,
                    &count, key_attr_service_id_passphrase, NULL, NULL);

                if (EFI_ERROR(status)) {
                        printf("Failed to add key attributes %lu\n",
                            EFI_ERROR_CODE(status));
                        return (status);
                }
        }

        keyid++;

        return (EFI_SUCCESS);
}

static EFI_STATUS
try_password(struct g_eli_metadata *md, struct g_eli_softc *sc,
    char *passphrase, bool register_pw)
{
	u_char key[G_ELI_USERKEYLEN], mkey[G_ELI_DATAIVKEYLEN], *mkp;
	u_int keynum;
	struct hmac_ctx ctx;
	int error;

	g_eli_crypto_hmac_init(&ctx, NULL, 0);
        /*
         * Prepare Derived-Key from the user passphrase.
	 */
	if (md->md_iterations == 0) {
		g_eli_crypto_hmac_update(&ctx, md->md_salt,
                    sizeof(md->md_salt));
		g_eli_crypto_hmac_update(&ctx, passphrase,
		    strlen(passphrase));
	} else if (md->md_iterations > 0) {
		u_char dkey[G_ELI_USERKEYLEN];

                pkcs5v2_genkey(dkey, sizeof(dkey), md->md_salt,
		    sizeof(md->md_salt), passphrase,
		    md->md_iterations);
		g_eli_crypto_hmac_update(&ctx, dkey, sizeof(dkey));
		explicit_bzero(&dkey, sizeof(dkey));
	}

	g_eli_crypto_hmac_final(&ctx, key, 0);

	error = g_eli_mkey_decrypt(md, key, mkey, &keynum);
	if (error == -1) {
                bzero(&key, sizeof(key));
                bzero(&mkey, sizeof(mkey));
		return (EFI_ACCESS_DENIED);
	} else if (error != 0) {
                bzero(&key, sizeof(key));
		bzero(&mkey, sizeof(mkey));
		printf("Failed to decrypt GELI master key: %d\n", error);
		return (EFI_LOAD_ERROR);
	}

        /* Register the new key */
        if (register_pw)
              register_key(sc, passphrase, key);
        else
              register_key(sc, NULL, key);

        bzero(&key, sizeof(key));

	/* Store the keys */
	bcopy(mkey, sc->sc_mkey, sizeof(sc->sc_mkey));
	bcopy(mkey, sc->sc_ivkey, sizeof(sc->sc_ivkey));
	mkp = mkey + sizeof(sc->sc_ivkey);
	if ((sc->sc_flags & G_ELI_FLAG_AUTH) == 0) {
		bcopy(mkp, sc->sc_ekey, G_ELI_DATAKEYLEN);
	} else {
		/*
		 * The encryption key is: ekey = HMAC_SHA512(Data-Key, 0x10)
		 */
		g_eli_crypto_hmac(mkp, G_ELI_MAXKEYLEN, "\x10", 1,
		    sc->sc_ekey, 0);
	}
	bzero(&mkey, sizeof(mkey));

	/* Initialize the per-sector IV */
	switch (sc->sc_ealgo) {
	case CRYPTO_AES_XTS:
		break;
	default:
		SHA256_Init(&sc->sc_ivctx);
		SHA256_Update(&sc->sc_ivctx, sc->sc_ivkey,
		    sizeof(sc->sc_ivkey));
		break;
	}
	return (EFI_SUCCESS);
}

static EFI_STATUS
try_cached_passphrases(struct g_eli_metadata *md, struct g_eli_softc *sc)
{
        EFI_STATUS status;
        UINTN i;
        UINTN count = 1;
        UINTN nkeydescs = 0;
        EFI_KMS_KEY_DESCRIPTOR *keydescs;

        status = kms->GetKeyByAttributes(kms, NULL, &count,
            key_attr_service_id_passphrase, &nkeydescs, NULL, NULL, NULL);

        /* We might get EFI_SUCCESS if there are no keys */
        if (status == EFI_SUCCESS || status == EFI_NOT_FOUND ||
            nkeydescs == 0) {
                return (EFI_ACCESS_DENIED);
        } else if (status != EFI_BUFFER_TOO_SMALL) {
                printf("Error getting number of passphrases: %lu\n",
                       EFI_ERROR_CODE(status));
                return (status);
        }

        keydescs = malloc(nkeydescs * sizeof(EFI_KMS_KEY_DESCRIPTOR));

        for(i = 0; i < nkeydescs; i++) {
                keydescs[i].KeyValue = malloc(MAX_KEY_BYTES);
        }

        if(keydescs == NULL) {
                return (EFI_OUT_OF_RESOURCES);
        }

        status = kms->GetKeyByAttributes(kms, NULL, &count,
            key_attr_service_id_passphrase, &nkeydescs, keydescs, NULL, NULL);

        if (EFI_ERROR(status)) {
                printf("Error getting passphrases: %lu\n",
                    EFI_ERROR_CODE(status));
                free(keydescs);

                return (status);
        }

        for(i = 0; i < nkeydescs; i++) {
                status = try_password(md, sc, keydescs[i].KeyValue, false);

                if (status != EFI_ACCESS_DENIED) {
                        for(; i < nkeydescs; i++) {
                                memset(keydescs[i].KeyValue, 0, MAX_KEY_BYTES);
                                free(keydescs[i].KeyValue);

                        }

                        free(keydescs);
                        return (status);
                }

                memset(keydescs[i].KeyValue, 0, MAX_KEY_BYTES);
                free(keydescs[i].KeyValue);
        }

        free(keydescs);

        return (EFI_ACCESS_DENIED);
}

static EFI_STATUS
try_keys(struct g_eli_metadata *md, struct g_eli_softc *sc)
{
        EFI_STATUS status;
        UINTN i;
        UINTN count = 1;
        UINTN nkeydescs = 0;
        EFI_KMS_KEY_DESCRIPTOR *keydescs;
	u_char mkey[G_ELI_DATAIVKEYLEN], *mkp;
	u_int keynum;

        status = kms->GetKeyByAttributes(kms, NULL, &count,
            key_attr_service_id_geli, &nkeydescs, NULL, NULL, NULL);

        /* We might get EFI_SUCCESS if there are no keys */
        if (status == EFI_SUCCESS || status == EFI_NOT_FOUND ||
            nkeydescs == 0) {
                return (try_cached_passphrases(md, sc));
        } else if (status != EFI_BUFFER_TOO_SMALL) {
                printf("Error getting number of keys: %lu\n",
                    EFI_ERROR_CODE(status));
                return (status);
        }

        keydescs = malloc(nkeydescs * sizeof(EFI_KMS_KEY_DESCRIPTOR));

        for(i = 0; i < nkeydescs; i++) {
                keydescs[i].KeyValue = malloc(MAX_KEY_BYTES);
        }

        if(keydescs == NULL) {
                return (EFI_OUT_OF_RESOURCES);
        }

        status = kms->GetKeyByAttributes(kms, NULL, &count,
            key_attr_service_id_geli, &nkeydescs, keydescs, NULL, NULL);

        if (EFI_ERROR(status)) {
                printf("Error getting keys: %lu\n", EFI_ERROR_CODE(status));
                free(keydescs);

                return (status);
        }

        /* Try all keys with the right encryption type */
        for(i = 0; i < nkeydescs; i++) {
                if(g_eli_mkey_decrypt(md, keydescs[i].KeyValue,
                                      mkey, &keynum) == 0) {
                        for(; i < nkeydescs; i++) {
                                memset(keydescs[i].KeyValue, 0, MAX_KEY_BYTES);
                                free(keydescs[i].KeyValue);

                        }

                        free(keydescs);

                        /* Store the keys */
                        bcopy(mkey, sc->sc_mkey, sizeof(sc->sc_mkey));
                        bcopy(mkey, sc->sc_ivkey, sizeof(sc->sc_ivkey));
                        mkp = mkey + sizeof(sc->sc_ivkey);
                        if ((sc->sc_flags & G_ELI_FLAG_AUTH) == 0) {
                                bcopy(mkp, sc->sc_ekey, G_ELI_DATAKEYLEN);
                        } else {
                          /*
                           * The encryption key is: ekey = HMAC_SHA512(Data-Key, 0x10)
                           */
                          g_eli_crypto_hmac(mkp, G_ELI_MAXKEYLEN, "\x10", 1,
                              sc->sc_ekey, 0);
                        }
                        bzero(&mkey, sizeof(mkey));

                        /* Initialize the per-sector IV */
                        switch (sc->sc_ealgo) {
                        case CRYPTO_AES_XTS:
                                break;
                        default:
                                SHA256_Init(&sc->sc_ivctx);
                                SHA256_Update(&sc->sc_ivctx, sc->sc_ivkey,
                                    sizeof(sc->sc_ivkey));
                                break;
                        }

                        memset(keydescs, 0,
                            nkeydescs * sizeof(EFI_KMS_KEY_DESCRIPTOR));
                        free(keydescs);

                        return (EFI_SUCCESS);
                } else {
                        memset(keydescs[i].KeyValue, 0, MAX_KEY_BYTES);
                        free(keydescs[i].KeyValue);
                }

                memset(keydescs[i].KeyValue, 0, MAX_KEY_BYTES);
        }

        memset(keydescs, 0, nkeydescs * sizeof(EFI_KMS_KEY_DESCRIPTOR));
        free(keydescs);

        return (try_cached_passphrases(md, sc));
}

static EFI_STATUS
ask_password(struct g_eli_metadata *md, struct g_eli_softc *sc) {
        char pwbuf[MAXPWLEN + 1];
        EFI_STATUS status;
        UINTN i;

        for(i = 0, status = EFI_ACCESS_DENIED;
            i < 5 && status == EFI_ACCESS_DENIED; i++) {
                printf("Enter passphrase for encrypted volume: ");
                pwgets(pwbuf, MAXPWLEN);
                status = try_password(md, sc, pwbuf, true);
                memset(pwbuf, 0, MAXPWLEN);

                if (status == EFI_SUCCESS) {
                        printf("OK\n");
                } else if (status == EFI_ACCESS_DENIED) {
                        printf("Incorrect\n");
                } else {
                        printf("Error!\n");
                }
        }

        if (status == EFI_ACCESS_DENIED) {
                printf("Access denied: too many tries\n");
        }

        return (status);
}

static EFI_STATUS
discover(struct g_eli_metadata *md, struct g_eli_softc *sc, EFI_BLOCK_IO *inner,
    EFI_HANDLE dev)
{
	u_char buf[inner->Media->BlockSize];
	int error;
        EFI_STATUS status;

        memset(md, 0, sizeof(struct g_eli_metadata));
        memset(sc, 0, sizeof(struct g_eli_softc));
        status = inner->ReadBlocks(inner, inner->Media->MediaId,
            inner->Media->LastBlock, inner->Media->BlockSize, buf);

        if (status != EFI_SUCCESS) {
                if (status != EFI_NO_MEDIA && status != EFI_MEDIA_CHANGED) {
                        printf("Failed to read last block (%lu)\n",
                            EFI_ERROR_CODE(status));
                }
                return (status);
        }

        error = eli_metadata_decode(buf, md);

        /* EINVAL means not found */
        if (error == EINVAL) {
                return (EFI_NOT_FOUND);
        } else if (error == EOPNOTSUPP) {
                return (EFI_UNSUPPORTED);
        } else if (error != 0) {
                return (EFI_LOAD_ERROR);
        }

	if ((md->md_flags & G_ELI_FLAG_ONETIME)) {
		/* Swap device, skip it */
		return (EFI_NOT_FOUND);
	}

	if (!(md->md_flags & G_ELI_FLAG_BOOT)) {
		/* Disk is not GELI boot device, skip it */
		return (EFI_NOT_FOUND);
	}

        /* First, try all the existing keys */
        status = try_keys(md, sc);

        if (status != EFI_SUCCESS) {
                /* If none of them work, give the user five tries to input a
                 * new password
                 */
                if (status == EFI_ACCESS_DENIED) {
                        status = ask_password(md, sc);

                        if (status != EFI_SUCCESS) {
                                return (status);
                        }
                } else {
                        return (status);
                }
        }

        eli_metadata_softc(sc, md, inner->Media->BlockSize,
            (inner->Media->LastBlock * inner->Media->BlockSize) +
            inner->Media->BlockSize);

        return (EFI_SUCCESS);
}

static EFI_STATUS EFIAPI
reset_impl(EFI_BLOCK_IO *This, BOOLEAN ev)
{
        geli_info_t *info = (geli_info_t*)(This + 1);

        return info->blkio->Reset(info->blkio, ev);
}

static EFI_STATUS EFIAPI
read_impl(EFI_BLOCK_IO *This, UINT32 MediaID, EFI_LBA LBA,
    UINTN BufferSize, VOID *Buffer)
{
        geli_info_t *info = (geli_info_t*)(This + 1);
	char iv[G_ELI_IVKEYLEN];
	char *pbuf = Buffer;
	off_t offset;
	uint64_t keyno;
	size_t n, nb;
	struct g_eli_key gkey;
        EFI_STATUS status;

        // Read the raw data
        status = info->blkio->ReadBlocks(info->blkio,
            info->blkio->Media->MediaId, LBA, BufferSize, Buffer);

        if (EFI_ERROR(status)) {
                printf("Error reading encrypted blocks (%lu)\n",
                    EFI_ERROR_CODE(status));
                return (status);
        }

	nb = BufferSize / info->sc.sc_sectorsize;

	for (n = 0; n < nb; n++) {
                offset = (LBA * info->blkio->Media->BlockSize) +
		    (n * info->sc.sc_sectorsize);
                pbuf = (char*)Buffer + (n * info->sc.sc_sectorsize);

		g_eli_crypto_ivgen(&(info->sc), offset, iv, G_ELI_IVKEYLEN);

		/* Get the key that corresponds to this offset */
		keyno = (offset >> G_ELI_KEY_SHIFT) /
                    info->sc.sc_sectorsize;

		g_eli_key_fill(&(info->sc), &gkey, keyno);

		status = decrypt(info->sc.sc_ealgo, pbuf,
		    info->sc.sc_sectorsize, gkey.gek_key,
                    info->sc.sc_ekeylen, iv);

		if (status != EFI_SUCCESS) {
                        printf("Error decrypting blocks %lu\n",
                            EFI_ERROR_CODE(status));
			explicit_bzero(&gkey, sizeof(gkey));
			return (status);
                }
        }

	explicit_bzero(&gkey, sizeof(gkey));

	return (EFI_SUCCESS);
}

static EFI_STATUS EFIAPI
write_impl(EFI_BLOCK_IO *This __unused, UINT32 MediaID __unused,
    EFI_LBA LBA __unused, UINTN BufferSize __unused, VOID *Buffer __unused)
{
        return (EFI_UNSUPPORTED);
}

static EFI_STATUS EFIAPI
flush_impl(EFI_BLOCK_IO *This)
{
        geli_info_t *info = (geli_info_t*)(This + 1);

        return info->blkio->FlushBlocks(info->blkio);
}

static EFI_BLOCK_IO*
make_block_io_iface(struct g_eli_metadata *md, struct g_eli_softc *sc,
    EFI_BLOCK_IO *inner, EFI_HANDLE dev)
{
        EFI_BLOCK_IO *blkio;
        geli_info_t *info;

        if ((blkio = malloc(sizeof(EFI_BLOCK_IO) + sizeof(geli_info_t))) ==
            NULL) {
                return NULL;
        }

        if ((blkio->Media = malloc(sizeof(EFI_BLOCK_IO_MEDIA))) == NULL) {
                free(blkio);

                return NULL;
        }

        info = (geli_info_t*)(blkio + 1);
        blkio->Revision = EFI_BLOCK_IO_INTERFACE_REVISION;
        blkio->Media->MediaId = inner->Media->MediaId;
        blkio->Media->RemovableMedia = false;
        blkio->Media->MediaPresent = true;
        blkio->Media->LogicalPartition = true;
        blkio->Media->ReadOnly = true;
        blkio->Media->WriteCaching = false;
        blkio->Media->BlockSize = inner->Media->BlockSize;
        blkio->Media->IoAlign = inner->Media->IoAlign;
        blkio->Media->LastBlock = inner->Media->LastBlock - 1;
        blkio->Reset = reset_impl;
        blkio->ReadBlocks = read_impl;
        blkio->WriteBlocks = write_impl;
        blkio->FlushBlocks = flush_impl;
        memcpy(&(info->md), md, sizeof(struct g_eli_metadata));
        memcpy(&(info->sc), sc, sizeof(struct g_eli_softc));
        info->dev = dev;

        return blkio;
}

static EFI_STATUS EFIAPI
supported_impl(EFI_DRIVER_BINDING *This, EFI_HANDLE handle,
    EFI_DEVICE_PATH *RemainingDevicePath __unused)
{
        return (BS->OpenProtocol(handle, &BlockIOProtocolGUID, NULL,
            This->DriverBindingHandle, handle,
            EFI_OPEN_PROTOCOL_TEST_PROTOCOL));
}

static EFI_STATUS EFIAPI
start_impl(EFI_DRIVER_BINDING *This, EFI_HANDLE handle,
    EFI_DEVICE_PATH *RemainingDevicePath __unused)
{
        EFI_BLOCK_IO *blkio;
        EFI_DISK_IO *driver_diskio;
        EFI_STATUS status;
        EFI_BLOCK_IO *newio;
        EFI_HANDLE newhandle = NULL;
        EFI_DEVICE_PATH *devpath, *newpath, *currpath, *newcurr;
        VENDOR_DEVICE_PATH *vendornode;
        struct g_eli_metadata md;
        struct g_eli_softc sc;
        UINTN pathlen;
        geli_info_t *info;

        /* Grab Disk IO to make sure that we don't end up registering
         * this handle twice.
         */
        status = BS->OpenProtocol(handle, &DiskIOProtocolGUID,
            (void**)&driver_diskio, This->DriverBindingHandle, handle,
            EFI_OPEN_PROTOCOL_BY_DRIVER);

        if (EFI_ERROR(status)) {
                if (status != EFI_ACCESS_DENIED &&
                    status != EFI_ALREADY_STARTED &&
                    status != EFI_UNSUPPORTED) {
                        printf("Could not open device %lu\n",
                            EFI_ERROR_CODE(status));
                }

                return (status);
        }

        /* Build device path */
        status = BS->OpenProtocol(handle, &DevicePathGUID, (void**)&devpath,
            This->DriverBindingHandle, handle, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

        if (EFI_ERROR(status)) {
                printf("Failed to obtain device path %lu\n",
                    EFI_ERROR_CODE(status));
                free(newio);

                return (status);
        }

        currpath = devpath;
        pathlen = 0;

        while (!IsDevicePathEnd(currpath)) {
                pathlen += DevicePathNodeLength(currpath);
                currpath = NextDevicePathNode(currpath);
        }

        pathlen += sizeof(EFI_DEVICE_PATH);
        pathlen += sizeof(VENDOR_DEVICE_PATH);
        newpath = malloc(pathlen);

        if (newpath == NULL) {
                printf("Failed to create new device path\n");

                return (status);
        }

        currpath = devpath;
        newcurr = newpath;

        while (!IsDevicePathEnd(currpath)) {
                memcpy(newcurr, currpath, DevicePathNodeLength(currpath));
                currpath = NextDevicePathNode(currpath);
                newcurr = NextDevicePathNode(newcurr);
        }

        vendornode = (VENDOR_DEVICE_PATH *)newcurr;
        vendornode->Header.Type = MEDIA_DEVICE_PATH;
        vendornode->Header.SubType = MEDIA_VENDOR_DP;
        vendornode->Header.Length[0] = sizeof(VENDOR_DEVICE_PATH);
        vendornode->Header.Length[1] = 0;
        memcpy(&(vendornode->Guid), &FreeBSDGELIGUID, sizeof(EFI_GUID));
        newcurr = NextDevicePathNode(newcurr);
        SetDevicePathEndNode(newcurr);
        devpath = newpath;

        BS->CloseProtocol(handle, &DevicePathGUID, This->DriverBindingHandle,
            handle);

        /* Get block IO */
        status = BS->OpenProtocol(handle, &BlockIOProtocolGUID, (void**)&blkio,
            This->DriverBindingHandle, handle, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

        if (EFI_ERROR(status)) {
                printf("Could not open device %lu\n", EFI_ERROR_CODE(status));
                free(newpath);

                return (status);
        }

        /* Test for GELI presence */
        status = discover(&md, &sc, blkio, handle);

        if (EFI_ERROR(status)) {
                BS->CloseProtocol(handle, &BlockIOProtocolGUID,
                    This->DriverBindingHandle, handle);
                free(newpath);

                return (status);
        }

        /* Make Block IO interface */
        newio = make_block_io_iface(&md, &sc, blkio, handle);
        info = (geli_info_t*)(newio + 1);
        info->diskio = driver_diskio;
        BS->CloseProtocol(handle, &BlockIOProtocolGUID,
            This->DriverBindingHandle, handle);

        if (newio == NULL) {
                free(newpath);
                printf("Failed to create new IO interface!\n");
                return (EFI_OUT_OF_RESOURCES);
        }

        /* Create device handle and attach interfaces */
        status = BS->InstallMultipleProtocolInterfaces(&newhandle,
            &BlockIOProtocolGUID, newio, &DevicePathGUID, newpath, NULL);

        if (EFI_ERROR(status)) {
                printf("Could not create child device %lu\n",
                    EFI_ERROR_CODE(status));
                free(newio);
                free(newpath);

                return (status);
        }

        status = BS->OpenProtocol(handle, &BlockIOProtocolGUID,
            (void**)&(info->blkio), This->DriverBindingHandle, newhandle,
            EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);

        if (EFI_ERROR(status)) {
                printf("Could not associate child device %lu\n",
                    EFI_ERROR_CODE(status));
                return (status);
        }

        return (EFI_SUCCESS);
}

static EFI_STATUS EFIAPI
stop_impl(EFI_DRIVER_BINDING *This __unused, EFI_HANDLE handle __unused,
    UINTN NumberOfChildren __unused, EFI_HANDLE *ChildHandleBuffer __unused)
{
        return (EFI_UNSUPPORTED);
}

static EFI_STATUS
locate_kms(void)
{
	EFI_HANDLE *handles;
	EFI_STATUS status;
	UINTN sz;
	u_int n, nin;

        /* Try and find a usable KMS */
	sz = 0;
	handles = NULL;
	status = BS->LocateHandle(ByProtocol, &EfiKmsProtocolGuid, NULL, &sz,
            NULL);

	if (status == EFI_BUFFER_TOO_SMALL) {
		handles = (EFI_HANDLE *)malloc(sz);
		status = BS->LocateHandle(ByProtocol, &EfiKmsProtocolGuid, 0,
                    &sz, handles);
		if (EFI_ERROR(status)) {
                        printf("Error getting KMS handles (%lu)\n",
                            EFI_ERROR_CODE(status));
			free(handles);

                        return (status);
                }
        } else {
                printf("Error getting size of KMS buffer (%lu)\n",
                    EFI_ERROR_CODE(status));

                return (status);
        }

	nin = sz / sizeof(EFI_HANDLE);

	for (n = 0; n < nin; n++) {
                status = BS->OpenProtocol(handles[n], &EfiKmsProtocolGuid,
                    (void**)&kms, IH, handles[n],
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL);

                if (EFI_ERROR(status)) {
                        printf("Failed to obtain KMS protocol interface (%lu)\n",
                            EFI_ERROR_CODE(status));
                        return (status);
                }

                if (!memcmp(&KernelKeyInjectorGuid, &(kms->ServiceId),
                        sizeof(EFI_GUID))) {
                        free(handles);

                        return (EFI_SUCCESS);
                }

                BS->CloseProtocol(handles[n], &KernelKeyInjectorGuid,
                    IH, handles[n]);
	}

        return (EFI_NOT_FOUND);
}

static void
init(void)
{
        EFI_STATUS status;
        EFI_HANDLE *handles;
        UINTN nhandles, i, hsize;

        status = locate_kms();

        if (EFI_ERROR(status)) {
                printf("Error locating usable KMS (%lu)\n",
                    EFI_ERROR_CODE(status));

                return;
        }

        geli_efi_driver.ImageHandle = IH;
        geli_efi_driver.DriverBindingHandle = NULL;
        status = BS->InstallMultipleProtocolInterfaces(
            &(geli_efi_driver.DriverBindingHandle), &DriverBindingProtocolGUID,
            &geli_efi_driver, NULL);

        if (EFI_ERROR(status)) {
                printf("Failed to install GELI driver (%ld)!\n",
                    EFI_ERROR_CODE(status));

                return;
        }

        nhandles = 0;
        hsize = 0;
        status = BS->LocateHandle(ByProtocol, &BlockIOProtocolGUID, NULL,
            &hsize, NULL);

        if (status != EFI_BUFFER_TOO_SMALL) {
                printf("Could not get number of handles! (%ld)\n",
                    EFI_ERROR_CODE(status));
                return;
        }

        handles = malloc(hsize);
        nhandles = hsize / sizeof(EFI_HANDLE);

        status = BS->LocateHandle(ByProtocol, &BlockIOProtocolGUID, NULL,
            &hsize, handles);

        if (EFI_ERROR(status)) {
                printf("Could not get handles! (%ld)\n",
                    EFI_ERROR_CODE(status));
                return;
        }

        for (i = 0; i < nhandles; i++) {
                BS->ConnectController(handles[i], NULL, NULL, false);
        }

        free(handles);
}

static EFI_DRIVER_BINDING geli_efi_driver = {
        .Version = 0x10,
        .Supported = supported_impl,
        .Start = start_impl,
        .Stop = stop_impl
};

const efi_driver_t geli_driver =
{
	.name = "GELI",
	.init = init,
};
