#pragma once

#include <stddef.h>

/** @brief No filesystem access in host tests; only document validation and serialization are exercised. */
struct File {
	explicit operator bool() const { return false; }
	size_t size() const { return 0; }
	void close() {}
	int read() { return -1; }
	size_t readBytes(char *, size_t) { return 0; }
};

struct HostSpiffs {
	bool begin(bool) { return false; }
	File open(const char *, const char *) { return {}; }
};

static HostSpiffs SPIFFS;
#define FILE_READ "r"
