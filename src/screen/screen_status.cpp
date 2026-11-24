#include "./screen.h"

#include <WiFi.h>

#include "../display/display.h"
#include "../init/init.h"
#include "../sd/sd.h"
#include "../touch/touch.h"

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

static String uptimeText() {
  const uint64_t total = infoFS.uptime;
  const uint32_t hours = (uint32_t)(total / 3600ULL);
  const uint32_t minutes = (uint32_t)((total / 60ULL) % 60ULL);
  const uint32_t seconds = (uint32_t)(total % 60ULL);
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu:%02lu:%02lu", (unsigned long)hours,
           (unsigned long)minutes, (unsigned long)seconds);
  return String(buffer);
}

static void value(int x, int y, const String& text, uint16_t color = TFT_WHITE,
                  uint16_t width = 88) {
  tft.fillRect(x, y, width, 12, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(text);
}

static void label(int x, int y, const char* text) {
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(text);
}

static void drawStatic() {
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, tft.width(), 34, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("VUESP LCD");

  tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(236, 12);
  tft.printf("%u.%u.%u", infoFS.firmware[0], infoFS.firmware[1], infoFS.firmware[2]);

  label(8, 44, "Wi-Fi:");
  label(8, 60, "IP:");
  label(8, 76, "RSSI:");
  label(8, 92, "Uptime:");
  label(8, 108, "Heap:");
  label(8, 124, "Min heap:");

  label(168, 44, "Touch:");
  label(168, 60, "Raw:");
  label(168, 76, "Count:");
  label(168, 92, "Driver:");
  label(168, 108, "Size:");
  label(168, 124, "SD:");

  tft.drawFastHLine(0, 150, tft.width(), TFT_DARKGREY);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(8, 160);
  tft.print("Board: ESP32-2432S024C");
  tft.setCursor(8, 176);
  tft.print("Swipe to change screens");
  tft.setCursor(8, 192);
  tft.print("SD card is optional; status updates every 5 s");
}

static void drawValues() {
  const bool wifi = WiFi.status() == WL_CONNECTED;

  if (wifi) {
    value(72, 44, WiFi.SSID(), TFT_GREEN);
    value(72, 60, WiFi.localIP().toString());
    value(72, 76, String(WiFi.RSSI()) + " dBm");
  } else {
    value(72, 44, "--", TFT_ORANGE);
    value(72, 60, "0.0.0.0", TFT_ORANGE);
    value(72, 76, "--");
  }
  value(72, 92, uptimeText());
  value(72, 108, String(ESP.getFreeHeap() / 1024) + " KB");
  value(72, 124, String(ESP.getMinFreeHeap() / 1024) + " KB");

  value(232, 44, touchPressed ? "pressed" : "idle",
        touchPressed ? TFT_GREEN : TFT_LIGHTGREY);
  value(232, 60, String(touchRawX) + "," + String(touchRawY));
  value(232, 76, String(touchCount));
  value(232, 92, driverName());
  value(232, 108, String(tft.width()) + "x" + String(tft.height()));
  value(232, 124,
        sdReady ? String((unsigned long)display.sdUsedMb) + "/" +
                      String((unsigned long)display.sdTotalMb) + " MB"
                : String("no card"),
        sdReady ? TFT_WHITE : TFT_ORANGE, 80);
}

void statusScreenEnter() {
  drawStatic();
  drawValues();
  lastStatus = millis();
}

void statusScreenTick(uint32_t now) {
  if (now - lastStatus < 1000) return;
  lastStatus = now;
  drawValues();
}
