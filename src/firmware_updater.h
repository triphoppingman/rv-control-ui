#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>
#include <mbedtls/sha256.h>

/** @brief Network-task-only HMAC authorization and bounded inactive-slot writes. */
class FirmwareUpdater {
public:
	/** @brief Initialize hash state; no sockets or LVGL are accessed. */
	FirmwareUpdater();
	/** @brief Report whether USB-provisioned authentication is enabled. */
	bool enabled() const;
	/** @brief Issue one expiring random challenge without exposing the shared key. */
	void challenge(WebServer &server);
	/** @brief Consume and verify an image/body-bound one-use authorization. */
	bool authorize(WebServer &server, const char *method, const char *path, size_t size, const char *digest);
	/** @brief Stream raw firmware chunks, authorizing before any flash operation. */
	void receive(WebServer &server, bool restartPending);
	/** @brief Finalize only a complete, hashed, target-marked application. */
	bool finish(WebServer &server);
	/** @brief Append public OTA capabilities and pending-image status. */
	void describe(JsonObject object) const;
	/** @brief Return the current upload's bounded HTTP status and safe error. */
	int errorStatus() const { return errorStatus_; }
	const char *error() const { return error_; }
	/** @brief Hash a bounded configuration body or empty authenticated GET body. */
	static void digest(const uint8_t *data, size_t size, char *output);
	/** @brief Encode binary identifiers without exposing key material. */
	static void hex(const uint8_t *data, size_t size, char *output);

private:
	/** @brief Abort inactive-slot writing, retaining a safe error response. */
	void fail(int status, const char *message);
	/** @brief Validate canonical SHA256/HMAC protocol fields. */
	static bool canonicalHex(const String &value);
	/** @brief Decode a provisioned 256-bit hexadecimal key. */
	static void decodeKey(const char *input, uint8_t *output);
	char nonce_[65] = {};
	uint32_t challengeAt_ = 0;
	bool challengeIssued_ = false;
	uint32_t uploadAt_ = 0;
	size_t expectedSize_ = 0;
	size_t receivedSize_ = 0;
	char expectedDigest_[65] = {};
	uint8_t firstHeader_[32] = {};
	size_t markerPosition_ = 0;
	bool markerFound_ = false;
	bool uploading_ = false;
	bool complete_ = false;
	int errorStatus_ = 400;
	const char *error_ = "invalid firmware upload";
	mbedtls_sha256_context hash_;
};