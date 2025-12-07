#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动 / 停止状态 HTTP server（在 STA 连接成功后启动）
void status_server_start(void);
void status_server_stop(void);

// 设置当前模式 / 语音识别到的词 / 手势状态
// 传入的字符串会被拷贝一份保存在模块内部
void status_set_mode(const char *mode);
void status_set_voice(const char *word);
void status_set_gesture(const char *gesture);

#ifdef __cplusplus
}
#endif
