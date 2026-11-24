#include "./touch.h"

#include <Wire.h>

#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_RST 25
#define TOUCH_INT 21
#define TOUCH_ADDRESS 0x15

bool touchPressed = false;
uint16_t touchX = 0;
uint16_t touchY = 0;
uint16_t touchRawX = 0;
uint16_t touchRawY = 0;
uint32_t touchCount = 0;

static uint32_t lastPoll = 0;

static bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TOUCH_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool readRegister(uint8_t reg, uint8_t* data, uint8_t length) {
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    Wire.beginTransmission(TOUCH_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) == 0) {
      const uint8_t received = Wire.requestFrom((uint8_t)TOUCH_ADDRESS, (uint8_t)length);
      if (received == length) {
        for (uint8_t i = 0; i < length; i++) data[i] = Wire.read();
        return true;
      }
    }
    delay(2);
  }
  return false;
}

void setupTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);

  pinMode(TOUCH_INT, OUTPUT);
  digitalWrite(TOUCH_INT, HIGH);
  delay(1);
  digitalWrite(TOUCH_INT, LOW);
  delay(1);

  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(300);

  // The controller has its own interrupt output; leave the line alone after reset.
  pinMode(TOUCH_INT, INPUT);
  writeRegister(0xFE, 0xFF);  // disable automatic sleep
}

void loopTouch(uint32_t now) {
  if (now - lastPoll < 20) return;
  lastPoll = now;

  uint8_t points = 0;
  if (!readRegister(0x02, &points, 1) || points == 0) {
    touchPressed = false;
    return;
  }

  uint8_t data[4];
  if (!readRegister(0x03, data, 4)) {
    touchPressed = false;
    return;
  }

  touchRawX = ((uint16_t)(data[0] & 0x0F) << 8) | data[1];
  touchRawY = ((uint16_t)(data[2] & 0x0F) << 8) | data[3];

  uint16_t nativeX = touchRawX > 240 ? (uint32_t)touchRawX * 240 / 4095 : touchRawX;
  uint16_t nativeY = touchRawY > 320 ? (uint32_t)touchRawY * 320 / 4095 : touchRawY;
  nativeX = nativeX > 239 ? 239 : nativeX;
  nativeY = nativeY > 319 ? 319 : nativeY;

  // Display rotation 1 (landscape 320x240).
  touchX = nativeY;
  touchY = 239 - nativeX;

  if (!touchPressed) touchCount++;
  touchPressed = true;
}
