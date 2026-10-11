#pragma once
#include <cstddef>
#include <cstdint>
inline uint8_t randomByte = 0xab;
inline void esp_fill_random(void *data, size_t size) {
	auto *bytes = static_cast<uint8_t *>(data);
	for (size_t index = 0; index < size; ++index) bytes[index] = randomByte;
}