#include "./device.h"

#include "../config/ConfigStore.h"

Buffer myBuffer = {KEY_BUFFER};

Device device = {KEY_DEVICE, 0, 100, 1760640900, 2, "text", ""};
uint32_t lastTimeDevice = 0;
static uint32_t lastRgbCheck = 0;

static uint16_t portValue(uint8_t gpio) {
  for (int i = 0; i < ports_len; i++) {
    if (ports[i].gpio == gpio) return ports[i].value;
  }
  return 255;
}

static void setPwmPort(uint8_t gpio, uint8_t value) {
  analogWrite(gpio, value);
  for (int i = 0; i < ports_len; i++) {
    if (ports[i].gpio == gpio) {
      ports[i].value = value;
      ports[i].mode = PORT_PWM;
      return;
    }
  }
}

static int8_t hexDigit(char value) {
  if (value >= '0' && value <= '9') return (int8_t)(value - '0');
  if (value >= 'a' && value <= 'f') return (int8_t)(value - 'a' + 10);
  if (value >= 'A' && value <= 'F') return (int8_t)(value - 'A' + 10);
  return -1;
}

// Accepts "#RRGGBB", "RRGGBB" and an empty string (off). "OFF" is kept as
// an alias so older clients do not get stuck with an invalid value.
static bool parseRgb(const char* value, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (!value || !value[0] || strcmp(value, "OFF") == 0 || strcmp(value, "off") == 0) {
    r = g = b = 0;
    return true;
  }

  const char* hex = value[0] == '#' ? value + 1 : value;
  if (strlen(hex) != 6) return false;

  int8_t digits[6];
  for (uint8_t i = 0; i < 6; i++) {
    digits[i] = hexDigit(hex[i]);
    if (digits[i] < 0) return false;
  }

  r = (uint8_t)((digits[0] << 4) | digits[1]);
  g = (uint8_t)((digits[2] << 4) | digits[3]);
  b = (uint8_t)((digits[4] << 4) | digits[5]);
  return true;
}

static void updateRgbFromPorts() {
  const uint8_t r = (uint8_t)(255 - (portValue(4) > 255 ? 255 : portValue(4)));
  const uint8_t g = (uint8_t)(255 - (portValue(16) > 255 ? 255 : portValue(16)));
  const uint8_t b = (uint8_t)(255 - (portValue(17) > 255 ? 255 : portValue(17)));

  memset(device.rgb, 0, sizeof(device.rgb));
  if (r || g || b) {
    snprintf(device.rgb, sizeof(device.rgb), "#%02X%02X%02X", r, g, b);
  }
}

static void applyRgb(const char* value) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  if (!parseRgb(value, r, g, b)) {
    updateRgbFromPorts();
    return;
  }

  // Common-anode RGB: 255 is off, 0 is full brightness.
  setPwmPort(4, (uint8_t)(255 - r));
  setPwmPort(16, (uint8_t)(255 - g));
  setPwmPort(17, (uint8_t)(255 - b));
  updateRgbFromPorts();
}

// CircularBuffer<int8_t, 256> buffer(myBuffer);

void onWsEventDevice(void* arg, uint8_t* data, size_t len, uint32_t clientId, uint8_t task) {
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (task == KEY_DEVICE && info->len == sizeof(device)) {
    memcpy(&device, data, sizeof(device));
  }
}

void onSendDevice() {
  updateRgbFromPorts();
  wsSendAll((uint8_t*)&device, sizeof(device));
}

// only port.interrupt == GPIO_INTERRUPT_CHANGE
void deviceGPIO(Port* port, uint8_t type) {
#if VUESP_LOG_LEVEL >= 2
  Serial.printf("gpio:%d, value:%d", port->gpio, port->value);
  if (type == EVENT_LONG_PRESS) {
    Serial.print(", type:long");
  } else if (type == EVENT_REPEAT) {
    Serial.print(", type:repeat");
  } else if (type == EVENT_CLICK) {
    Serial.printf(", type:click, count:%d", port->count);
  } else {
    // Serial.print(port->gpio);
  }
  Serial.println("");
#else
  (void)port;
  (void)type;
#endif
}

static char displayBuffer[64] = "5";

