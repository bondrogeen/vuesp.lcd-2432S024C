#include "./screen.h"

#include <LittleFS.h>
#include <SD.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config/ConfigStore.h"
#include "../display/display.h"
#include "../gpio/gpio.h"
#include "../runner/runner.h"
#include "../sd/sd.h"
#include "../touch/touch.h"
#include "../webserver/webserver.h"

Screen screen = {KEY_SCREEN};
ScreenEntry screenEntries[SCREEN_MAX];
uint8_t screenCount = 0;
uint8_t screenActive = 0;
bool screenSwipeEnabled = true;

struct ScreenState {
  uint8_t active;
  uint8_t swipe;
};

static ScreenState screenState = {0, 1};
static uint32_t screenSwitchCount = 0;
static uint32_t lastLabelTick = 0;
static uint32_t lastGestureMs = 0;
static bool screenInitialised = false;
static bool fullRedraw = false;
static int8_t pendingShow = -1;
static bool pendingDefault = false;
static char* pendingConfig = nullptr;
static int8_t pendingSwipe = -1;

static bool wasPressed = false;
static uint16_t pressX = 0;
static uint16_t pressY = 0;
static uint32_t pressMs = 0;

static const char* typeName(uint8_t type) {
  switch (type) {
    case SCREEN_IMAGE:
      return "image";
    case SCREEN_DATA:
      return "data";
    default:
      return "status";
  }
}

const char* screenTypeName(uint8_t type) { return typeName(type); }

const char* screenLocationName(uint8_t location) {
  return location == SCREEN_LOCATION_SD ? "sd" : "lfs";
}

const char* screenAlignName(uint8_t align) {
  switch (align) {
    case SCREEN_ALIGN_CENTER:
      return "center";
    case SCREEN_ALIGN_RIGHT:
      return "right";
    default:
      return "left";
  }
}

static int8_t hexDigit(char value) {
  if (value >= '0' && value <= '9') return (int8_t)(value - '0');
  if (value >= 'a' && value <= 'f') return (int8_t)(value - 'a' + 10);
  if (value >= 'A' && value <= 'F') return (int8_t)(value - 'A' + 10);
  return -1;
}

uint16_t screenColor(const char* value, uint16_t fallback) {
  if (!value || value[0] != '#') return fallback;

  char digits[7];
  size_t length = strlen(value + 1);
  if (length == 3) {
    digits[0] = value[1];
    digits[1] = value[1];
    digits[2] = value[2];
    digits[3] = value[2];
    digits[4] = value[3];
    digits[5] = value[3];
    digits[6] = 0;
  } else if (length >= 6) {
    memcpy(digits, value + 1, 6);
    digits[6] = 0;
  } else {
    return fallback;
  }

  for (uint8_t i = 0; i < 6; i++) {
    if (hexDigit(digits[i]) < 0) return fallback;
  }

  uint8_t rgb[3];
  for (uint8_t i = 0; i < 3; i++) {
    rgb[i] = (uint8_t)((hexDigit(digits[i * 2]) << 4) | hexDigit(digits[i * 2 + 1]));
  }
  return tft.color565(rgb[0], rgb[1], rgb[2]);
}

static void copyText(char* dst, size_t size, const char* src) {
  if (!dst || !size) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i + 1 < size && src[i]; i++) {
    const char c = src[i];
    dst[i] = (c == '|' || c == '\r' || c == '\n') ? ' ' : c;
  }
  dst[i] = '\0';
}

static void screenResetEntry(ScreenEntry& entry) {
  memset(&entry, 0, sizeof(entry));
  entry.type = SCREEN_STATUS;
  entry.bg = TFT_BLACK;
}

void screenResetOverrides() {
  for (uint8_t s = 0; s < screenCount; s++) {
    ScreenEntry& entry = screenEntries[s];
    for (uint8_t i = 0; i < entry.labelCount; i++) {
      entry.labels[i].overrideActive = false;
      entry.labels[i].override[0] = '\0';
      entry.labels[i].shown[0] = '\0';
    }
  }
}

void screenLayoutDefault() {
  for (uint8_t i = 0; i < SCREEN_MAX; i++) screenResetEntry(screenEntries[i]);
  screenCount = 1;
  copyText(screenEntries[0].name, sizeof(screenEntries[0].name), "Status");
  screenEntries[0].type = SCREEN_STATUS;
  screenEntries[0].bg = TFT_BLACK;
}

