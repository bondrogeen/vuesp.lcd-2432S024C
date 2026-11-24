#include "./webserver.h"

#include "../display/display.h"
#include "../screen/screen.h"
#include "../sd/sd.h"
#include "../speaker/speaker.h"

#include <SD.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/esp");

Ping ping = {KEY_PING};
Progress progress = {KEY_PROGRESS, 5, 0, 0, 0};

uint32_t clientID = 0;
uint32_t lastTime = 0;
uint32_t lastCleanup = 0;
uint32_t lastProgress = 0;

// Upload progress is throttled: a large file must not flood every client with
// a frame per chunk. PING stays at 1 s as the liveness heartbeat.
#define PROGRESS_MIN_INTERVAL_MS 250
#define CLEANUP_INTERVAL_MS 1000

static AsyncAuthenticationMiddleware basicAuth;
static AsyncCorsMiddleware cors;

String status(uint8_t state) {
  return (state) ? "{\"state\":true}" : "{\"state\":false}";
}

uint8_t wsClientCount() {
  return (uint8_t)ws.count();
}

void wsSend(uint8_t* message, size_t len) {
  if (message == nullptr || len == 0) return;
  if (clientID && ws.hasClient(clientID)) {
    ws.binary(clientID, message, len);
  }
}

void wsSendAll(uint8_t* message, size_t len) {
  if (message == nullptr || len == 0) return;
  ws.binaryAll(message, len);
}

void emitEvent(char* message) {
  if (message == nullptr) return;
  memset(event.name, 0, sizeof(event.name));
  strcpy(event.name, message);
  wsSendAll((uint8_t*)&event, sizeof(event));
  scriptRunner.emitEvent(message);
}

void sendProgress(bool force = false) {
  if (ws.count() == 0) return;

  const uint32_t now = millis();
  if (!force && progress.status == 2 && now - lastProgress < PROGRESS_MIN_INTERVAL_MS) return;

  ws.binaryAll((uint8_t*)&progress, sizeof(progress));
  lastProgress = now;
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  clientID = client->id();
  if (type == WS_EVT_DISCONNECT) {
    if (clientID && !ws.hasClient(clientID)) {
      clientID = 0;
    }
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_BINARY) {
      if (info->final && info->index == 0 && info->len == len) {
        uint8_t task = data[0];
        if (task > KEY_END) return;
        onWsEventTasks(arg, data, len, clientID, task);
        onWsEventDevice(arg, data, len, clientID, task);
        onWsEventDisplay(arg, data, len, clientID, task);
        onWsEventSpeaker(arg, data, len, clientID, task);
        onWsEventScreen(arg, data, len, clientID, task);
      }
    }
  }
}

void onReqUpload(AsyncWebServerRequest* request) {
  uint8_t method = request->method();
  if (request->hasParam("file")) {
    const AsyncWebParameter* p = request->getParam("file");
    if (method == HTTP_GET)
      if (LittleFS.exists(p->value())) return request->send(LittleFS, p->value(), String(), true);
    if (method == HTTP_DELETE)
      if (LittleFS.exists(p->value()) && (LittleFS.remove(p->value()) || LittleFS.rmdir(p->value()))) return request->send(200, RES_TYPE_JSON, status(1));
  }
  if (method == HTTP_POST && request->hasParam("format"))
    if (LittleFS.format()) return request->send(200, RES_TYPE_JSON, status(1));

  if (method == HTTP_POST) {
    progress.size = 0;
    progress.status = 0;
    return request->send(200, RES_TYPE_JSON, status(1));
  }
  request->send(404, RES_TYPE_JSON, status(0));
}

void onUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  const uint32_t MAX_FILE_SIZE = 5 * 1024 * 1024;

  progress.size += len;
  progress.status = !index ? 1 : 2;

  if (progress.size > MAX_FILE_SIZE) {
    if (request->_tempFile) {
      request->_tempFile.close();
      LittleFS.remove(filename);
    }
    progress.status = 0;
    request->send(413, RES_TYPE_JSON, status(0));
    return;
  }

  if (!index) {
    progress.size = 0;
    progress.length = request->contentLength();
    if (request->contentLength() > MAX_FILE_SIZE) {
      request->send(413, RES_TYPE_JSON, status(0));
      return;
    }

    if (request->_tempFile) request->_tempFile.close();
    request->_tempFile = LittleFS.open(filename, "w");
    if (!request->_tempFile) {
      request->send(500, RES_TYPE_JSON, status(0));
      return;
    }
  }
  if (len && request->_tempFile) {
    if (progress.size <= MAX_FILE_SIZE) request->_tempFile.write(data, len);
  }
  if (final) {
    if (request->_tempFile) request->_tempFile.close();
  }
  sendProgress(final);
}

void onReqUpdate(AsyncWebServerRequest* request) {
  uint8_t isReboot = !Update.hasError();
#if VUESP_LOG_LEVEL >= 1
  Serial.printf("[ota] result: %s, bytes=%u\n", isReboot ? "ok" : "error", (unsigned)progress.size);
#endif
  AsyncWebServerResponse* response = request->beginResponse(200, RES_TYPE_JSON, status(isReboot));
  response->addHeader("Connection", "close");
  request->send(response);
  tasks[KEY_REBOOT] = true;
}

void onUpdate(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  progress.size += len;
  progress.status = !index ? 1 : 2;

  if (!index) {
    progress.size = 0;
    progress.length = request->contentLength();
#if defined(ESP8266)
    Update.runAsync(true);
#endif
    int cmd = (filename == "littlefs.bin") ? 100 : U_FLASH;
    size_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin((cmd == 100) ? infoFS.totalBytes : maxSketchSpace, cmd)) {
      Update.printError(Serial);
      request->send(400, RES_TYPE_JSON, status(0));
      return;
    }
#if VUESP_LOG_LEVEL >= 1
    Serial.printf("[ota] begin: %s, contentLength=%u\n", (cmd == 100) ? "littlefs" : "firmware",
                  (unsigned)request->contentLength());
#endif
  }

  if (!Update.hasError()) {
    size_t written = Update.write(data, len);
    if (written != len) {
      Update.printError(Serial);
    }
  }

  if (final) {
    if (Update.hasError()) {
      Update.printError(Serial);
      request->send(500, RES_TYPE_JSON, status(0));
      return;
    }
    if (!Update.end(true)) {
      Update.printError(Serial);
      request->send(500, RES_TYPE_JSON, status(0));
      return;
    }
  }

  sendProgress(final);
}

void onRecovery(AsyncWebServerRequest* request) {
  const bool hasIndex = LittleFS.exists("/www/index.html") || LittleFS.exists("/www/index.html.gz");
  if (hasIndex && !request->hasParam("force")) {
    request->redirect("/");
  } else {
    AsyncWebServerResponse* response = request->beginResponse_P(200, "text/html", recovery, sizeof(recovery));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  }
}

// void onGetData(AsyncWebServerRequest *request) {
//   AsyncResponseStream *response = request->beginResponseStream("application/x-binary", sizeof(infoFS));
//   response->write((const uint8_t *)&infoFS, sizeof(infoFS));
//   request->send(response);
// }
static String sdNormalizePath(const String& input) {
  String path = input;
  if (!path.length()) path = "/";
  if (!path.startsWith("/")) path = "/" + path;
  while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
  return path;
}

