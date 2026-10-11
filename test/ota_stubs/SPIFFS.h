#pragma once
#include "Arduino.h"
constexpr const char *FILE_READ = "r";
struct File {
	explicit operator bool() const { return false; }
	size_t size() const { return 0; }
	void close() {}
	int read() { return -1; }
	size_t readBytes(char *, size_t) { return 0; }
};
struct SPIFFSStub {
	bool begin(bool) { return false; }
	File open(const char *, const char *) { return {}; }
};
inline SPIFFSStub SPIFFS;