#pragma once
#include "Arduino.h"
#include <map>
#include <vector>

enum HTTPRawStatus { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
struct HTTPRaw {
	HTTPRawStatus status = RAW_START;
	size_t currentSize = 0;
	uint8_t *buf = nullptr;
};
class WebServer {
public:
	std::map<std::string, String> headers;
	int responseStatus = 0;
	String responseType;
	String responseBody;
	HTTPRaw rawEvent;
	String header(const char *name) const {
		const auto found = headers.find(name);
		return found == headers.end() ? String() : found->second;
	}
	HTTPRaw &raw() { return rawEvent; }
	void send(int status, const char *type, const String &body) {
		responseStatus = status;
		responseType = type;
		responseBody = body;
	}
};