bool dataProvider(const char* id, DataKind kind, DataValue& value, bool write) {
#if VUESP_LOG_LEVEL >= 3
  Serial.print(id);
  Serial.print(write ? "WRITE" : "READ");
  Serial.print(" kind=");
  Serial.println(kind);
#endif

  if (strcmp(id, "$display") == 0) {
    if (write) {
      if (kind == KIND_STRING) {
        char buf[65];
        uint8_t len = value.stringVal.len;
        if (len > 64) len = 64;
        strncpy(buf, (char*)value.stringVal.data, len);
        buf[len] = '\0';
        strcpy(displayBuffer, buf);
#if VUESP_LOG_LEVEL >= 3
        Serial.print("DISPLAY: ");
        Serial.println(buf);
#endif
      } else if (kind == KIND_INT) {
        snprintf(displayBuffer, sizeof(displayBuffer), "%d", value.intVal);
#if VUESP_LOG_LEVEL >= 3
        Serial.print("DISPLAY: ");
        Serial.println(value.intVal);
#endif
      } else if (kind == KIND_UINT) {
        snprintf(displayBuffer, sizeof(displayBuffer), "%u", value.uintVal);
#if VUESP_LOG_LEVEL >= 3
        Serial.print("DISPLAY: ");
        Serial.println(value.uintVal);
#endif
      } else if (kind == KIND_FLOAT) {
        snprintf(displayBuffer, sizeof(displayBuffer), "%.2f", value.floatVal);
#if VUESP_LOG_LEVEL >= 3
        Serial.print("DISPLAY: ");
        Serial.println(value.floatVal);
#endif
      }
      return true;
    } else {
      if (kind == KIND_STRING) {
        value.stringVal.data = (uint8_t*)displayBuffer;
        value.stringVal.len = strlen(displayBuffer);
      } else if (kind == KIND_INT) {
        value.intVal = atoi(displayBuffer);
      } else if (kind == KIND_UINT) {
        value.uintVal = (uint32_t)atoi(displayBuffer);
      } else if (kind == KIND_FLOAT) {
        value.floatVal = atof(displayBuffer);
      }
      return true;
    }
  }
  return false;
}

bool httpHandler(uint8_t paramCount, const Value* params, Value& result, void* userData) {
  if (paramCount < 2) return false;

  const char* method = params[0].stringVal.data;
  const char* url = params[1].stringVal.data;
  const int32_t count = params[2].intVal;
  const uint32_t num = params[3].uintVal;

  const uint8_t* data = params[4].arrayVal.data;
  uint8_t len = params[4].arrayVal.len;

#if VUESP_LOG_LEVEL >= 3
  for (uint8_t i = 0; i < len; i++) {
    Serial.print(data[i]);
  }

  Serial.println("method");
  Serial.println(method);
  Serial.println(url);
  Serial.println(count);
  Serial.println(num);
#else
  (void)data;
  (void)len;
  (void)method;
  (void)url;
  (void)count;
  (void)num;
#endif

  result.type = VAL_INT;
  result.intVal = 200;
  return true;
}

// void sendNotification(const char* text) {
//   message.type = MESSAGE_TYPE_NOTIFICATION;
//   memset(message.text, 0, sizeof(message.text));
//   strcpy(message.text, text);
//   wsSendAll((uint8_t*)&message, sizeof(message));
// }

void setupDevice() {
  updateRgbFromPorts();
  scriptRunner.setDataProvider(dataProvider);
  scriptRunner.registerFunction("http", httpHandler);
}

void setupFirstDevice() {
  const TConfigStatus status =
      configStore.read("device", &device, sizeof(device), CONFIG_DEVICE_VERSION);
  if (status == TConfigStatus::Ok) {
    device.command = 0;
  }
#if VUESP_LOG_LEVEL >= 1
  else if (status != TConfigStatus::NotFound) {
    Serial.printf("[config] device read: %d\n", (int)status);
  }
#endif
}

void loopDevice(uint32_t now) {
  // Periodic push is a template: it shows how a user can stream device state.
  if (now - lastTimeDevice > 10000) {
    lastTimeDevice = now;
    onSendDevice();
  }

  // Keep Device.rgb in sync when scripts or the GPIO page change the RGB
  // ports directly. Only send a frame when the value actually changes.
  if (now - lastRgbCheck > 500) {
    lastRgbCheck = now;
    char previous[sizeof(device.rgb)];
    strncpy(previous, device.rgb, sizeof(previous));
    previous[sizeof(previous) - 1] = '\0';
    updateRgbFromPorts();
    if (strcmp(previous, device.rgb) != 0) onSendDevice();
  }

  if (tasks[KEY_BUFFER]) {
    wsSendAll((uint8_t*)&myBuffer, sizeof(myBuffer));
    tasks[KEY_BUFFER] = 0;
  };

  if (tasks[KEY_DEVICE]) {
    if (device.command == CMD_SAVE) {
      device.command = 0;
      const TConfigStatus status =
          configStore.write("device", &device, sizeof(device), CONFIG_DEVICE_VERSION);
#if VUESP_LOG_LEVEL >= 1
      if (status != TConfigStatus::Ok) {
        Serial.printf("[config] device write: %d\n", (int)status);
      }
#endif
    }

    if (device.command == CMD_SET) applyRgb(device.rgb);

    device.command = 0;
    tasks[KEY_DEVICE] = 0;
    onSendDevice();
  };
}