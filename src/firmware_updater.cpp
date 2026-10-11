#include "firmware_updater.h"

#include <Update.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/md.h>
#include <string.h>

#include "config_loader.h"
#include "constants.h"

namespace {
using namespace rv_control_ui::constants;
}

FirmwareUpdater::FirmwareUpdater() { mbedtls_sha256_init(&hash_); }

bool FirmwareUpdater::enabled() const { return AppConfig::instance().otaSharedSecret[0] != '\0'; }

void FirmwareUpdater::hex(const uint8_t *data, size_t size, char *output) {
	const char digits[] = "0123456789abcdef";
	for (size_t index = 0; index < size; ++index) {
		output[index * 2] = digits[data[index] >> 4];
		output[index * 2 + 1] = digits[data[index] & 15];
	}
	output[size * 2] = '\0';
}

void FirmwareUpdater::digest(const uint8_t *data, size_t size, char *output) {
	uint8_t result[32];
	mbedtls_sha256_ret(data, size, result, 0);
	hex(result, sizeof(result), output);
}

bool FirmwareUpdater::canonicalHex(const String &value) {
	if (value.length() != 64) return false;
	for (size_t index = 0; index < 64; ++index) {
		if (!((value[index] >= '0' && value[index] <= '9') || (value[index] >= 'a' && value[index] <= 'f'))) return false;
	}
	return true;
}

void FirmwareUpdater::decodeKey(const char *input, uint8_t *output) {
	for (size_t index = 0; index < 32; ++index) {
		unsigned int value = 0;
		sscanf(input + index * 2, "%2x", &value);
		output[index] = static_cast<uint8_t>(value);
	}
}

void FirmwareUpdater::challenge(WebServer &server) {
	if (!enabled()) {
		server.send(403, "application/json", "{\"error\":\"OTA is disabled; provision a key over USB\"}");
		return;
	}
	if (challengeIssued_ && millis() - challengeAt_ < kOtaChallengeIntervalMilliseconds) {
		server.send(429, "application/json", "{\"error\":\"challenge rate limit\"}");
		return;
	}
	uint8_t random[32];
	esp_fill_random(random, sizeof(random));
	hex(random, sizeof(random), nonce_);
	challengeAt_ = millis();
	challengeIssued_ = true;
	JsonDocument document;
	describe(document.to<JsonObject>());
	document["nonce"] = nonce_;
	document["expires_in_ms"] = kOtaChallengeLifetimeMilliseconds;
	String body;
	serializeJson(document, body);
	server.send(200, "application/json", body);
}

bool FirmwareUpdater::authorize(WebServer &server, const char *method, const char *path,
								size_t size, const char *bodyDigest) {
	if (!enabled()) return false;
	const String nonce = server.header("X-OTA-Nonce");
	const bool fresh = nonce_[0] && millis() - challengeAt_ < kOtaChallengeLifetimeMilliseconds && nonce == nonce_;
	if (!fresh) return false;
	nonce_[0] = '\0';
	const String suppliedDigest = server.header("X-OTA-SHA256");
	const String suppliedAuth = server.header("X-OTA-Auth");
	if (server.header("X-OTA-Size") != String(static_cast<unsigned int>(size)) ||
		server.header("X-OTA-Target") != kOtaTarget || !canonicalHex(suppliedDigest) ||
		!canonicalHex(suppliedAuth) || suppliedDigest != bodyDigest) return false;
	char message[320];
	const int length = snprintf(message, sizeof(message), "%s\n%s\n%s\n%s\n%u\n%s\n%s\n",
		kOtaProtocol, method, path, nonce.c_str(), static_cast<unsigned int>(size), bodyDigest, kOtaTarget);
	if (length < 0 || static_cast<size_t>(length) >= sizeof(message)) return false;
	uint8_t key[32];
	uint8_t result[32];
	decodeKey(AppConfig::instance().otaSharedSecret, key);
	const int status = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, sizeof(key),
		 reinterpret_cast<const uint8_t *>(message), length, result);
	memset(key, 0, sizeof(key));
	if (status != 0) return false;
	char expected[65];
	hex(result, sizeof(result), expected);
	volatile uint8_t difference = 0;
	for (size_t index = 0; index < 64; ++index) difference |= expected[index] ^ suppliedAuth[index];
	return difference == 0;
}

void FirmwareUpdater::fail(int status, const char *message) {
	if (uploading_) Update.abort();
	uploading_ = false;
	complete_ = false;
	errorStatus_ = status;
	error_ = message;
}

