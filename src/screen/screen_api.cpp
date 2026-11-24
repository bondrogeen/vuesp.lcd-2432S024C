#include "./screen.h"

#include <LittleFS.h>
#include <SD.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../sd/sd.h"
#include "../display/display.h"
#include "../webserver/webserver.h"

#define SCREEN_IMAGE_MAX (300 * 1024)
#define SCREEN_IMAGE_DIR "/images"

static char configBuffer[SCREEN_CONFIG_MAX];
static size_t configLength = 0;
static bool configOverflow = false;

static File uploadFile;
static String uploadPath;
static uint8_t uploadLocation = SCREEN_LOCATION_FS;
static size_t uploadSize = 0;
static bool uploadOk = false;
static bool uploadTooLarge = false;

static void colorHex(uint16_t color, char* out, size_t size) {
  const uint8_t r = (uint8_t)(((color >> 11) & 0x1F) * 255 / 31);
  const uint8_t g = (uint8_t)(((color >> 5) & 0x3F) * 255 / 63);
  const uint8_t b = (uint8_t)((color & 0x1F) * 255 / 31);
  snprintf(out, size, "#%02X%02X%02X", r, g, b);
}

static String jsonEscape(const char* value) {
  String out;
  if (!value) return out;
  for (const char* pos = value; *pos; pos++) {
    const char c = *pos;
    if (c == '"' || c == '\\') out += '\\';
    out += ((uint8_t)c < 0x20) ? ' ' : c;
  }
  return out;
}

static fs::FS* volumeFor(uint8_t location) {
  if (location == SCREEN_LOCATION_SD) return sdReady ? &SD : nullptr;
  return &LittleFS;
}

static bool safeImagePath(const String& input) {
  if (!input.startsWith(SCREEN_IMAGE_DIR "/")) return false;
  if (input.indexOf("..") >= 0) return false;
  if (input.length() >= SCREEN_PATH_LEN) return false;
  return true;
}

static String safeImageName(const String& input) {
  String out;
  for (size_t i = 0; i < input.length(); i++) {
    const char c = (char)tolower((unsigned char)input[i]);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
        c == '-') {
      out += c;
    }
  }
  if (!out.length()) out = "image";
  if (!out.endsWith(".jpg") && !out.endsWith(".jpeg")) out += ".jpg";
  return out;
}

