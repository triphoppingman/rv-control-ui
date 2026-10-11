#pragma once
#include <cstdint>
constexpr int ESP_OK = 0;
enum esp_ota_img_states_t { ESP_OTA_IMG_VALID, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_UNDEFINED };
struct esp_partition_t { uint32_t size; const char *label; };
inline esp_partition_t inactivePartition{4096, "ota_1"};
inline esp_partition_t runningPartition{4096, "ota_0"};
inline bool partitionAvailable = true;
inline int partitionStateResult = ESP_OK;
inline esp_ota_img_states_t partitionState = ESP_OTA_IMG_VALID;
inline const esp_partition_t *esp_ota_get_next_update_partition(const void *) {
	return partitionAvailable ? &inactivePartition : nullptr;
}
inline const esp_partition_t *esp_ota_get_running_partition() { return &runningPartition; }
inline int esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *state) {
	*state = partitionState;
	return partitionStateResult;
}