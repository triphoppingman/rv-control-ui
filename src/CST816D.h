#pragma once

#include <Wire.h>

/**
 * @brief Small driver for the CST816D capacitive touch controller.
 *
 * The driver owns the secondary I2C bus configuration and the controller's
 * reset/interrupt startup sequence. It exposes a compact polling interface
 * that the LVGL input callback can use without knowing controller registers.
 */
class CST816D {
 public:
	/** @brief Store the board pins used by this touch-controller instance. */
  CST816D(int8_t sdaPin, int8_t sclPin, int8_t resetPin, int8_t interruptPin);
	/** @brief Start I2C and reset the CST816D into normal operating state. */
  void begin();
	/** @brief Read the current finger state, coordinates, and reported gesture. */
  bool getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture);

 private:
	/** @brief Read one byte from a CST816D register. */
  uint8_t readRegister(uint8_t address);
	/** @brief Write one byte to a CST816D register. */
  void writeRegister(uint8_t address, uint8_t value);

  // Stored board pins used to initialize the dedicated touch-controller I2C bus.
  int8_t sdaPin_;
  int8_t sclPin_;
  int8_t resetPin_;
  int8_t interruptPin_;
};