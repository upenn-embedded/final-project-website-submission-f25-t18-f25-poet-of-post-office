#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif


void status_server_start(void);
void status_server_stop(void);


void status_set_mode(const char *mode);
void status_set_voice(const char *word);
void status_set_gesture(const char *gesture);

#ifdef __cplusplus
}
#endif