static ScreenEntry* entryById(uint8_t id) {
  if (id >= SCREEN_MAX) return nullptr;
  return &screenEntries[id];
}

static void parseScreen(char* value) {
  char* cursor = value;
  char* idText = cursor;
  char* sep = strchr(cursor, '|');
  if (!sep) return;
  *sep = '\0';
  cursor = sep + 1;

  const int id = atoi(idText);
  if (id < 0 || id >= SCREEN_MAX) return;

  char* name = cursor;
  sep = strchr(cursor, '|');
  if (!sep) return;
  *sep = '\0';
  cursor = sep + 1;

  char* type = cursor;
  sep = strchr(cursor, '|');
  if (sep) {
    *sep = '\0';
    cursor = sep + 1;
  } else {
    cursor = nullptr;
  }

  ScreenEntry& entry = screenEntries[id];
  screenResetEntry(entry);
  copyText(entry.name, sizeof(entry.name), name);

  if (strcmp(type, "image") == 0) {
    entry.type = SCREEN_IMAGE;
    char* location = cursor;
    sep = cursor ? strchr(cursor, '|') : nullptr;
    if (!sep) {
      screenResetEntry(entry);
      return;
    }
    *sep = '\0';
    cursor = sep + 1;

    char* file = cursor;
    sep = strchr(cursor, '|');
    if (!sep) {
      screenResetEntry(entry);
      return;
    }
    *sep = '\0';
    cursor = sep + 1;

    entry.location = strcmp(location, "sd") == 0 ? SCREEN_LOCATION_SD : SCREEN_LOCATION_FS;
    copyText(entry.image, sizeof(entry.image), file);
    entry.bg = screenColor(cursor, TFT_BLACK);
  } else if (strcmp(type, "data") == 0) {
    entry.type = SCREEN_DATA;
    entry.bg = screenColor(cursor, TFT_BLACK);
  } else {
    entry.type = SCREEN_STATUS;
    entry.bg = TFT_BLACK;
  }

  if (entry.name[0] == '\0') copyText(entry.name, sizeof(entry.name), "Screen");
  if ((uint8_t)(id + 1) > screenCount) screenCount = (uint8_t)(id + 1);
}

static void parseLabel(char* value) {
  char* parts[10];
  uint8_t count = 0;
  char* cursor = value;
  while (cursor && count < 10) {
    parts[count++] = cursor;
    char* sep = strchr(cursor, '|');
    if (!sep) break;
    *sep = '\0';
    cursor = sep + 1;
  }
  if (count < 10) return;

  const int screenId = atoi(parts[0]);
  if (screenId < 0 || screenId >= SCREEN_MAX) return;

  ScreenEntry& entry = screenEntries[screenId];
  if (entry.labelCount >= SCREEN_LABEL_MAX) return;

  ScreenLabel& label = entry.labels[entry.labelCount];
  memset(&label, 0, sizeof(label));

  label.slot = (uint8_t)atoi(parts[1]);
  label.x = (int16_t)atoi(parts[2]);
  label.y = (int16_t)atoi(parts[3]);
  label.w = (int16_t)atoi(parts[4]);
  label.h = (int16_t)atoi(parts[5]);
  label.size = (uint8_t)atoi(parts[6]);
  label.color = screenColor(parts[7], TFT_WHITE);

  if (strcmp(parts[8], "center") == 0) label.align = SCREEN_ALIGN_CENTER;
  else if (strcmp(parts[8], "right") == 0) label.align = SCREEN_ALIGN_RIGHT;
  else label.align = SCREEN_ALIGN_LEFT;

  copyText(label.text, sizeof(label.text), parts[9]);

  if (label.slot >= SCREEN_LABEL_MAX) label.slot = entry.labelCount;
  if (label.size < 1) label.size = 1;
  if (label.size > 4) label.size = 4;
  if (label.w < 1) label.w = 1;
  if (label.h < 1) label.h = 1;
  if (label.x < 0) label.x = 0;
  if (label.y < 0) label.y = 0;
  if (label.x + label.w > 320) label.w = (int16_t)(320 - label.x);
  if (label.y + label.h > 240) label.h = (int16_t)(240 - label.y);
  if (label.w < 1 || label.h < 1) return;

  entry.labelCount++;
}

