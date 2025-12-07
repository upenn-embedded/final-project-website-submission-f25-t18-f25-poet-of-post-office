#pragma once

#include <string.h>
#include <stdlib.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"   

#ifdef __cplusplus
extern "C" {
#endif

typedef enum{
    WIFI_CONN_IDLE = 0,
    WIFI_CONN_STA_TRY,
    WIFI_CONN_STA_OK,
    WIFI_CONN_AP_PORTAL,
} wifi_conn_state_t;

typedef struct {
  wifi_conn_state_t state;
  esp_ip4_addr_t    ip;
} wifi_conn_info_t;

typedef void (*wifi_conn_cb_t)(wifi_conn_state_t new_state, void* user);

void wifi_conn_init(wifi_conn_cb_t cb, void* user);
void wifi_conn_start(void);
void wifi_conn_get_info(wifi_conn_info_t* out);
void wifi_conn_forget_saved(void);

#ifdef __cplusplus
}
#endif