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

#include <crypto/intake.h>
#include <efi.h>
#include <efilib.h>
#include <efisec.h>
#include <stdbool.h>
#include <string.h>
#include <bootstrap.h>

#include <sys/linker.h>

#include "efi_drivers.h"
#include "key_inject.h"

#define MAX_KEYS 64

#define KEY_ATTR_SERVICE_ID_NAME "SERVICE_ID"
#define KEY_ATTR_SERVICE_ID_NAME_LEN 10

enum key_attr_id_t {
        KEY_ATTR_SERVICE_ID_GELI,
        KEY_ATTR_SERVICE_ID_PASSPHRASE
};

/* Tell who provided the key */
enum service_id_t {
        SERVICE_ID_NONE,
        SERVICE_ID_GELI,
        SERVICE_ID_PASSPHRASE
};

typedef struct key_entry_t {
        UINT8 k_id_size;
        UINT8 k_id[EFI_KMS_KEY_IDENTIFIER_MAX_SIZE];
        enum service_id_t k_service;
        EFI_GUID k_format;
        char k_data[MAX_KEY_BYTES];
} key_entry_t;

static key_entry_t keys[MAX_KEYS];

static int service_id_geli = SERVICE_ID_GELI;
static int service_id_passphrase = SERVICE_ID_PASSPHRASE;
static EFI_GUID Generic128Guid = EFI_KMS_FORMAT_GENERIC_128_GUID;
static EFI_GUID Generic256Guid = EFI_KMS_FORMAT_GENERIC_256_GUID;
static EFI_GUID Generic512Guid = EFI_KMS_FORMAT_GENERIC_512_GUID;
static EFI_GUID Generic1024Guid = EFI_KMS_FORMAT_GENERIC_1024_GUID;
static EFI_GUID Generic2048Guid = EFI_KMS_FORMAT_GENERIC_2048_GUID;
static EFI_GUID Generic3072Guid = EFI_KMS_FORMAT_GENERIC_3072_GUID;
static EFI_GUID AesXts128Guid = EFI_KMS_FORMAT_AESXTS_128_GUID;
static EFI_GUID AesXts256Guid = EFI_KMS_FORMAT_AESXTS_256_GUID;
static EFI_GUID AesCbc128Guid = EFI_KMS_FORMAT_AESCBC_128_GUID;
static EFI_GUID AesCbc256Guid = EFI_KMS_FORMAT_AESCBC_256_GUID;
static EFI_GUID RsaSha2048Guid = EFI_KMS_FORMAT_RSASHA256_2048_GUID;
static EFI_GUID RsaSha3072Guid = EFI_KMS_FORMAT_RSASHA256_3072_GUID;
static EFI_GUID EfiKmsProtocolGuid = EFI_KMS_PROTOCOL;
static EFI_GUID KernelKeyInjectorGuid = KERNEL_KEY_INJECTOR_GUID;

static EFI_KMS_SERVICE key_inject_kms;

static void
fill_keybuf(struct keybuf *keybuf)
{
        int i, idx;

        for (i = 0, idx = 0; i < MAX_KEYS; i++) {
                switch (keys[i].k_service) {
                default:
                        printf("Unknown service type %u\n", keys[i].k_service);

                case SERVICE_ID_PASSPHRASE:
                case SERVICE_ID_NONE:
                        break;

                case SERVICE_ID_GELI:
                        keybuf->kb_ents[idx].ke_type = KEYBUF_TYPE_GELI;
                        memcpy(keybuf->kb_ents[idx].ke_data, keys[i].k_data,
                            MAX_KEY_BYTES);
                        idx++;
                        break;
                }
        }

        keybuf->kb_nents = idx;
}

