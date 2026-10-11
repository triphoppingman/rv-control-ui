#pragma once
#include <cstdint>
#include <vector>
constexpr int U_FLASH = 0;
struct UpdateStub {
	int begins = 0;
	int writes = 0;
	int aborts = 0;
	int ends = 0;
	size_t requestedSize = 0;
	bool beginOk = true;
	bool writeOk = true;
	bool endOk = true;
	bool active = false;
	bool endEvenIfRemaining = true;
	std::vector<uint8_t> bytes;
	bool begin(size_t size, int command) {
		++begins;
		requestedSize = size;
		active = beginOk && command == U_FLASH;
		return active;
	}
	size_t write(uint8_t *data, size_t size) {
		++writes;
		const size_t written = writeOk ? size : size - 1;
		bytes.insert(bytes.end(), data, data + written);
		return written;
	}
	void abort() { ++aborts; active = false; }
	bool end(bool evenIfRemaining) {
		++ends;
		endEvenIfRemaining = evenIfRemaining;
		if (endOk) active = false;
		return endOk;
	}
};
inline UpdateStub Update;