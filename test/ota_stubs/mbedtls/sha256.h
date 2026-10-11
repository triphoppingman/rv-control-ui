#pragma once
#include <openssl/evp.h>
#include <cstddef>
#include <cstdint>
struct mbedtls_sha256_context {
	EVP_MD_CTX *value = nullptr;
	~mbedtls_sha256_context() { EVP_MD_CTX_free(value); }
};
inline void mbedtls_sha256_init(mbedtls_sha256_context *context) { context->value = EVP_MD_CTX_new(); }
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context *context, int is224) {
	return !is224 && EVP_DigestInit_ex(context->value, EVP_sha256(), nullptr) == 1 ? 0 : -1;
}
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context *context, const uint8_t *data, size_t size) {
	return EVP_DigestUpdate(context->value, data, size) == 1 ? 0 : -1;
}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context *context, uint8_t *output) {
	unsigned int size = 0;
	return EVP_DigestFinal_ex(context->value, output, &size) == 1 && size == 32 ? 0 : -1;
}
inline int mbedtls_sha256_ret(const uint8_t *data, size_t size, uint8_t *output, int is224) {
	unsigned int written = 0;
	return !is224 && EVP_Digest(data, size, output, &written, EVP_sha256(), nullptr) == 1 && written == 32 ? 0 : -1;
}