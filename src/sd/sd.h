#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

void setupSD();
void loopSD(uint32_t now);
bool formatSD();

extern bool sdReady;
extern uint64_t sdTotal;
extern uint64_t sdUsed;

#endif
