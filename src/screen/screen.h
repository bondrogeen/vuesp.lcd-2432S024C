#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

#include "../const/protocol.h"

#define SCREEN_MAX 8
#define SCREEN_LABEL_MAX 8
#define SCREEN_NAME_LEN 20
#define SCREEN_PATH_LEN 64
#define SCREEN_TEXT_LEN 64
#define SCREEN_CONFIG_MAX 6144
#define SCREEN_CONFIG_PATH "/screens.cfg"

enum screenType : uint8_t {
  SCREEN_STATUS = 0,
  SCREEN_IMAGE = 1,
  SCREEN_DATA = 2
};

enum screenLocation : uint8_t {
  SCREEN_LOCATION_FS = 0,
  SCREEN_LOCATION_SD = 1
};

enum screenAlign : uint8_t {
  SCREEN_ALIGN_LEFT = 0,
  SCREEN_ALIGN_CENTER = 1,
  SCREEN_ALIGN_RIGHT = 2
};

struct ScreenLabel {
  uint8_t slot;
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  uint8_t size;
  uint16_t color;
  uint8_t align;
  bool overrideActive;
  char text[SCREEN_TEXT_LEN];
  char override[SCREEN_TEXT_LEN];
  char shown[SCREEN_TEXT_LEN];
};

struct ScreenEntry {
  uint8_t type;
  uint8_t location;
  uint16_t bg;
  uint8_t labelCount;
  char name[SCREEN_NAME_LEN];
  char image[SCREEN_PATH_LEN];
  ScreenLabel labels[SCREEN_LABEL_MAX];
};

extern Screen screen;
extern ScreenEntry screenEntries[SCREEN_MAX];
extern uint8_t screenCount;
extern uint8_t screenActive;
extern bool screenSwipeEnabled;

class AsyncWebServer;

void setupScreen();
void loopScreen(uint32_t now);
void screenProcessCommand();
void onWsEventScreen(void* arg, uint8_t* data, size_t len, uint32_t clientId, uint8_t task);
void setupScreenApi(AsyncWebServer& server);

void screenRenderFull();
void screenInvalidate();
void screenShow(uint8_t index, bool save);
void screenNext(int8_t direction);
void screenResetOverrides();
void screenSetValue(uint8_t slot, const char* value);
void screenResetValue(uint8_t slot);
void screenSend();
void screenTelemetry();

bool screenLayoutApply(const char* text, size_t len);
bool screenLayoutSave();
void screenLayoutDefault();
void screenActivate(uint8_t index);
void screenRequestSwipe(bool enabled);
void screenRequestDefault();
void screenSetSwipe(bool enabled);
void screenStateSave();

const char* screenTypeName(uint8_t type);
const char* screenLocationName(uint8_t location);
const char* screenAlignName(uint8_t align);
uint16_t screenColor(const char* value, uint16_t fallback);

// Rendered by screen_status.cpp / screen_image.cpp.
void statusScreenEnter();
void statusScreenTick(uint32_t now);
void imageScreenSetup();
bool imageScreenDraw(const ScreenEntry& entry);

#endif
