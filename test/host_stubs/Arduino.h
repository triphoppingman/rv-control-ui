#pragma once

#include <stddef.h>
#include <string.h>

/** @brief Host replacement for the bounded string copy used by config validation. */
inline size_t strlcpy(char *destination, const char *source, size_t size) {
	const size_t length = strlen(source);
	if (size) {
		const size_t copied = length < size - 1 ? length : size - 1;
		memcpy(destination, source, copied);
		destination[copied] = '\0';
	}
	return length;
}