static bool parseLayout(char* buffer) {
  for (uint8_t i = 0; i < SCREEN_MAX; i++) screenResetEntry(screenEntries[i]);
  screenCount = 0;

  char* save = nullptr;
  char* line = strtok_r(buffer, "\n", &save);
  while (line) {
    const size_t length = strlen(line);
    if (length && line[length - 1] == '\r') line[length - 1] = '\0';
    if (line[0] && line[0] != '#') {
      if (strncmp(line, "screen=", 7) == 0) {
        parseScreen(line + 7);
      } else if (strncmp(line, "label=", 6) == 0) {
        parseLabel(line + 6);
      }
    }
    line = strtok_r(nullptr, "\n", &save);
  }

  if (screenCount == 0) {
    screenLayoutDefault();
    return false;
  }

  for (uint8_t i = 0; i < screenCount; i++) {
    if (screenEntries[i].name[0] == '\0') {
      copyText(screenEntries[i].name, sizeof(screenEntries[i].name), "Screen");
    }
  }
  return true;
}

static void colorText(uint16_t color, char* out, size_t size) {
  const uint8_t r = (uint8_t)(((color >> 11) & 0x1F) * 255 / 31);
  const uint8_t g = (uint8_t)(((color >> 5) & 0x3F) * 255 / 63);
  const uint8_t b = (uint8_t)((color & 0x1F) * 255 / 31);
  snprintf(out, size, "#%02X%02X%02X", r, g, b);
}

bool screenLayoutSave() {
  File file = LittleFS.open(SCREEN_CONFIG_PATH, "w");
  if (!file) return false;

  file.print("V1\n");
  for (uint8_t i = 0; i < screenCount; i++) {
    const ScreenEntry& entry = screenEntries[i];
    char background[8];
    colorText(entry.bg, background, sizeof(background));
    if (entry.type == SCREEN_IMAGE) {
      file.printf("screen=%u|%s|image|%s|%s|%s\n", i, entry.name,
                  screenLocationName(entry.location), entry.image, background);
    } else if (entry.type == SCREEN_DATA) {
      file.printf("screen=%u|%s|data|%s\n", i, entry.name, background);
    } else {
      file.printf("screen=%u|%s|status\n", i, entry.name);
    }

    for (uint8_t l = 0; l < entry.labelCount; l++) {
      const ScreenLabel& label = entry.labels[l];
      char labelColor[8];
      colorText(label.color, labelColor, sizeof(labelColor));
      file.printf("label=%u|%u|%d|%d|%d|%d|%u|%s|%s|%s\n", i, label.slot, label.x,
                  label.y, label.w, label.h, label.size, labelColor,
                  screenAlignName(label.align), label.text);
    }
  }
  file.close();
  return true;
}

static bool loadState() {
  const TConfigStatus status =
      configStore.read("screen", &screenState, sizeof(screenState), CONFIG_SCREEN_VERSION);
  if (status != TConfigStatus::Ok) {
    screenState.active = 0;
    screenState.swipe = 1;
  }
  if (screenState.swipe > 1) screenState.swipe = 1;
  return status == TConfigStatus::Ok;
}

void screenStateSave() {
  screenState.active = screenActive;
  screenState.swipe = screenSwipeEnabled ? 1 : 0;
  configStore.write("screen", &screenState, sizeof(screenState), CONFIG_SCREEN_VERSION);
}

static bool loadLayout() {
  if (!LittleFS.exists(SCREEN_CONFIG_PATH)) {
    screenLayoutDefault();
    screenLayoutSave();
    return true;
  }

  File file = LittleFS.open(SCREEN_CONFIG_PATH, "r");
  if (!file) {
    screenLayoutDefault();
    return false;
  }

  const size_t size = file.size();
  if (!size || size > SCREEN_CONFIG_MAX) {
    file.close();
    screenLayoutDefault();
    return false;
  }

  char* buffer = (char*)malloc(size + 1);
  if (!buffer) {
    file.close();
    screenLayoutDefault();
    return false;
  }

  const size_t read = file.readBytes(buffer, size);
  file.close();
  buffer[read] = '\0';

  const bool ok = parseLayout(buffer);
  free(buffer);
  if (!ok) screenLayoutSave();
  return ok;
}