static String baseName(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

static void ensureImagesDir(fs::FS& volume) {
  if (!volume.exists(SCREEN_IMAGE_DIR)) volume.mkdir(SCREEN_IMAGE_DIR);
}

static void onScreensGet(AsyncWebServerRequest* request) {
  AsyncResponseStream* response = request->beginResponseStream(RES_TYPE_JSON);
  response->print("{\"active\":");
  response->print(screenActive);
  response->print(",\"swipe\":");
  response->print(screenSwipeEnabled ? 1 : 0);
  response->print(",\"fsFree\":");
  response->print((unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()));
  response->print(",\"sdFree\":");
  response->print(sdReady ? (unsigned long)(SD.totalBytes() - SD.usedBytes()) : 0);
  response->print(",\"screens\":[");

  for (uint8_t i = 0; i < screenCount; i++) {
    const ScreenEntry& entry = screenEntries[i];
    char background[8];
    colorHex(entry.bg, background, sizeof(background));

    if (i) response->print(",");
    response->print("{\"name\":\"");
    response->print(jsonEscape(entry.name));
    response->print("\",\"type\":\"");
    response->print(screenTypeName(entry.type));
    response->print("\",\"location\":\"");
    response->print(screenLocationName(entry.location));
    response->print("\",\"file\":\"");
    response->print(jsonEscape(entry.image));
    response->print("\",\"bg\":\"");
    response->print(background);
    response->print("\",\"labels\":[");

    for (uint8_t l = 0; l < entry.labelCount; l++) {
      const ScreenLabel& label = entry.labels[l];
      char labelColor[8];
      colorHex(label.color, labelColor, sizeof(labelColor));
      if (l) response->print(",");
      response->print("{\"slot\":");
      response->print(label.slot);
      response->print(",\"x\":");
      response->print(label.x);
      response->print(",\"y\":");
      response->print(label.y);
      response->print(",\"w\":");
      response->print(label.w);
      response->print(",\"h\":");
      response->print(label.h);
      response->print(",\"size\":");
      response->print(label.size);
      response->print(",\"color\":\"");
      response->print(labelColor);
      response->print("\",\"align\":\"");
      response->print(screenAlignName(label.align));
      response->print("\",\"text\":\"");
      response->print(jsonEscape(label.text));
      response->print("\"}");
    }
    response->print("]}");
  }
  response->print("]}");
  request->send(response);
}

static void onScreensUpload(AsyncWebServerRequest* request, String filename, size_t index,
                            uint8_t* data, size_t len, bool final) {
  (void)request;
  (void)filename;
  if (!index) {
    configLength = 0;
    configOverflow = false;
  }
  if (len && configLength + len < sizeof(configBuffer)) {
    memcpy(configBuffer + configLength, data, len);
    configLength += len;
  } else if (len) {
    configOverflow = true;
  }
  if (final) configBuffer[configLength] = '\0';
}

static void onScreensPost(AsyncWebServerRequest* request) {
  if (configOverflow || !configLength) {
    configLength = 0;
    configOverflow = false;
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }
  if (!screenLayoutApply(configBuffer, configLength)) {
    configLength = 0;
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }
  configLength = 0;
  request->send(200, RES_TYPE_JSON, status(1));
}

static void onScreenImageGet(AsyncWebServerRequest* request) {
  if (!request->hasParam("file")) {
    request->send(404, RES_TYPE_JSON, status(0));
    return;
  }

  const uint8_t location =
      request->hasParam("location") && request->getParam("location")->value() == "sd"
          ? SCREEN_LOCATION_SD
          : SCREEN_LOCATION_FS;
  const String path = request->getParam("file")->value();
  fs::FS* volume = volumeFor(location);
  if (!volume || !safeImagePath(path) || !volume->exists(path)) {
    request->send(404, RES_TYPE_JSON, status(0));
    return;
  }

  AsyncWebServerResponse* response = request->beginResponse(*volume, path, "image/jpeg");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

static void onScreenImageDelete(AsyncWebServerRequest* request) {
  if (!request->hasParam("file")) {
    request->send(404, RES_TYPE_JSON, status(0));
    return;
  }

  const uint8_t location =
      request->hasParam("location") && request->getParam("location")->value() == "sd"
          ? SCREEN_LOCATION_SD
          : SCREEN_LOCATION_FS;
  const String path = request->getParam("file")->value();
  fs::FS* volume = volumeFor(location);
  if (!volume || !safeImagePath(path)) {
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }

  const bool removed = volume->exists(path) ? volume->remove(path) : true;
  request->send(200, RES_TYPE_JSON, status(removed ? 1 : 0));
}

static void onScreenUpload(AsyncWebServerRequest* request, String filename, size_t index,
                           uint8_t* data, size_t len, bool final) {
  if (!index) {
    uploadSize = 0;
    uploadOk = false;
    uploadTooLarge = false;
    uploadPath = "";
    uploadLocation = SCREEN_LOCATION_FS;

    if (request->hasParam("location") &&
        request->getParam("location")->value() == "sd") {
      uploadLocation = SCREEN_LOCATION_SD;
    }

    String name = request->hasParam("name") ? request->getParam("name")->value()
                                             : baseName(filename);
    name = safeImageName(baseName(name));
    uploadPath = String(SCREEN_IMAGE_DIR "/") + name;

    fs::FS* volume = volumeFor(uploadLocation);
    if (!volume) return;
    ensureImagesDir(*volume);

    if (uploadFile) uploadFile.close();
    uploadFile = volume->open(uploadPath, "w");
    uploadOk = (bool)uploadFile;
  }

  if (!uploadOk && !final) return;

  if (uploadOk) {
    uploadSize += len;
    if (uploadSize > SCREEN_IMAGE_MAX) {
      uploadTooLarge = true;
      uploadOk = false;
      if (uploadFile) uploadFile.close();
    } else if (len && uploadFile.write(data, len) != len) {
      uploadOk = false;
      if (uploadFile) uploadFile.close();
    }
  }

  if (final) {
    if (uploadFile) uploadFile.close();
    if (!uploadOk) {
      fs::FS* volume = volumeFor(uploadLocation);
      if (volume && uploadPath.length()) volume->remove(uploadPath);
    }
  }
}

static void onScreenUploadRequest(AsyncWebServerRequest* request) {
  if (uploadTooLarge) {
    request->send(413, RES_TYPE_JSON, status(0));
    return;
  }
  if (!uploadOk || !uploadPath.length()) {
    request->send(500, RES_TYPE_JSON, status(0));
    return;
  }

  char result[160];
  snprintf(result, sizeof(result),
           "{\"state\":true,\"file\":\"%s\",\"location\":\"%s\",\"size\":%u}",
           uploadPath.c_str(), screenLocationName(uploadLocation),
           (unsigned)uploadSize);
  uploadOk = false;
  request->send(200, RES_TYPE_JSON, result);
}

static void onScreenActivate(AsyncWebServerRequest* request) {
  if (!request->hasParam("index")) {
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }
  const int index = atoi(request->getParam("index")->value().c_str());
  if (index < 0 || index >= screenCount) {
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }
  screenActivate((uint8_t)index);
  request->send(200, RES_TYPE_JSON, status(1));
}

static void onScreenState(AsyncWebServerRequest* request) {
  if (!request->hasParam("swipe")) {
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }
  screenRequestSwipe(request->getParam("swipe")->value() == "1");
  request->send(200, RES_TYPE_JSON, status(1));
}

static void onScreenReset(AsyncWebServerRequest* request) {
  screenRequestDefault();
  request->send(200, RES_TYPE_JSON, status(1));
}

// Diagnostic probe used to verify that something was actually rendered.
// Reads a small rectangle back from the panel and reports how many pixels
// differ from the background.
static void onScreenProbe(AsyncWebServerRequest* request) {
  int x = request->hasParam("x") ? atoi(request->getParam("x")->value().c_str()) : 0;
  int y = request->hasParam("y") ? atoi(request->getParam("y")->value().c_str()) : 0;
  int w = request->hasParam("w") ? atoi(request->getParam("w")->value().c_str()) : 32;
  int h = request->hasParam("h") ? atoi(request->getParam("h")->value().c_str()) : 32;

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > 32) w = 32;
  if (h > 32) h = 32;
  if (x + w > tft.width()) w = tft.width() - x;
  if (y + h > tft.height()) h = tft.height() - y;

  static uint16_t pixels[32 * 32];
  tft.readRect(x, y, w, h, pixels);

  uint16_t nonBlack = 0;
  for (int i = 0; i < w * h; i++) {
    if (pixels[i]) nonBlack++;
  }

  char result[96];
  snprintf(result, sizeof(result), "{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"nonBlack\":%u,\"total\":%d}",
           x, y, w, h, (unsigned)nonBlack, w * h);
  request->send(200, RES_TYPE_JSON, result);
}

void setupScreenApi(AsyncWebServer& server) {
  server.on(AsyncURIMatcher::exact("/screens"), HTTP_GET, onScreensGet);
  server.on(AsyncURIMatcher::exact("/screens"), HTTP_POST, onScreensPost, onScreensUpload);
  server.on(AsyncURIMatcher::exact("/screens/image"), HTTP_GET, onScreenImageGet);
  server.on(AsyncURIMatcher::exact("/screens/image"), HTTP_DELETE, onScreenImageDelete);
  server.on(AsyncURIMatcher::exact("/screens/image"), HTTP_POST, onScreenUploadRequest, onScreenUpload);
  server.on(AsyncURIMatcher::exact("/screens/activate"), HTTP_POST, onScreenActivate);
  server.on(AsyncURIMatcher::exact("/screens/state"), HTTP_POST, onScreenState);
  server.on(AsyncURIMatcher::exact("/screens/reset"), HTTP_POST, onScreenReset);
  server.on(AsyncURIMatcher::exact("/screens/probe"), HTTP_GET, onScreenProbe);
}
