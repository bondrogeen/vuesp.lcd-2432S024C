#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

void setupTouch();
void loopTouch(uint32_t now);

extern bool touchPressed;
extern uint16_t touchX;
extern uint16_t touchY;
extern uint16_t touchRawX;
extern uint16_t touchRawY;
extern uint32_t touchCount;

#endif
