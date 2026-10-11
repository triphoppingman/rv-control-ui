#pragma once
#include <openssl/hmac.h>
#include <cstddef>
#include <cstdint>
constexpr int MBEDTLS_MD_SHA256 = 1;
struct mbedtls_md_info_t {};
inline const mbedtls_md_info_t *mbedtls_md_info_from_type(int type) {
	static mbedtls_md_info_t info;
	return type == MBEDTLS_MD_SHA256 ? &info : nullptr;
}
inline int mbedtls_md_hmac(const mbedtls_md_info_t *info, const uint8_t *key, size_t keySize,
		const uint8_t *data, size_t size, uint8_t *output) {
	unsigned int written = 0;
	return info && HMAC(EVP_sha256(), key, static_cast<int>(keySize), data, size, output, &written)
		&& written == 32 ? 0 : -1;
}