static EFI_STATUS EFIAPI
register_client_impl(EFI_KMS_SERVICE *This, EFI_KMS_CLIENT_INFO *Client,
    UINTN *ClientDataState __unused, VOID **ClientData __unused)
{
        size_t keybuf_size = sizeof(struct keybuf) +
            (MAX_KEYS * sizeof(struct keybuf_ent));
        char buf[keybuf_size];
	struct preloaded_file *kfp;

        /* Spec compliance */
        if (This == NULL || Client == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        if (Client->ClientIdSize != sizeof(struct preloaded_file *)) {
                return (EFI_INVALID_PARAMETER);
        }

	kfp = (struct preloaded_file *)Client->ClientId;
        fill_keybuf((struct keybuf *)buf);
        file_addmetadata(kfp, MODINFOMD_KEYBUF, keybuf_size, buf);

        return (EFI_SUCCESS);
}

/* We don't support secure creation of keys at runtime! */
static EFI_STATUS EFIAPI
create_key_impl(EFI_KMS_SERVICE *This __unused,
    EFI_KMS_CLIENT_INFO *Client __unused, UINT16 *KeyDescriptorCount __unused,
    EFI_KMS_KEY_DESCRIPTOR *KeyDescriptor __unused,
    UINTN *ClientDataSize __unused, VOID **ClientData __unused)
{
        return (EFI_UNSUPPORTED);
}

static EFI_STATUS
key_size(const EFI_GUID *format, size_t *size)
{
        if (!memcmp(format, &Generic128Guid, sizeof(EFI_GUID))) {
                *size = 128 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &Generic256Guid, sizeof(EFI_GUID))) {
                *size = 256 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &Generic512Guid, sizeof(EFI_GUID))) {
                *size = 512 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &Generic1024Guid, sizeof(EFI_GUID))) {
                *size = 1024 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &Generic2048Guid, sizeof(EFI_GUID))) {
                *size = 2048 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &Generic3072Guid, sizeof(EFI_GUID))) {
                *size = 3072 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &AesXts128Guid, sizeof(EFI_GUID))) {
                *size = 128 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &AesXts256Guid, sizeof(EFI_GUID))) {
                *size = 256 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &AesCbc128Guid, sizeof(EFI_GUID))) {
                *size = 128 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &AesCbc256Guid, sizeof(EFI_GUID))) {
                *size = 256 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &RsaSha2048Guid, sizeof(EFI_GUID))) {
                *size = 2048 / 8;
                return (EFI_SUCCESS);
        } else if (!memcmp(format, &RsaSha3072Guid, sizeof(EFI_GUID))) {
                *size = 3072 / 8;
                return (EFI_SUCCESS);
        } else {
                return (EFI_INVALID_PARAMETER);
        }
}

static EFI_STATUS
copy_key(void *dst, const void *src, const EFI_GUID *format)
{
        EFI_STATUS status;
        size_t size;

        status = key_size(format, &size);

        if (EFI_ERROR(status)) {
                return (status);
        }

        memcpy(dst, src, size);

        return (EFI_SUCCESS);
}

