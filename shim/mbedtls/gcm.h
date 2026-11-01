#pragma once
// ESP-NOW's mesh crypto: encrypts nothing, because no radio is emulated.
#include <stddef.h>
#include <string.h>
struct mbedtls_gcm_context { int unused; };
#define MBEDTLS_CIPHER_ID_AES 2
#define MBEDTLS_GCM_ENCRYPT 1
#define MBEDTLS_GCM_DECRYPT 0
inline void mbedtls_gcm_init(mbedtls_gcm_context *) {}
inline void mbedtls_gcm_free(mbedtls_gcm_context *) {}
inline int mbedtls_gcm_setkey(mbedtls_gcm_context *, int, const unsigned char *, unsigned) { return 0; }
inline int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *, int, size_t length, const unsigned char *, size_t, const unsigned char *, size_t,
                                     const unsigned char *input, unsigned char *output, size_t tagLength, unsigned char *tag) {
  memmove(output, input, length); memset(tag, 0, tagLength); return 0;
}
inline int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *, size_t length, const unsigned char *, size_t, const unsigned char *, size_t,
                                    const unsigned char *, size_t, const unsigned char *input, unsigned char *output) {
  memmove(output, input, length); return 0;
}