bool screenLayoutApply(const char* text, size_t len) {
  if (!text || !len || len >= SCREEN_CONFIG_MAX) return false;

  char* copy = (char*)malloc(len + 1);
  if (!copy) return false;
  memcpy(copy, text, len);
  copy[len] = '\0';

  char* previous = pendingConfig;
  pendingConfig = copy;
  free(previous);
  return true;
}

void screenRequestSwipe(bool enabled) { pendingSwipe = enabled ? 1 : 0; }

void screenRequestDefault() { pendingDefault = true; }

void screenInvalidate() { fullRedraw = true; }

void screenTelemetry() {
  screen.status = screenInitialised ? 1 : 0;
  screen.active = screenActive;
  screen.count = screenCount;
  screen.swipe = screenSwipeEnabled ? 1 : 0;
  screen.busy = 0;
  screen.width = tft.width();
  screen.height = tft.height();
  screen.switchCount = screenSwitchCount;
  screen.fsFree = (uint32_t)(LittleFS.totalBytes() - LittleFS.usedBytes());
  screen.sdFree = sdReady ? (uint32_t)(SD.totalBytes() - SD.usedBytes()) : 0;
  memset(screen.name, 0, sizeof(screen.name));
  if (screenCount) {
    strncpy(screen.name, screenEntries[screenActive].name, sizeof(screen.name) - 1);
  }
}

void screenSend() {
  if (wsClientCount() == 0) return;
  screenTelemetry();
  wsSendAll((uint8_t*)&screen, sizeof(screen));
}

static void drawDots(uint16_t background) {
  if (screenCount < 2) return;

  const int16_t height = 12;
  const int16_t y = tft.height() - height;
  tft.fillRect(0, y, tft.width(), height, background);

  const int16_t spacing = 14;
  const int16_t total = (int16_t)((screenCount - 1) * spacing);
  const int16_t x0 = (int16_t)((tft.width() - total) / 2);
  for (uint8_t i = 0; i < screenCount; i++) {
    tft.fillCircle(x0 + i * spacing, y + height / 2, i == screenActive ? 3 : 2,
                   i == screenActive ? TFT_WHITE : TFT_DARKGREY);
  }
}

static void append(char* out, size_t size, size_t& pos, const char* text) {
  while (text && *text && pos + 1 < size) out[pos++] = *text++;
  out[pos] = '\0';
}

static bool tokenValue(const char* token, size_t length, char* out, size_t size) {
  if (length < 2) return false;
  const char kind = token[0];
  if (kind != 'u' && kind != 'i' && kind != 'f' && kind != 's' && kind != 'p') return false;

  uint16_t index = 0;
  uint8_t decimals = 2;
  size_t offset = 1;
  const size_t maxDigits = kind == 'p' ? 2 : 1;
  while (offset < length && (size_t)(offset - 1) < maxDigits &&
         isdigit((unsigned char)token[offset])) {
    index = (uint16_t)(index * 10 + (token[offset] - '0'));
    offset++;
  }
  if (offset == 1) return false;
  if (index > 255) return false;
  if (offset < length && token[offset] == ':' && kind == 'f') {
    if (offset + 1 < length && isdigit((unsigned char)token[offset + 1])) {
      decimals = (uint8_t)(token[offset + 1] - '0');
    }
  }

  switch (kind) {
    case 'u':
      snprintf(out, size, "%lu", (unsigned long)scriptRunner.getUintVar(index));
      break;
    case 'i':
      snprintf(out, size, "%ld", (long)scriptRunner.getIntVar(index));
      break;
    case 'f': {
      char format[8];
      snprintf(format, sizeof(format), "%%.%uf", (unsigned)decimals);
      snprintf(out, size, format, scriptRunner.getFloatVar(index));
      break;
    }
    case 's':
      snprintf(out, size, "%s", scriptRunner.getStringVar(index));
      break;
    case 'p': {
      uint16_t portValue = 0;
      getValue(index, portValue);
      snprintf(out, size, "%u", (unsigned)portValue);
      break;
    }
    default:
      return false;
  }

  return true;
}

