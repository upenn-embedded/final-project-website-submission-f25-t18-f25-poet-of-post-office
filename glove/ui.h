#ifndef UI_H
#define UI_H

#include <stdint.h>

typedef enum {
    GESTURE_0 = 0,   // ???? PALM
    GESTURE_1 = 1,
    GESTURE_2 = 2,
    GESTURE_3 = 3,
    GESTURE_4 = 4,
    GESTURE_5 = 5,
    GESTURE_6 = 6,
    GESTURE_7 = 7    // ???? FIST
} gesture_t;

/* ??????? */
#define GESTURE_PALM  GESTURE_0
#define GESTURE_FIST  GESTURE_7

void UI_Init(void);
void UI_OnHeartRateUpdated(uint16_t bpm);
void UI_OnGestureUpdated(gesture_t gesture);

#endif