static void
get_one_key(EFI_KMS_KEY_DESCRIPTOR *KeyDescriptor)
{
        EFI_STATUS status;
        int i;

        for (i = 0; i < MAX_KEYS; i++) {
                if (keys[i].k_id_size != 0 &&
                    keys[i].k_id_size == KeyDescriptor->KeyIdentifierSize &&
                    !memcmp(keys[i].k_id, KeyDescriptor->KeyIdentifier,
                        keys[i].k_id_size)) {
                        memcpy(&(KeyDescriptor->KeyFormat), &keys[i].k_format,
                            sizeof(EFI_GUID));
                        status = copy_key(KeyDescriptor->KeyValue,
                            keys[i].k_data, &keys[i].k_format);
                        KeyDescriptor->KeyStatus = status;

                        return;
                }
        }

        KeyDescriptor->KeyStatus = EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI
get_key_impl(EFI_KMS_SERVICE *This, EFI_KMS_CLIENT_INFO *Client,
    UINT16 *KeyDescriptorCount, EFI_KMS_KEY_DESCRIPTOR *KeyDescriptor,
    UINTN *ClientDataSize __unused, VOID **ClientData __unused)
{
        int i;

        /* Spec compliance */
        if (This == NULL || KeyDescriptorCount == NULL ||
            KeyDescriptor == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        for(i = 0; i < *KeyDescriptorCount; i++) {
                get_one_key(KeyDescriptor + i);
        }

        return (EFI_SUCCESS);
}

static void
add_one_key(EFI_KMS_KEY_DESCRIPTOR *KeyDescriptor)
{
        EFI_STATUS status;
        int i;

        for (i = 0; i < MAX_KEYS; i++) {
                if (keys[i].k_id_size == 0) {
                        keys[i].k_id_size = KeyDescriptor->KeyIdentifierSize;
                        memcpy(keys[i].k_id, KeyDescriptor->KeyIdentifier,
                            keys[i].k_id_size);
                        memcpy(&(keys[i].k_format), &(KeyDescriptor->KeyFormat),
                            sizeof(EFI_GUID));
                        status = copy_key(keys[i].k_data,
                            KeyDescriptor->KeyValue, &(keys[i].k_format));
                        KeyDescriptor->KeyStatus = status;

                        return;
                }
        }

        KeyDescriptor->KeyStatus = EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI
add_key_impl(EFI_KMS_SERVICE *This,
    EFI_KMS_CLIENT_INFO *Client __unused, UINT16 *KeyDescriptorCount,
    EFI_KMS_KEY_DESCRIPTOR *KeyDescriptor, UINTN *ClientDataSize __unused,
    VOID **ClientData __unused)
{
        int i;

        /* Spec compliance */
        if (This == NULL || KeyDescriptorCount == NULL ||
            KeyDescriptor == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        for (i = 0; i < *KeyDescriptorCount; i++) {
                add_one_key(KeyDescriptor + i);
        }

        return (EFI_SUCCESS);
}

static void
delete_one_key(EFI_KMS_KEY_DESCRIPTOR *KeyDescriptor)
{
        int i;

        for (i = 0; i < MAX_KEYS; i++) {
                if (keys[i].k_id_size != 0 &&
                    keys[i].k_id_size == KeyDescriptor->KeyIdentifierSize &&
                    !memcmp(keys[i].k_id, KeyDescriptor->KeyIdentifier,
                        keys[i].k_id_size)) {
                        memset(keys + i, 0, sizeof(key_entry_t));

                        return;
                }
        }

        KeyDescriptor->KeyStatus = EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI
delete_key_impl(EFI_KMS_SERVICE *This,
    EFI_KMS_CLIENT_INFO *Client __unused, UINT16 *KeyDescriptorCount,
    EFI_KMS_KEY_DESCRIPTOR *KeyDescriptor, UINTN *ClientDataSize __unused,
    VOID **ClientData __unused)
{
        int i;

        /* Spec compliance */
        if (This == NULL || KeyDescriptorCount == NULL ||
            KeyDescriptor == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        for (i = 0; i < *KeyDescriptorCount; i++) {
                delete_one_key(KeyDescriptor + i);
        }


        return (EFI_SUCCESS);
}

static EFI_STATUS EFIAPI
get_service_status_impl(EFI_KMS_SERVICE *This __unused)
{
        return (EFI_SUCCESS);
}

static void
do_get_key_attributes(key_entry_t *entry, EFI_KMS_KEY_ATTRIBUTE *KeyAttributes)
{
        KeyAttributes[0].KeyAttributeIdentifierType = EFI_KMS_DATA_TYPE_UTF8;
        KeyAttributes[0].KeyAttributeIdentifierCount =
            sizeof KEY_ATTR_SERVICE_ID_NAME;
        KeyAttributes[0].KeyAttributeIdentifier = KEY_ATTR_SERVICE_ID_NAME;
        KeyAttributes[0].KeyAttributeInstance = 1;
        KeyAttributes[0].KeyAttributeType = EFI_KMS_ATTRIBUTE_TYPE_INTEGER;
        KeyAttributes[0].KeyAttributeValueSize = sizeof(int);
        *((int*)(KeyAttributes[0].KeyAttributeValue)) = entry->k_service;
        KeyAttributes[0].KeyAttributeStatus = EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
get_key_attributes_impl(EFI_KMS_SERVICE *This,
    EFI_KMS_CLIENT_INFO *Client __unused, UINT8 *KeyIdentifierSize,
    const VOID *KeyIdentifier, UINT16 *KeyAttributesCount,
    EFI_KMS_KEY_ATTRIBUTE *KeyAttributes, UINTN *ClientDataSize __unused,
    VOID **ClientData __unused)
{
        UINTN i;

        if (This == NULL || KeyIdentifierSize == NULL ||
            KeyIdentifier == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        if (*KeyAttributesCount < 1) {
                return (EFI_BUFFER_TOO_SMALL);
        }

        for (i = 0; i < MAX_KEYS; i++) {
                if (keys[i].k_id_size != 0 &&
                    keys[i].k_id_size == *KeyIdentifierSize &&
                    !memcmp(keys[i].k_id, KeyIdentifier, *KeyIdentifierSize)) {
                        do_get_key_attributes(keys + i, KeyAttributes);

                        return (EFI_SUCCESS);
                }
        }

        return (EFI_NOT_FOUND);
}

static void
do_add_key_attributes(key_entry_t *entry, EFI_KMS_KEY_ATTRIBUTE *KeyAttribute)
{
        if (KeyAttribute->KeyAttributeIdentifierCount ==
            KEY_ATTR_SERVICE_ID_NAME_LEN &&
            !memcmp(KEY_ATTR_SERVICE_ID_NAME,
                KeyAttribute->KeyAttributeIdentifier,
                KEY_ATTR_SERVICE_ID_NAME_LEN)) {
                entry->k_service = *((int*)(KeyAttribute->KeyAttributeValue));
                KeyAttribute->KeyAttributeStatus = EFI_SUCCESS;
        } else {
                KeyAttribute->KeyAttributeStatus = EFI_INVALID_PARAMETER;
        }
}

static EFI_STATUS EFIAPI
add_key_attributes_impl(EFI_KMS_SERVICE *This,
    EFI_KMS_CLIENT_INFO *Client __unused, UINT8 *KeyIdentifierSize,
    const VOID *KeyIdentifier, UINT16 *KeyAttributesCount,
    EFI_KMS_KEY_ATTRIBUTE *KeyAttributes, UINTN *ClientDataSize __unused,
    VOID **ClientData __unused)
{
        UINTN i, j;

        if (This == NULL || KeyIdentifierSize == NULL ||
            KeyIdentifier == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        for (i = 0; i < MAX_KEYS; i++) {
                if(keys[i].k_id_size != 0 &&
                   keys[i].k_id_size == *KeyIdentifierSize &&
                   !memcmp(keys[i].k_id, KeyIdentifier, *KeyIdentifierSize)) {
                        for (j = 0; j < *KeyAttributesCount; j++) {
                                do_add_key_attributes(keys + i,
                                    KeyAttributes + j);

                        }

                        return (EFI_SUCCESS);
                }
        }

        return (EFI_NOT_FOUND);
}

static void
do_delete_key_attributes(key_entry_t *entry,
    EFI_KMS_KEY_ATTRIBUTE *KeyAttribute)
{
        if (KeyAttribute->KeyAttributeIdentifierCount ==
            KEY_ATTR_SERVICE_ID_NAME_LEN &&
            !memcmp(KEY_ATTR_SERVICE_ID_NAME,
                KeyAttribute->KeyAttributeIdentifier,
                KEY_ATTR_SERVICE_ID_NAME_LEN)) {
                entry->k_service = SERVICE_ID_NONE;
                KeyAttribute->KeyAttributeStatus = EFI_SUCCESS;
        } else {
                KeyAttribute->KeyAttributeStatus = EFI_INVALID_PARAMETER;
        }
}

static EFI_STATUS EFIAPI
delete_key_attributes_impl(EFI_KMS_SERVICE *This,
    EFI_KMS_CLIENT_INFO *Client __unused, UINT8 *KeyIdentifierSize,
    const VOID *KeyIdentifier, UINT16 *KeyAttributesCount,
    EFI_KMS_KEY_ATTRIBUTE *KeyAttributes, UINTN *ClientDataSize __unused,
    VOID **ClientData __unused)
{
        UINTN i, j;

        if (This == NULL || KeyIdentifierSize == NULL ||
            KeyIdentifier == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        for (i = 0; i < MAX_KEYS; i++) {
                if(keys[i].k_id_size != 0 &&
                   keys[i].k_id_size == *KeyIdentifierSize &&
                   !memcmp(keys[i].k_id, KeyIdentifier, *KeyIdentifierSize)) {
                        for (j = 0; j < *KeyAttributesCount; j++) {
                                do_delete_key_attributes(keys + i,
                                    KeyAttributes + j);
                        }

                        return (EFI_SUCCESS);
                }
        }

        return (EFI_NOT_FOUND);
}

static EFI_STATUS
check_match(key_entry_t *entry, EFI_KMS_KEY_ATTRIBUTE *KeyAttribute,
    bool *match)
{
        if (KeyAttribute->KeyAttributeIdentifierCount ==
            KEY_ATTR_SERVICE_ID_NAME_LEN &&
            !memcmp(KEY_ATTR_SERVICE_ID_NAME,
                KeyAttribute->KeyAttributeIdentifier,
                KEY_ATTR_SERVICE_ID_NAME_LEN)) {
                *match = (entry->k_service ==
                          *((int*)(KeyAttribute->KeyAttributeValue)));

                return (EFI_SUCCESS);
        } else {
                return (EFI_INVALID_PARAMETER);
        }
}

static EFI_STATUS EFIAPI
get_key_by_attributes_impl(EFI_KMS_SERVICE *This,
    EFI_KMS_CLIENT_INFO *Client __unused, UINTN *KeyAttributesCount,
    EFI_KMS_KEY_ATTRIBUTE *KeyAttributes, UINTN *KeyDescriptorCount,
    EFI_KMS_KEY_DESCRIPTOR *KeyDescriptors, UINTN *ClientDataSize __unused,
    VOID **ClientData __unused)
{
        EFI_STATUS status;
        UINT8 idxs[MAX_KEYS];
        UINT8 nmatches = 0;
        UINTN i, j;
        bool match;

        if (This == NULL || KeyAttributesCount == NULL ||
            KeyAttributes == NULL || KeyDescriptorCount == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        for (i = 0; i < MAX_KEYS; i++) {
                match = true;

                for (j = 0; j < *KeyAttributesCount && match; j++) {
                        status = check_match(keys + i, KeyAttributes + j,
                            &match);

                        if (EFI_ERROR(status)) {
                                return (status);
                        }
                }

                if (match) {
                        idxs[nmatches] = i;
                        nmatches++;
                }
        }

        if (nmatches == 0) {
                return (EFI_NOT_FOUND);
        }

        if (*KeyDescriptorCount < nmatches) {
                *KeyDescriptorCount = nmatches;

                return (EFI_BUFFER_TOO_SMALL);
        }

        if (KeyDescriptors == NULL) {
                return (EFI_INVALID_PARAMETER);
        }

        *KeyDescriptorCount = nmatches;

        for (i = 0; i < nmatches; i++) {
                KeyDescriptors[i].KeyIdentifierSize = keys[idxs[i]].k_id_size;
                KeyDescriptors[i].KeyIdentifier = keys[idxs[i]].k_id;
                memcpy(&(KeyDescriptors[i].KeyFormat),
                    &(keys[idxs[i]].k_format), sizeof(EFI_GUID));
                status = copy_key(KeyDescriptors[i].KeyValue,
                    keys[idxs[i]].k_data, &keys[idxs[i]].k_format);
                KeyDescriptors[i].KeyStatus = status;
        }

        return (EFI_SUCCESS);
}

static void
register_kms(void)
{
	EFI_STATUS status;
        EFI_HANDLE handle = NULL;

        status = BS->InstallMultipleProtocolInterfaces(&handle,
            &EfiKmsProtocolGuid, &key_inject_kms, NULL);

        if (EFI_ERROR(status)) {
              printf("Could not register kernel KMS (%lu)\n",
                  EFI_ERROR_CODE(status));
        }
}

static void
init(void)
{
	EFI_HANDLE *handles;
        EFI_KMS_SERVICE *kms;
	EFI_STATUS status;
	UINTN sz;
	u_int n, nin;
        bool found;

        /* Try and find an instance of our KMS */
	sz = 0;
	handles = NULL;
	status = BS->LocateHandle(ByProtocol, &EfiKmsProtocolGuid, 0, &sz, 0);

	if (status == EFI_BUFFER_TOO_SMALL) {
		handles = (EFI_HANDLE *)malloc(sz);
		status = BS->LocateHandle(ByProtocol, &EfiKmsProtocolGuid,
                    0, &sz, handles);

                if (status == EFI_NOT_FOUND) {
                        /* No handles found, just register our KMS */
                        register_kms();

                        return;
                } else if (EFI_ERROR(status)) {
                        printf("Could not get KMS device handles (%lu)\n",
                            EFI_ERROR_CODE(status));
			free(handles);

                        return;
                }
        } else if (status == EFI_NOT_FOUND) {
                register_kms();

                return;
        } else {
                printf("Could not get KMS device handles (%lu)\n",
                       EFI_ERROR_CODE(status));

                return;
        }

	nin = sz / sizeof(EFI_HANDLE);

	for (n = 0; n < nin && !found; n++) {
                status = BS->OpenProtocol(handles[n], &KernelKeyInjectorGuid,
                    (void**)&kms, IH, handles[n],
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL);

                if (EFI_ERROR(status)) {
                        printf("Could not open KMS service (%lu)\n",
                            EFI_ERROR_CODE(status));
                        return;
                }

                if (!memcmp(&KernelKeyInjectorGuid, &(kms->ServiceId),
                    sizeof(EFI_GUID))) {
                        found = true;
                }

                BS->CloseProtocol(handles[n], &KernelKeyInjectorGuid,
                    IH, handles[n]);
	}

	free(handles);

        if (!found) {
                register_kms();
        }
}

static CHAR16 kernel_inject_str[] = {
  'F', 'r', 'e', 'e', 'B', 'S', 'D', ' ',
  'K', 'e', 'r', 'n', 'e', 'l', ' ',
  'I', 'n', 'j', 'e', 'c', 't', 'i', 'o', 'n', ' ',
  'K', 'M', 'S', '\0'
};

static EFI_GUID key_formats[] = {
        EFI_KMS_FORMAT_GENERIC_128_GUID,
        EFI_KMS_FORMAT_GENERIC_160_GUID,
        EFI_KMS_FORMAT_GENERIC_256_GUID,
        EFI_KMS_FORMAT_GENERIC_512_GUID,
        EFI_KMS_FORMAT_GENERIC_1024_GUID,
        EFI_KMS_FORMAT_GENERIC_2048_GUID,
        EFI_KMS_FORMAT_GENERIC_3072_GUID,
        EFI_KMS_FORMAT_SHA256_GUID,
        EFI_KMS_FORMAT_SHA512_GUID,
        EFI_KMS_FORMAT_AESXTS_128_GUID,
        EFI_KMS_FORMAT_AESXTS_256_GUID,
        EFI_KMS_FORMAT_AESCBC_128_GUID,
        EFI_KMS_FORMAT_AESCBC_256_GUID,
        EFI_KMS_FORMAT_RSASHA256_2048_GUID,
        EFI_KMS_FORMAT_RSASHA256_3072_GUID
};


static EFI_KMS_KEY_ATTRIBUTE key_attributes[] = {
        [KEY_ATTR_SERVICE_ID_GELI] = {
                .KeyAttributeIdentifierType = EFI_KMS_DATA_TYPE_UTF8,
                .KeyAttributeIdentifierCount = KEY_ATTR_SERVICE_ID_NAME_LEN,
                .KeyAttributeIdentifier = KEY_ATTR_SERVICE_ID_NAME,
                .KeyAttributeInstance = 1,
                .KeyAttributeType = EFI_KMS_ATTRIBUTE_TYPE_INTEGER,
                .KeyAttributeValueSize = sizeof(int),
                .KeyAttributeValue = &service_id_geli,
        },
        [KEY_ATTR_SERVICE_ID_PASSPHRASE] = {
                .KeyAttributeIdentifierType = EFI_KMS_DATA_TYPE_UTF8,
                .KeyAttributeIdentifierCount = KEY_ATTR_SERVICE_ID_NAME_LEN,
                .KeyAttributeIdentifier = KEY_ATTR_SERVICE_ID_NAME,
                .KeyAttributeInstance = 2,
                .KeyAttributeType = EFI_KMS_ATTRIBUTE_TYPE_INTEGER,
                .KeyAttributeValueSize = sizeof(int),
                .KeyAttributeValue = &service_id_passphrase,
        }
};

EFI_KMS_KEY_ATTRIBUTE * const key_attr_service_id_geli =
  &(key_attributes[KEY_ATTR_SERVICE_ID_GELI]);

EFI_KMS_KEY_ATTRIBUTE * const key_attr_service_id_passphrase =
  &(key_attributes[KEY_ATTR_SERVICE_ID_PASSPHRASE]);

static EFI_KMS_SERVICE key_inject_kms = {
        .GetServiceStatus = get_service_status_impl,
        .RegisterClient = register_client_impl,
        .CreateKey = create_key_impl,
        .GetKey = get_key_impl,
        .AddKey = add_key_impl,
        .DeleteKey = delete_key_impl,
        .GetKeyAttributes = get_key_attributes_impl,
        .AddKeyAttributes = add_key_attributes_impl,
        .DeleteKeyAttributes = delete_key_attributes_impl,
        .GetKeyByAttributes = get_key_by_attributes_impl,
        .ProtocolVersion = EFI_KMS_PROTOCOL_VERSION,
        .ServiceId = KERNEL_KEY_INJECTOR_GUID,
        .ServiceName = kernel_inject_str,
        .ServiceVersion = 1,
        .ServiceAvailable = true,
        .ClientIdSupported = true,
        .ClientIdRequired = false,
        .ClientNameStringTypes = EFI_KMS_DATA_TYPE_UTF8,
        .ClientNameRequired = true,
        .ClientNameMaxCount = 255,
        .ClientDataSupported = true,
        .ClientDataMaxSize = 0xffffffffffffffff,
        .KeyIdVariableLenSupported = true,
        .KeyIdMaxSize = EFI_KMS_KEY_IDENTIFIER_MAX_SIZE,
        .KeyFormatsCount = sizeof key_formats / sizeof key_formats[0],
        .KeyFormats = key_formats,
        .KeyAttributesSupported = true,
        .KeyAttributeIdStringTypes = EFI_KMS_DATA_TYPE_UTF8,
        .KeyAttributeIdMaxCount = EFI_KMS_KEY_ATTRIBUTE_ID_MAX_SIZE,
        .KeyAttributesCount = sizeof key_attributes / sizeof key_attributes[0],
        .KeyAttributes = key_attributes
};

const efi_driver_t key_inject_driver =
{
	.name = "Key Inject KMS",
	.init = init,
};