static void renderTemplate(const char* input, char* output, size_t size) {
  size_t pos = 0;
  output[0] = '\0';

  for (size_t i = 0; input && input[i] && pos + 1 < size;) {
    const unsigned char c = (unsigned char)input[i];
    if (c == '{') {
      const char* end = strchr(input + i + 1, '}');
      if (end) {
        const size_t length = (size_t)(end - (input + i + 1));
        char token[16];
        if (length < sizeof(token)) {
          memcpy(token, input + i + 1, length);
          token[length] = '\0';
          char value[24];
          if (tokenValue(token, length, value, sizeof(value))) {
            append(output, size, pos, value);
            i = (size_t)(end - input) + 1;
            continue;
          }
        }
      }
    }

    if (c < 0x80) {
      if (c != '\r' && c != '\n') output[pos++] = (char)c;
      i++;
    } else {
      // TFT_eSPI built-in fonts are ASCII only: collapse a UTF-8 code point
      // into one placeholder instead of printing its raw bytes.
      i++;
      while (((unsigned char)input[i] & 0xC0) == 0x80) i++;
      output[pos++] = '?';
    }
    output[pos] = '\0';
  }
}

static void drawLabel(ScreenEntry& entry, ScreenLabel& label, bool force) {
  char rendered[SCREEN_TEXT_LEN];
  if (label.overrideActive) {
    strncpy(rendered, label.override, sizeof(rendered) - 1);
    rendered[sizeof(rendered) - 1] = '\0';
  } else {
    renderTemplate(label.text, rendered, sizeof(rendered));
  }

  if (!force && strcmp(rendered, label.shown) == 0) return;
  strncpy(label.shown, rendered, sizeof(label.shown) - 1);
  label.shown[sizeof(label.shown) - 1] = '\0';

  tft.fillRect(label.x, label.y, label.w, label.h, entry.bg);

  const uint8_t size = label.size ? label.size : 1;
  tft.setTextSize(size);
  tft.setTextColor(label.color, entry.bg);

  char clipped[SCREEN_TEXT_LEN];
  strncpy(clipped, rendered, sizeof(clipped) - 1);
  clipped[sizeof(clipped) - 1] = '\0';
  while (strlen(clipped) > 1 && tft.textWidth(clipped) > label.w) {
    clipped[strlen(clipped) - 1] = '\0';
  }
  if (strcmp(clipped, rendered) != 0 && strlen(clipped) + 2 < sizeof(clipped)) {
    strcat(clipped, "..");
  }

  int16_t x = label.x;
  uint8_t datum = TL_DATUM;
  if (label.align == SCREEN_ALIGN_CENTER) {
    x = (int16_t)(label.x + label.w / 2);
    datum = TC_DATUM;
  } else if (label.align == SCREEN_ALIGN_RIGHT) {
    x = (int16_t)(label.x + label.w);
    datum = TR_DATUM;
  }
  tft.setTextDatum(datum);
  tft.drawString(clipped, x, label.y);
}

static void drawLabels(ScreenEntry& entry, bool force) {
  for (uint8_t i = 0; i < entry.labelCount; i++) {
    drawLabel(entry, entry.labels[i], force);
  }
}

void screenRenderFull() {
  if (!screenCount) return;

  ScreenEntry& entry = screenEntries[screenActive];
  if (entry.type == SCREEN_STATUS) {
    statusScreenEnter();
  } else {
    if (entry.type == SCREEN_IMAGE) {
      imageScreenDraw(entry);
    } else {
      tft.fillScreen(entry.bg);
    }
    for (uint8_t i = 0; i < entry.labelCount; i++) entry.labels[i].shown[0] = '\0';
    drawLabels(entry, true);
  }
  drawDots(entry.bg);
}

void screenShow(uint8_t index, bool save) {
  if (!screenCount) return;
  const uint8_t next = (uint8_t)(index % screenCount);
  const bool changed = screenInitialised && next != screenActive;

  screenActive = next;
  screenResetOverrides();

  if (changed) {
    screenSwitchCount++;
    scriptRunner.emitEvent("screen", 1, (int32_t)screenActive);
  }

  screenRenderFull();
  screenInitialised = true;

  if (save) screenStateSave();
  screenTelemetry();
  screenSend();
}

void screenNext(int8_t direction) {
  if (screenCount < 2) return;
  const int16_t next = (int16_t)screenActive + direction;
  if (next < 0) {
    screenShow((uint8_t)(screenCount - 1), true);
  } else if (next >= screenCount) {
    screenShow(0, true);
  } else {
    screenShow((uint8_t)next, true);
  }
}

void screenActivate(uint8_t index) { pendingShow = (int8_t)index; }

void screenSetSwipe(bool enabled) {
  screenSwipeEnabled = enabled;
  screenStateSave();
  screenTelemetry();
  screenSend();
}

