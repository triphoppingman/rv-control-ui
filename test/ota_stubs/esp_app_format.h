#pragma once
#include <cstdint>
constexpr uint8_t ESP_IMAGE_HEADER_MAGIC = 0xe9;
struct esp_image_header_t { uint8_t bytes[24]; };
struct esp_image_segment_header_t { uint8_t bytes[8]; };
struct esp_app_desc_t { uint8_t bytes[256]; };
static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t) == 288);