#include "./sd.h"

#include <SD.h>
#include <SPI.h>
#include <sd_diskio.h>
#include <ff.h>
#include <stdlib.h>

#define SD_CS 5
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23

bool sdReady = false;
uint64_t sdTotal = 0;
uint64_t sdUsed = 0;

static uint32_t lastCheck = 0;
static bool spiStarted = false;

static bool mountSD() {
  if (!spiStarted) {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    spiStarted = true;
  }
  if (!SD.begin(SD_CS, SPI, 4000000)) return false;

  sdTotal = SD.totalBytes();
  sdUsed = SD.usedBytes();
  sdReady = true;
  return true;
}

void setupSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  mountSD();
}

void loopSD(uint32_t now) {
  if (now - lastCheck < 30000) return;
  lastCheck = now;

  if (!sdReady) {
    mountSD();
    return;
  }

  sdTotal = SD.totalBytes();
  sdUsed = SD.usedBytes();
}

bool formatSD() {
  SD.end();
  sdReady = false;
  sdTotal = 0;
  sdUsed = 0;

  if (!spiStarted) {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    spiStarted = true;
  }

  const uint8_t pdrv = sdcard_init(SD_CS, &SPI, 4000000);
  if (pdrv == 0xFF) return false;

  BYTE* work = (BYTE*)malloc(FF_MAX_SS);
  if (!work) {
    sdcard_uninit(pdrv);
    return false;
  }

  char drive[3] = {(char)('0' + pdrv), ':', 0};
  const FRESULT result = f_mkfs(drive, FM_ANY, 0, work, FF_MAX_SS);
  free(work);
  sdcard_uninit(pdrv);
  if (result != FR_OK) return false;

  if (!SD.begin(SD_CS, SPI, 4000000, "/sd", 5, false)) return false;
  sdTotal = SD.totalBytes();
  sdUsed = SD.usedBytes();
  sdReady = true;
  return true;
}