void FirmwareUpdater::receive(WebServer &server, bool restartPending) {
	HTTPRaw &raw = server.raw();
	if (raw.status == RAW_START) {
		fail(400, "invalid firmware upload");
		receivedSize_ = 0;
		markerPosition_ = 0;
		markerFound_ = false;
		memset(firstHeader_, 0, sizeof(firstHeader_));
		if (restartPending) { fail(409, "restart already pending"); return; }
		const String sizeHeader = server.header("Content-Length");
		char *end = nullptr;
		const unsigned long length = strtoul(sizeHeader.c_str(), &end, 10);
		const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
		if (sizeHeader.isEmpty() || !end || *end || sizeHeader != String(length) || length < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
			fail(400, "invalid firmware length"); return;
		}
		if (!partition || length > partition->size) { fail(413, "image exceeds inactive slot capacity"); return; }
		if (server.header("Content-Type") != "application/octet-stream" || server.header("Transfer-Encoding").length()) {
			fail(400, "expected a fixed-length application binary"); return;
		}
		expectedSize_ = length;
		strlcpy(expectedDigest_, server.header("X-OTA-SHA256").c_str(), sizeof(expectedDigest_));
		if (!authorize(server, "POST", kOtaFirmwarePath, expectedSize_, expectedDigest_)) {
			fail(401, "firmware authorization failed"); return;
		}
		esp_ota_img_states_t state;
		if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
			fail(409, "running firmware is not yet confirmed"); return;
		}
		if (!Update.begin(expectedSize_, U_FLASH)) { fail(500, "could not begin update"); return; }
		if (mbedtls_sha256_starts_ret(&hash_, 0) != 0) { Update.abort(); fail(500, "hash initialization failed"); return; }
		uploading_ = true;
		uploadAt_ = millis();
		Serial.println("[INFO] Authorized firmware upload started");
		return;
	}
	if (!uploading_) return;
	if (raw.status == RAW_ABORTED || millis() - uploadAt_ > kOtaUploadTimeoutMilliseconds) {
		fail(400, "firmware upload interrupted or timed out"); return;
	}
	if (raw.status == RAW_END) {
		complete_ = receivedSize_ == expectedSize_;
		return;
	}
	if (raw.status != RAW_WRITE) return;
	if (!raw.currentSize || raw.currentSize > expectedSize_ - receivedSize_) {
		fail(400, "firmware byte count mismatch"); return;
	}
	for (size_t index = 0; index < raw.currentSize; ++index) {
		const uint8_t byte = raw.buf[index];
		if (receivedSize_ + index < sizeof(firstHeader_)) firstHeader_[receivedSize_ + index] = byte;
		if (!markerFound_) {
			if (byte == static_cast<uint8_t>(kOtaTargetMarker[markerPosition_])) {
				if (++markerPosition_ == sizeof(kOtaTargetMarker)) markerFound_ = true;
			} else markerPosition_ = byte == static_cast<uint8_t>(kOtaTargetMarker[0]) ? 1 : 0;
		}
	}
	if (receivedSize_ + raw.currentSize >= sizeof(firstHeader_) &&
		(firstHeader_[0] != ESP_IMAGE_HEADER_MAGIC || firstHeader_[12] != 9 || firstHeader_[13] != 0)) {
		fail(400, "image is not an ESP32-S3 application"); return;
	}
	if (mbedtls_sha256_update_ret(&hash_, raw.buf, raw.currentSize) != 0 || Update.write(raw.buf, raw.currentSize) != raw.currentSize) {
		fail(500, "firmware flash write failed"); return;
	}
	receivedSize_ += raw.currentSize;
	yield();
}

bool FirmwareUpdater::finish(WebServer &server) {
	if (!uploading_) return false;
	if (!complete_ || !markerFound_) { fail(400, "incomplete or wrong-target firmware"); return false; }
	uint8_t result[32];
	char actual[65];
	if (mbedtls_sha256_finish_ret(&hash_, result) != 0) { fail(500, "firmware hash failed"); return false; }
	hex(result, sizeof(result), actual);
	if (strcmp(actual, expectedDigest_) != 0) { fail(400, "firmware SHA256 mismatch"); return false; }
	if (!Update.end(false)) { fail(500, "application validation or activation failed"); return false; }
	uploading_ = false;
	complete_ = false;
	Serial.printf("[INFO] Firmware upload verified: %u bytes; restart pending\n", static_cast<unsigned int>(receivedSize_));
	return true;
}

void FirmwareUpdater::describe(JsonObject object) const {
	const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
	object["enabled"] = enabled();
	object["target"] = kOtaTarget;
	object["capacity"] = partition ? partition->size : 0;
	object["active_slot"] = esp_ota_get_running_partition()->label;
	esp_ota_img_states_t state;
	const bool managed = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK;
	object["state"] = managed && state == ESP_OTA_IMG_PENDING_VERIFY ? "pending" :
		managed && state == ESP_OTA_IMG_VALID ? "valid" : "unmanaged";
}