static String sdJsonEscape(const String& input) {
  String out;
  for (size_t i = 0; i < input.length(); i++) {
    const char c = input[i];
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

static void ensureSdDir(const String& path) {
  int index = 1;
  while ((index = path.indexOf('/', index)) >= 0) {
    const String dir = path.substring(0, index);
    if (dir.length() && !SD.exists(dir)) SD.mkdir(dir);
    index++;
  }
}

static void onSdList(AsyncWebServerRequest* request) {
  if (!sdReady) {
    request->send(200, "application/json", "[]");
    return;
  }

  const String path = sdNormalizePath(request->hasParam("path") ? request->getParam("path")->value() : "/");
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    request->send(200, "application/json", "[]");
    return;
  }

  AsyncResponseStream* response = request->beginResponseStream("application/json");
  response->print("[");
  bool first = true;
  File entry = dir.openNextFile();
  while (entry) {
    String name = entry.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (!first) response->print(",");
    first = false;
    response->printf("{\"name\":\"%s\",\"size\":%u,\"isDir\":%s,\"isFile\":%s}",
                     sdJsonEscape(name).c_str(), (unsigned)entry.size(),
                     entry.isDirectory() ? "true" : "false",
                     entry.isDirectory() ? "false" : "true");
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  response->print("]");
  request->send(response);
}

void onReqSd(AsyncWebServerRequest* request) {
  const uint8_t method = request->method();
  if (request->hasParam("file")) {
    const String path = sdNormalizePath(request->getParam("file")->value());
    if (method == HTTP_GET && SD.exists(path)) {
      return request->send(SD, path, String(), true);
    }
    if (method == HTTP_DELETE && SD.exists(path)) {
      File entry = SD.open(path);
      const bool isDir = entry && entry.isDirectory();
      if (entry) entry.close();
      const bool removed = isDir ? SD.rmdir(path) : SD.remove(path);
      return request->send(200, RES_TYPE_JSON, status(removed ? 1 : 0));
    }
  }

  if (method == HTTP_POST && request->hasParam("format")) {
    if (!sdReady) return request->send(200, RES_TYPE_JSON, status(0));
    return request->send(200, RES_TYPE_JSON, status(formatSD() ? 1 : 0));
  }

  if (method == HTTP_POST) {
    return request->send(200, RES_TYPE_JSON, status(1));
  }
  request->send(404, RES_TYPE_JSON, status(0));
}

static void ensureFsDir(fs::FS& volume, const String& dir) {
  if (!dir.length() || dir == "/") return;
  int index = 1;
  while ((index = dir.indexOf('/', index)) >= 0) {
    const String sub = dir.substring(0, index);
    if (sub.length() && !volume.exists(sub)) volume.mkdir(sub);
    index++;
  }
  if (!volume.exists(dir)) volume.mkdir(dir);
}

static bool copyTree(fs::FS& source, fs::FS& target, const String& srcPath, const String& destDir) {
  File entry = source.open(srcPath, "r");
  if (!entry) return false;

  String base = srcPath.substring(srcPath.lastIndexOf('/') + 1);
  String dest = destDir;
  if (!dest.endsWith("/")) dest += "/";
  dest += base;

  if (entry.isDirectory()) {
    if (!target.exists(dest)) target.mkdir(dest);
    bool ok = true;
    File child = entry.openNextFile();
    while (child) {
      String childPath = srcPath;
      if (!childPath.endsWith("/")) childPath += "/";
      childPath += child.name();
      if (!copyTree(source, target, childPath, dest)) ok = false;
      child.close();
      child = entry.openNextFile();
    }
    entry.close();
    return ok;
  }

  const int slash = dest.lastIndexOf('/');
  ensureFsDir(target, slash > 0 ? dest.substring(0, slash) : String("/"));

  File out = target.open(dest, "w");
  if (!out) {
    entry.close();
    return false;
  }

  uint8_t buffer[512];
  while (entry.available()) {
    const size_t got = entry.read(buffer, sizeof(buffer));
    if (!got) break;
    if (out.write(buffer, got) != got) {
      out.close();
      entry.close();
      return false;
    }
  }
  out.close();
  entry.close();
  return true;
}

void onCopyFile(AsyncWebServerRequest* request) {
  if (!request->hasParam("from") || !request->hasParam("to") || !request->hasParam("file") ||
      !request->hasParam("dir")) {
    return request->send(400, RES_TYPE_JSON, status(0));
  }

  const String from = request->getParam("from")->value();
  const String to = request->getParam("to")->value();
  if (from == to) return request->send(400, RES_TYPE_JSON, status(0));

  const String file = sdNormalizePath(request->getParam("file")->value());
  const String dir = sdNormalizePath(request->getParam("dir")->value());

  bool ok = false;
  if (from == "fs" && to == "sd") {
    if (sdReady) ok = copyTree(LittleFS, SD, file, dir);
  } else if (from == "sd" && to == "fs") {
    if (sdReady) ok = copyTree(SD, LittleFS, file, dir);
  }
  request->send(200, RES_TYPE_JSON, status(ok ? 1 : 0));
}

void onSdUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  progress.size += len;
  progress.status = !index ? 1 : 2;

  if (!index) {
    progress.size = 0;
    progress.length = request->contentLength();
    const String path = sdNormalizePath(filename);
    ensureSdDir(path);
    if (request->_tempFile) request->_tempFile.close();
    request->_tempFile = SD.open(path, "w");
    if (!request->_tempFile) {
      request->send(500, RES_TYPE_JSON, status(0));
      return;
    }
  }

  if (len && request->_tempFile) request->_tempFile.write(data, len);
  if (final && request->_tempFile) request->_tempFile.close();
  sendProgress(final);
}

char cmdScriptBuffer[256];

void onCmd(AsyncWebServerRequest* request) {
  if (!request->hasParam("id") || !request->hasParam("script")) {
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }

  const String& idValue = request->getParam("id")->value();
  const String& scriptValue = request->getParam("script")->value();

  const uint8_t id = (uint8_t)atoi(idValue.c_str());
  const size_t scriptLen = scriptValue.length();

  if (id == 0 || scriptLen >= 255) {
    request->send(400, RES_TYPE_JSON, status(0));
    return;
  }

  // AsyncWebServer already URL-decodes query/form parameters; do not decode
  // them a second time.
  strncpy(cmdScriptBuffer, scriptValue.c_str(), 255);
  cmdScriptBuffer[255] = '\0';

  bool result = scriptRunner.registerScript(id, cmdScriptBuffer);
  if (result) result = scriptRunner.runScript(id) >= 0;
  request->send(200, RES_TYPE_JSON, status(result));
}

void onRoot(AsyncWebServerRequest* request) {
  if (!LittleFS.exists("/www/index.html") && !LittleFS.exists("/www/index.html.gz")) {
    request->redirect("/recovery");
    return;
  }

  AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/www/index.html", "text/html");
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
}

void onRedirectHome(AsyncWebServerRequest* request) {
  request->redirect("/");
}

void setupServer() {

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, onRoot);
  server.serveStatic("/assets/", LittleFS, "/www/assets/").setCacheControl("public, max-age=31536000, immutable");
  server.serveStatic("/", LittleFS, "/www/").setCacheControl("no-store");

  server.on("/fs", HTTP_ANY, onReqUpload, onUpload);
  server.on("/sd/list", HTTP_GET, onSdList);
  server.on("/sd", HTTP_ANY, onReqSd, onSdUpload);
  server.on("/copy", HTTP_POST, onCopyFile);
  server.on("/cmd", HTTP_GET, onCmd);
  setupScreenApi(server);
  server.on("/update", HTTP_POST, onReqUpdate, onUpdate);
  server.on("/recovery", HTTP_GET, onRecovery);
  server.on("*", HTTP_ANY, onRoot);

  if (settings.authMode) {
    basicAuth.setUsername(settings.authLogin);
    basicAuth.setPassword(settings.authPass);
    basicAuth.setRealm("vuesp");
    basicAuth.setAuthFailureMessage("Authentication failed");
    basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
    basicAuth.generateHash();
    server.addMiddleware(&basicAuth);
  }

  server.begin();
}

void loopServer(uint32_t now) {
  // Scanning clients every millisecond is wasteful; once per second is enough
  // to release disconnected sockets.
  if (now - lastCleanup >= CLEANUP_INTERVAL_MS) {
    lastCleanup = now;
    ws.cleanupClients();
  }

  if (!ws.count()) return;

  if (now - lastTime > 1000) {
    lastTime = now;
    if (progress.status == 0) {
      sendProgress();
      progress.status = 5;
    } else {
      ws.binaryAll((uint8_t*)&ping, sizeof(ping));
    }
  }
}