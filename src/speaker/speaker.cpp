#include "./speaker.h"

#include "../config/ConfigStore.h"
#include "../tasks/tasks.h"
#include "../webserver/webserver.h"

#define SPEAKER_PIN 26

Speaker speaker = {KEY_SPEAKER};

static void sanitize() {
  if (speaker.frequency < 100) speaker.frequency = 100;
  if (speaker.frequency > 10000) speaker.frequency = 10000;
  if (speaker.duration < 20) speaker.duration = 20;
  if (speaker.duration > 5000) speaker.duration = 5000;
}

static void saveConfig() {
  speaker.key = KEY_SPEAKER;
  const TConfigStatus status =
      configStore.write("speaker", &speaker, sizeof(speaker), CONFIG_SPEAKER_VERSION);
#if VUESP_LOG_LEVEL >= 1
  if (status != TConfigStatus::Ok) {
    Serial.printf("[config] speaker write: %d\n", (int)status);
  }
#endif
}

static void sendSpeaker() {
  if (wsClientCount() == 0) return;
  wsSendAll((uint8_t*)&speaker, sizeof(speaker));
}

void onWsEventSpeaker(void* arg, uint8_t* data, size_t len, uint32_t clientId, uint8_t task) {
  if (!arg || !data || task != KEY_SPEAKER) return;
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (info->len != sizeof(speaker) || len < sizeof(speaker)) return;

  const uint8_t command = data[1];
  if (command == CMD_WRITE) {
    speaker.volume = data[3];
    speaker.frequency = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
    speaker.duration = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
  }
  speaker.command = command;
  tasks[KEY_SPEAKER] = 1;
}

void setupSpeaker() {
  const TConfigStatus status =
      configStore.read("speaker", &speaker, sizeof(speaker), CONFIG_SPEAKER_VERSION);
  if (status != TConfigStatus::Ok) {
    speaker.frequency = 1000;
    speaker.duration = 150;
    speaker.volume = 128;
  }
  sanitize();
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);
}

void loopSpeaker(uint32_t now) {
  (void)now;
  if (!tasks[KEY_SPEAKER]) return;
  tasks[KEY_SPEAKER] = 0;

  switch (speaker.command) {
    case CMD_WRITE:
      sanitize();
      // The FM8002A has a fixed gain; volume is stored for future use.
      tone(SPEAKER_PIN, speaker.frequency, speaker.duration);
      speaker.status = 1;
      speaker.count++;
      saveConfig();
      break;
    case CMD_RESTORE:
      speaker.frequency = 1000;
      speaker.duration = 150;
      speaker.volume = 128;
      sanitize();
      saveConfig();
      break;
    default:
      break;
  }

  sendSpeaker();
}