void screenSetValue(uint8_t slot, const char* value) {
  if (slot >= SCREEN_LABEL_MAX || !screenCount) return;
  ScreenEntry& entry = screenEntries[screenActive];
  for (uint8_t i = 0; i < entry.labelCount; i++) {
    ScreenLabel& label = entry.labels[i];
    if (label.slot != slot) continue;
    strncpy(label.override, value ? value : "", sizeof(label.override) - 1);
    label.override[sizeof(label.override) - 1] = '\0';
    label.overrideActive = true;
    drawLabel(entry, label, true);
    return;
  }
}

void screenResetValue(uint8_t slot) {
  if (slot >= SCREEN_LABEL_MAX || !screenCount) return;
  ScreenEntry& entry = screenEntries[screenActive];
  for (uint8_t i = 0; i < entry.labelCount; i++) {
    ScreenLabel& label = entry.labels[i];
    if (label.slot != slot) continue;
    label.overrideActive = false;
    label.override[0] = '\0';
    drawLabel(entry, label, true);
    return;
  }
}

static int32_t valueInt(const Value& value) {
  switch (value.type) {
    case VAL_INT:
      return value.intVal;
    case VAL_FLOAT:
      return (int32_t)value.floatVal;
    case VAL_STRING: {
      char buffer[16];
      const size_t length =
          value.stringVal.len < sizeof(buffer) - 1 ? value.stringVal.len : sizeof(buffer) - 1;
      memcpy(buffer, value.stringVal.data, length);
      buffer[length] = '\0';
      return atoi(buffer);
    }
    default:
      return (int32_t)value.uintVal;
  }
}

static void valueText(const Value& value, char* out, size_t size) {
  switch (value.type) {
    case VAL_STRING: {
      const size_t length = value.stringVal.len < size - 1 ? value.stringVal.len : size - 1;
      memcpy(out, value.stringVal.data, length);
      out[length] = '\0';
      break;
    }
    case VAL_FLOAT:
      snprintf(out, size, "%.2f", (double)value.floatVal);
      break;
    case VAL_INT:
      snprintf(out, size, "%ld", (long)value.intVal);
      break;
    default:
      snprintf(out, size, "%lu", (unsigned long)value.uintVal);
      break;
  }
}

static bool snextHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  (void)paramCount;
  (void)params;
  (void)userData;
  screenNext(1);
  result.type = VAL_INT;
  result.intVal = (int32_t)screenActive;
  return true;
}

static bool sprevHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  (void)paramCount;
  (void)params;
  (void)userData;
  screenNext(-1);
  result.type = VAL_INT;
  result.intVal = (int32_t)screenActive;
  return true;
}

static bool sshowHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  (void)userData;
  result.type = VAL_INT;
  result.intVal = (int32_t)screenActive;
  if (paramCount < 1 || !screenCount) return false;
  screenShow((uint8_t)valueInt(params[0]), true);
  result.intVal = (int32_t)screenActive;
  return true;
}

static bool sindexHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  (void)paramCount;
  (void)params;
  (void)userData;
  result.type = VAL_INT;
  result.intVal = (int32_t)screenActive;
  return true;
}

static bool svalHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  (void)userData;
  result.type = VAL_INT;
  result.intVal = 0;
  if (paramCount < 2) return false;
  char text[SCREEN_TEXT_LEN];
  valueText(params[1], text, sizeof(text));
  screenSetValue((uint8_t)valueInt(params[0]), text);
  result.intVal = 1;
  return true;
}

static bool sresetHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  (void)userData;
  result.type = VAL_INT;
  result.intVal = 0;
  if (paramCount < 1) return false;
  screenResetValue((uint8_t)valueInt(params[0]));
  result.intVal = 1;
  return true;
}

static bool srefreshHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  (void)paramCount;
  (void)params;
  (void)userData;
  screenRenderFull();
  result.type = VAL_INT;
  result.intVal = 1;
  return true;
}

