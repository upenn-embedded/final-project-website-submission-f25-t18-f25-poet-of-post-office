#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "status_server.h"

static const char* TAG = "status_server";

#define STATUS_MODE_MAX_LEN    32
#define STATUS_VOICE_MAX_LEN   64
#define STATUS_GESTURE_MAX_LEN 32

typedef struct {
  char mode[STATUS_MODE_MAX_LEN];
  char voice[STATUS_VOICE_MAX_LEN];
  char gesture[STATUS_GESTURE_MAX_LEN];
} status_state_t;

// 当前状态（默认 unknown）
static status_state_t s_state = {
  .mode    = "unknown",
  .voice   = "none",
  .gesture = "unknown"
};

static httpd_handle_t s_httpd = NULL;

// 简单网页，轮询 /status 显示当前状态
static const char s_index_html[] =
"<!doctype html><html><head><meta charset=\"utf-8\">"
"<title>Glove Status</title>"
"<style>body{font-family:sans-serif;max-width:480px;margin:2rem auto;}h1{font-size:1.2rem;}"
"pre{background:#111;color:#0f0;padding:1rem;border-radius:8px;white-space:pre-wrap;}</style>"
"</head><body>"
"<h1>Glove status</h1>"
"<div>mode: <span id=\"mode\">-</span></div>"
"<div>detected word: <span id=\"voice\">-</span></div>"
"<div>gesture: <span id=\"gesture\">-</span></div>"
"<h2>original JSON</h2>"
"<pre id=\"raw\"></pre>"
"<script>"
"async function refresh(){"
"  try{"
"    const r = await fetch('/status');"
"    const j = await r.json();"
"    document.getElementById('mode').textContent = j.mode;"
"    document.getElementById('voice').textContent = j.voice;"
"    document.getElementById('gesture').textContent = j.gesture;"
"    document.getElementById('raw').textContent = JSON.stringify(j,null,2);"
"  }catch(e){console.error(e);}"
"}"
"setInterval(refresh, 500);"
"refresh();"
"</script>"
"</body></html>";

// GET /status -> 返回 JSON
static esp_err_t status_get_handler(httpd_req_t *req)
{
  char json[STATUS_MODE_MAX_LEN + STATUS_VOICE_MAX_LEN +
            STATUS_GESTURE_MAX_LEN + 64];

  int n = snprintf(json, sizeof(json),
                   "{\"mode\":\"%s\",\"voice\":\"%s\",\"gesture\":\"%s\"}",
                   s_state.mode, s_state.voice, s_state.gesture);
  if (n < 0) n = 0;
  if (n >= (int)sizeof(json)) n = sizeof(json) - 1;

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); // 方便你自己写网页调试
  return httpd_resp_send(req, json, n);
}

// GET / -> 返回网页
static esp_err_t index_get_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, s_index_html, sizeof(s_index_html) - 1);
}

void status_server_start(void)
{
  if (s_httpd) {
    ESP_LOGI(TAG, "status server already running");
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // 默认端口 80，和你配网页面用的是同一个端口；
  // wifi_connect.c 里在配网结束后会 stop 掉原来的 httpd，这里再起一个新的。
  esp_err_t err = httpd_start(&s_httpd, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
    s_httpd = NULL;
    return;
  }

  httpd_uri_t uri_root = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = index_get_handler,
    .user_ctx = NULL
  };
  httpd_register_uri_handler(s_httpd, &uri_root);

  httpd_uri_t uri_status = {
    .uri      = "/status",
    .method   = HTTP_GET,
    .handler  = status_get_handler,
    .user_ctx = NULL
  };
  httpd_register_uri_handler(s_httpd, &uri_status);

  ESP_LOGI(TAG, "status server started on port %d", config.server_port);
}

void status_server_stop(void)
{
  if (s_httpd) {
    httpd_stop(s_httpd);
    s_httpd = NULL;
    ESP_LOGI(TAG, "status server stopped");
  }
}

// ------- 对外的状态设置接口 -------

void status_set_mode(const char *mode)
{
  if (!mode) return;
  strncpy(s_state.mode, mode, sizeof(s_state.mode) - 1);
  s_state.mode[sizeof(s_state.mode) - 1] = '\0';
}

void status_set_voice(const char *word)
{
  if (!word) return;
  strncpy(s_state.voice, word, sizeof(s_state.voice) - 1);
  s_state.voice[sizeof(s_state.voice) - 1] = '\0';
}

void status_set_gesture(const char *gesture)
{
  if (!gesture) return;
  strncpy(s_state.gesture, gesture, sizeof(s_state.gesture) - 1);
  s_state.gesture[sizeof(s_state.gesture) - 1] = '\0';
}
