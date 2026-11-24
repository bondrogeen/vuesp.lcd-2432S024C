#include "./screen.h"

#include <LittleFS.h>
#include <SD.h>
#include <TJpg_Decoder.h>

#include "../display/display.h"
#include "../sd/sd.h"

static bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

void imageScreenSetup() {
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(jpegOutput);
}

static void drawMessage(const ScreenEntry& entry, const char* text) {
  tft.fillScreen(entry.bg);
  tft.setTextColor(TFT_WHITE, entry.bg);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(text, tft.width() / 2, (tft.height() - 12) / 2);
  tft.setTextDatum(TL_DATUM);
}

bool imageScreenDraw(const ScreenEntry& entry) {
  fs::FS* volume = &LittleFS;
  if (entry.location == SCREEN_LOCATION_SD) {
    if (!sdReady) {
      drawMessage(entry, "SD missing");
      return false;
    }
    volume = &SD;
  }

  if (!entry.image[0] || !volume->exists(entry.image)) {
    drawMessage(entry, "Image not found");
    return false;
  }

  uint16_t width = 0;
  uint16_t height = 0;
  if (TJpgDec.getFsJpgSize(&width, &height, entry.image, *volume) != JDR_OK ||
      !width || !height) {
    drawMessage(entry, "Bad JPEG");
    return false;
  }

  tft.fillScreen(entry.bg);
  const int16_t x = (int16_t)((tft.width() - (int16_t)width) / 2);
  const int16_t y = (int16_t)((tft.height() - (int16_t)height) / 2);

  TJpgDec.setJpgScale(1);
  if (TJpgDec.drawFsJpg(x, y, entry.image, *volume) != JDR_OK) {
    drawMessage(entry, "Decode error");
    return false;
  }
  return true;
}
