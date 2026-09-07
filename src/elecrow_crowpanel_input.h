#pragma once

#include <Arduino.h>
#include <lvgl.h>

/** @brief High-level rotary actions safely queued for Arduino's LVGL loop. */
enum class InputActionType : int8_t { RotateClockwise, RotateCounterclockwise, Click, DoubleClick };

/** @brief One queued rotary encoder movement or button action. */
struct InputAction {
	InputActionType type;
};

/**
 * @brief Owns the Elecrow CrowPanel touch controller and rotary encoder.
 *
 * The encoder runs in a dedicated FreeRTOS task and publishes bounded input
 * actions to Arduino's loop thread. Touch samples are delivered to LVGL on its
 * own loop-thread input pass. Neither path changes LVGL objects directly.
 */
class ElecrowCrowPanelInput {
 public:
	/** @brief Return the one board input device present in this firmware. */
	static ElecrowCrowPanelInput &instance();

	/** @brief Initialize touch, register it with the board display, and start the encoder task. */
	void begin();

	/** @brief Copy the next queued encoder action, returning false when none are pending. */
	bool readAction(InputAction &action);

 private:
	/** @brief Construct the singleton; use instance() to access the board input. */
	ElecrowCrowPanelInput() = default;

	/** @brief Record a debounced encoder-button edge with minimal interrupt work. */
	static void IRAM_ATTR encoderButtonInterrupt(void *parameter);

	/** @brief Enter the encoder task using the board-input instance supplied by FreeRTOS. */
	static void encoderTaskEntry(void *parameter);

	/** @brief Sample quadrature state and queue click or rotation actions without touching LVGL. */
	void runEncoderTask();

	/** @brief Adapt LVGL's touch callback to the board-input instance stored as user data. */
	static void readTouchCallback(lv_indev_t *input, lv_indev_data_t *data);

	/** @brief Translate one CST816D sample into LVGL pointer-input data. */
	void readTouch(lv_indev_data_t *data);

	QueueHandle_t actionQueue_ = nullptr;
	volatile unsigned long lastPressTime_ = 0;
	volatile uint8_t clickCount_ = 0;
	int lastEncoderClockState_ = LOW;
};