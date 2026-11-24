#ifndef SPEAKER_H
#define SPEAKER_H

#include "../const/protocol.h"

void setupSpeaker();
void loopSpeaker(uint32_t now);
void onWsEventSpeaker(void* arg, uint8_t* data, size_t len, uint32_t clientId, uint8_t task);

extern Speaker speaker;

#endif
