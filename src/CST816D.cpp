#include "CST816D.h"

namespace {

constexpr uint8_t kAddress = 0x15;

}  // namespace

/**
 * @brief Store the physical pins used by the board's CST816D controller.
 *
 * No I2C traffic occurs during construction; begin() performs the delayed
 * controller initialization after the ESP32 runtime is ready.
 */
CST816D::CST816D(int8_t sdaPin, int8_t sclPin, int8_t resetPin, int8_t interruptPin)
    : sdaPin_(sdaPin), sclPin_(sclPin), resetPin_(resetPin), interruptPin_(interruptPin) {}

/**
 * @brief Initialize Wire1 and perform the controller startup reset sequence.
 *
 * The pulse sequence is copied from the working demonstration. The final
 * register write selects the controller's normal operating configuration.
 */
void CST816D::begin() {
  Wire1.begin(sdaPin_, sclPin_);
  pinMode(interruptPin_, OUTPUT);
  digitalWrite(interruptPin_, HIGH);
  delay(1);
  digitalWrite(interruptPin_, LOW);
  delay(1);
  pinMode(resetPin_, OUTPUT);
  digitalWrite(resetPin_, LOW);
  delay(10);
  digitalWrite(resetPin_, HIGH);
  delay(300);
  writeRegister(0xFE, 0xFF);
}

/**
 * @brief Read one complete touch sample from the CST816D register map.
 *
 * @param x Receives the horizontal coordinate in display pixels.
 * @param y Receives the vertical coordinate in display pixels.
 * @param gesture Receives the raw controller gesture code for future use.
 * @return True when the controller reports an active finger.
 */
bool CST816D::getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture) {
  const bool fingerDetected = readRegister(0x02) != 0;
  *gesture = readRegister(0x01);
  Wire1.beginTransmission(kAddress);
  Wire1.write(0x03);
  Wire1.endTransmission(false);
  Wire1.requestFrom(kAddress, static_cast<uint8_t>(4));
  uint8_t data[4] = {};
  for (uint8_t index = 0; index < 4 && Wire1.available(); ++index) data[index] = Wire1.read();
  *x = static_cast<uint16_t>((data[0] & 0x0F) << 8) | data[1];
  *y = static_cast<uint16_t>((data[2] & 0x0F) << 8) | data[3];
  return fingerDetected;
}

/**
 * @brief Read one byte using an I2C repeated-start transaction.
 *
 * A failed or empty transfer produces zero, which makes the caller report no
 * touch rather than retaining a stale coordinate from an earlier sample.
 */
uint8_t CST816D::readRegister(uint8_t address) {
  Wire1.beginTransmission(kAddress);
  Wire1.write(address);
  Wire1.endTransmission(false);
  Wire1.requestFrom(kAddress, static_cast<uint8_t>(1));
  return Wire1.available() ? Wire1.read() : 0;
}

/**
 * @brief Write a configuration byte to the CST816D over Wire1.
 *
 * This is used during startup and intentionally remains private because the
 * application should use the driver API rather than controller registers.
 */
void CST816D::writeRegister(uint8_t address, uint8_t value) {
  Wire1.beginTransmission(kAddress);
  Wire1.write(address);
  Wire1.write(value);
  Wire1.endTransmission();
}