static void handleGestures(uint32_t now) {
  if (now == lastGestureMs) return;
  lastGestureMs = now;

  const bool pressed = touchPressed;
  if (pressed && !wasPressed) {
    pressX = touchX;
    pressY = touchY;
    pressMs = now;
  }

  if (!pressed && wasPressed) {
    int16_t dx = (int16_t)touchX - (int16_t)pressX;
    int16_t dy = (int16_t)touchY - (int16_t)pressY;
    if (display.rotation == 3) {
      dx = (int16_t)-dx;
      dy = (int16_t)-dy;
    }
    const uint32_t elapsed = now - pressMs;
    const int16_t absX = (int16_t)(dx < 0 ? -dx : dx);
    const int16_t absY = (int16_t)(dy < 0 ? -dy : dy);

    if (screenSwipeEnabled && screenCount > 1 && absX >= 40 && absX > absY && elapsed < 600) {
      const int8_t direction = dx < 0 ? 1 : -1;
      screenNext(direction);
      scriptRunner.emitEvent("swipe", 2, (int32_t)direction, (int32_t)screenActive);
    } else if (absX < 20 && absY < 20 && elapsed < 800) {
      scriptRunner.emitEvent("touch", 3, (int32_t)touchX, (int32_t)touchY,
                             (int32_t)screenActive);
    }
  }

  wasPressed = pressed;
}

void setupScreen() {
  imageScreenSetup();
  screenLayoutDefault();
  loadState();
  loadLayout();

  if (screenCount == 0) screenLayoutDefault();
  if (screenState.active >= screenCount) screenState.active = 0;
  screenActive = screenState.active;
  screenSwipeEnabled = screenState.swipe != 0;

  scriptRunner.registerFunction("snext", snextHandler);
  scriptRunner.registerFunction("sprev", sprevHandler);
  scriptRunner.registerFunction("sshow", sshowHandler);
  scriptRunner.registerFunction("sindex", sindexHandler);
  scriptRunner.registerFunction("sval", svalHandler);
  scriptRunner.registerFunction("sreset", sresetHandler);
  scriptRunner.registerFunction("srefresh", srefreshHandler);

  screenInitialised = true;
  screenRenderFull();
  screenTelemetry();
  screenSend();
}

void loopScreen(uint32_t now) {
  if (tasks[KEY_SCREEN]) {
    tasks[KEY_SCREEN] = 0;
    screenProcessCommand();
  }

  if (pendingSwipe >= 0) {
    screenSwipeEnabled = pendingSwipe != 0;
    pendingSwipe = -1;
    screenStateSave();
    screenTelemetry();
    screenSend();
  }

  char* layout = pendingConfig;
  pendingConfig = nullptr;
  if (layout) {
    if (parseLayout(layout)) {
      screenLayoutSave();
      if (screenActive >= screenCount) screenActive = 0;
      screenStateSave();
      fullRedraw = true;
    }
    free(layout);
  }

  if (pendingDefault) {
    pendingDefault = false;
    screenLayoutDefault();
    screenLayoutSave();
    screenActive = 0;
    screenStateSave();
    fullRedraw = true;
  }

  if (pendingShow >= 0) {
    const uint8_t index = (uint8_t)pendingShow;
    pendingShow = -1;
    screenShow(index, true);
  }

  if (fullRedraw) {
    fullRedraw = false;
    screenRenderFull();
    screenTelemetry();
    screenSend();
  }

  handleGestures(now);

  if (!screenCount) return;
  if (screenEntries[screenActive].type == SCREEN_STATUS) {
    statusScreenTick(now);
  } else if (now - lastLabelTick >= 250) {
    lastLabelTick = now;
    drawLabels(screenEntries[screenActive], false);
  }
}

void onWsEventScreen(void* arg, uint8_t* data, size_t len, uint32_t clientId, uint8_t task) {
  (void)clientId;
  if (!arg || !data || task != KEY_SCREEN) return;
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (info->len != sizeof(screen) || len < sizeof(screen)) return;

  // Only control fields are taken from the client. The rest of the struct is
  // telemetry and must not be overwritten by defaults from the web store.
  screen.command = data[1];
  screen.active = data[3];
  screen.swipe = data[5];
  tasks[KEY_SCREEN] = 1;
}

void screenProcessCommand() {
  switch (screen.command) {
    case CMD_CHANGE:
      screenNext(1);
      break;
    case CMD_SET:
      if (screen.active < screenCount && screen.active != screenActive) {
        screenShow(screen.active, true);
      }
      screenSetSwipe(screen.swipe != 0);
      break;
    case CMD_RESTORE:
      pendingDefault = true;
      break;
    case CMD_GET:
    default:
      screenTelemetry();
      screenSend();
      break;
  }
}
