#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class String {
public:
	String() = default;
	String(const char *text) : text_(text ? text : "") {}
	String(const std::string &text) : text_(text) {}
	String(unsigned int value) : text_(std::to_string(value)) {}
	String(unsigned long value) : text_(std::to_string(value)) {}
	const char *c_str() const { return text_.c_str(); }
	size_t length() const { return text_.size(); }
	bool isEmpty() const { return text_.empty(); }
	char operator[](size_t index) const { return text_[index]; }
	bool operator==(const String &other) const { return text_ == other.text_; }
	bool operator!=(const String &other) const { return !(*this == other); }
	String &operator+=(char value) { text_ += value; return *this; }
	String &operator+=(const char *value) { text_ += value; return *this; }
	bool concat(const char *value, size_t size) { text_.append(value, size); return true; }
	bool concat(const char *value) { text_ += value; return true; }
private:
	std::string text_;
};

inline size_t strlcpy(char *destination, const char *source, size_t size) {
	const size_t length = strlen(source);
	if (size) {
		const size_t copied = std::min(length, size - 1);
		memcpy(destination, source, copied);
		destination[copied] = '\0';
	}
	return length;
}

inline uint32_t fakeMillis = 0;
inline uint32_t millis() { return fakeMillis; }
inline void yield() {}
struct SerialStub {
	void println(const char *) {}
	template <typename... Args> void printf(const char *, Args...) {}
};
inline SerialStub Serial;