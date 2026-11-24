#ifndef DISPLAY_H
#define DISPLAY_H

#include <TFT_eSPI.h>
#include <stdint.h>

#include "../const/protocol.h"

void setupDisplay();
void loopDisplay(uint32_t now);
void displaySetBrightness(uint8_t value);
void onWsEventDisplay(void* arg, uint8_t* data, size_t len, uint32_t clientId, uint8_t task);

extern Display display;
extern TFT_eSPI tft;

#endif
