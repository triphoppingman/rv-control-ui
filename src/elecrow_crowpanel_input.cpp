#include "elecrow_crowpanel_input.h"

#include <Arduino.h>

#include "CST816D.h"
#include "constants.h"
#include "elecrow_crowpanel_display.h"
#include "ui_controller.h"

namespace {

using namespace rv_control_ui::constants;

/** @brief The board touch peripheral used exclusively by the input controller. */
CST816D touch(kTouchSdaPin, kTouchSclPin, kTouchResetPin, kTouchInterruptPin);

}  // namespace

ElecrowCrowPanelInput &ElecrowCrowPanelInput::instance() {
	static ElecrowCrowPanelInput input;
	return input;
}

void ElecrowCrowPanelInput::begin() {
	pinMode(kEncoderAPin, INPUT);
	pinMode(kEncoderBPin, INPUT);
	pinMode(kEncoderButtonPin, INPUT_PULLUP);
	attachInterruptArg(digitalPinToInterrupt(kEncoderButtonPin), encoderButtonInterrupt, this, CHANGE);

	touch.begin();
	lv_indev_t *touchInput = lv_indev_create();
	lv_indev_set_type(touchInput, LV_INDEV_TYPE_POINTER);
	lv_indev_set_user_data(touchInput, this);
	lv_indev_set_read_cb(touchInput, readTouchCallback);
	lv_indev_set_display(touchInput, ElecrowCrowPanelDisplay::instance().lvglDisplay());

	actionQueue_ = xQueueCreate(16, sizeof(InputAction));
	xTaskCreatePinnedToCore(encoderTaskEntry, "encoder", 2048, this, 1, nullptr, 0);
}

bool ElecrowCrowPanelInput::readAction(InputAction &action) {
	return actionQueue_ && xQueueReceive(actionQueue_, &action, 0) == pdTRUE;
}

void IRAM_ATTR ElecrowCrowPanelInput::encoderButtonInterrupt(void *parameter) {
	auto *input = static_cast<ElecrowCrowPanelInput *>(parameter);
	static unsigned long lastInterruptTime = 0;
	const unsigned long now = millis();
	if (now - lastInterruptTime > kDebounceMilliseconds && !digitalRead(kEncoderButtonPin)) {
		input->lastPressTime_ = now;
		++input->clickCount_;
	}
	lastInterruptTime = now;
}

void ElecrowCrowPanelInput::encoderTaskEntry(void *parameter) {
	static_cast<ElecrowCrowPanelInput *>(parameter)->runEncoderTask();
}

void ElecrowCrowPanelInput::runEncoderTask() {
	lastEncoderClockState_ = digitalRead(kEncoderAPin);
	while (true) {
		const int currentClockState = digitalRead(kEncoderAPin);
		if (currentClockState != lastEncoderClockState_ && currentClockState == HIGH && actionQueue_) {
			const InputAction action = {digitalRead(kEncoderBPin) != currentClockState ? InputActionType::RotateClockwise
																																																																			 : InputActionType::RotateCounterclockwise};
			xQueueSend(actionQueue_, &action, 0);
		}
		lastEncoderClockState_ = currentClockState;

		if (clickCount_ >= 2 && actionQueue_) {
			const InputAction action = {InputActionType::DoubleClick};
			clickCount_ = 0;
			xQueueSend(actionQueue_, &action, 0);
		} else if (clickCount_ == 1 && millis() - lastPressTime_ > kDoubleClickMilliseconds && actionQueue_) {
			const InputAction action = {InputActionType::Click};
			clickCount_ = 0;
			xQueueSend(actionQueue_, &action, 0);
		}
		vTaskDelay(pdMS_TO_TICKS(2));
	}
}

void ElecrowCrowPanelInput::readTouchCallback(lv_indev_t *input, lv_indev_data_t *data) {
	auto *boardInput = static_cast<ElecrowCrowPanelInput *>(lv_indev_get_user_data(input));
	boardInput->readTouch(data);
}

void ElecrowCrowPanelInput::readTouch(lv_indev_data_t *data) {
	uint8_t gesture = 0;
	uint16_t touchX = 0;
	uint16_t touchY = 0;
	if (!touch.getTouch(&touchX, &touchY, &gesture)) {
		data->state = LV_INDEV_STATE_REL;
		return;
	}
	UiController::instance().recordUserActivity();
	data->state = LV_INDEV_STATE_PR;
	data->point.x = touchX;
	data->point.y = touchY;
}