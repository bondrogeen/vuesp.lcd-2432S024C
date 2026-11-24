#include "./display.h"

#include <string.h>

#include "../config/ConfigStore.h"
#include "../const/index.h"
#include "../init/init.h"
#include "../screen/screen.h"
#include "../sd/sd.h"
#include "../tasks/tasks.h"
#include "../touch/touch.h"
#include "../webserver/webserver.h"

TFT_eSPI tft;
Display display = {KEY_DISPLAY};

static uint32_t lastStatus = 0;

static const char* driverName() {
#if defined(ILI9341_DRIVER)
  return "ILI9341";
#elif defined(ST7789_DRIVER)
  return "ST7789";
#else
  return "TFT";
#endif
}

static void saveConfig() {
  const TConfigStatus status =
      configStore.write("display", &display, sizeof(display), CONFIG_DISPLAY_VERSION);
#if VUESP_LOG_LEVEL >= 1
  if (status != TConfigStatus::Ok) {
    Serial.printf("[config] display write: %d\n", (int)status);
  }
#endif
}

static void updateStatus() {
  display.status = display.brightness ? 1 : 0;
  display.width = tft.width();
  display.height = tft.height();
  display.touchPressed = touchPressed ? 1 : 0;
  display.touchX = touchX;
  display.touchY = touchY;
  display.touchRawX = touchRawX;
  display.touchRawY = touchRawY;
  display.touchCount = touchCount;
  display.sdReady = sdReady ? 1 : 0;
  display.sdTotalMb = (uint32_t)(sdTotal / (1024ULL * 1024ULL));
  display.sdUsedMb = (uint32_t)(sdUsed / (1024ULL * 1024ULL));
  display.driver = driverName()[0] == 'S' ? 2 : 1;
  strncpy(display.driverName, driverName(), sizeof(display.driverName) - 1);
  display.driverName[sizeof(display.driverName) - 1] = '\0';
}

static void displaySend() {
  if (wsClientCount() == 0) return;
  wsSendAll((uint8_t*)&display, sizeof(display));
}

static void applyConfig() {
  if (display.rotation != 1 && display.rotation != 3) display.rotation = 1;
  displaySetBrightness(display.brightness);
  tft.setRotation(display.rotation);
  updateStatus();
  screenInvalidate();
}

void displaySetBrightness(uint8_t value) {
  display.brightness = value;
  display.status = value ? 1 : 0;
  analogWrite(TFT_BL, value);
}

void onWsEventDisplay(void* arg, uint8_t* data, size_t len, uint32_t clientId, uint8_t task) {
  (void)clientId;
  if (!arg || !data || task != KEY_DISPLAY) return;
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (info->len != sizeof(display) || len < sizeof(display)) return;

  const uint8_t command = data[1];
  if (command == CMD_SET) {
    display.brightness = data[4];
    display.rotation = data[3];
  }
  display.command = command;
  tasks[KEY_DISPLAY] = 1;
}

void setupDisplay() {
  // The onboard RGB LED is common-anode; keep all channels high (off).
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  pinMode(16, OUTPUT);
  digitalWrite(16, HIGH);
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  // JPEG blocks are decoded into RGB565 and pushed through pushImage().
  tft.setSwapBytes(true);
  display.brightness = 255;
  display.rotation = 1;
  display.status = 1;

  const TConfigStatus status =
      configStore.read("display", &display, sizeof(display), CONFIG_DISPLAY_VERSION);
  if (status != TConfigStatus::Ok) {
    display.brightness = 255;
    display.rotation = 1;
  }
  if (display.rotation != 1 && display.rotation != 3) display.rotation = 1;

  tft.setRotation(display.rotation);
  displaySetBrightness(display.brightness);
  updateStatus();
}

void loopDisplay(uint32_t now) {
  if (tasks[KEY_DISPLAY]) {
    tasks[KEY_DISPLAY] = 0;
    const uint8_t command = display.command;
    if (command == CMD_SET) {
      applyConfig();
      saveConfig();
    } else {
      updateStatus();
    }
    displaySend();
  }

  if (now - lastStatus < 1000) return;
  lastStatus = now;

  updateStatus();
  displaySend();
}
