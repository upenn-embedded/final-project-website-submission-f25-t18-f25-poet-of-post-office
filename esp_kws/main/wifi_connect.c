#include "wifi_connect.h"

static const char* TAG = "wifi_conn";

#define AP_SSID "CatFeeder_Setup"
#define AP_PASS "12345678"
#define STA_TRY_MS 8000 // 每次尝试连接的最长时间
#define MAX_SSID 256
#define MAX_PASS 256

// 待切换的 SSID/密码缓存（由 /provision 填，wifi_switch_task 读取）
static char g_pending_ssid[MAX_SSID + 1] = {0};
static char g_pending_pass[MAX_PASS + 1] = {0};

// 全局上下文，集中保存 Wi-Fi/HTTP 状态
typedef struct {
  wifi_conn_state_t state;      // 当前连接状态（STA尝试中/STA成功/SoftAP）
  esp_ip4_addr_t ip;            // 如果 STA 成功，存储获取到的 IP
  wifi_conn_cb_t cb;            // 状态变化时的回调函数
  void* cb_user;                // 回调的用户数据
  httpd_handle_t httpd;         // SoftAP 模式下启动的 HTTP server 句柄
  EventGroupHandle_t evt;       // FreeRTOS EventGroup，用于等待“是否连上IP”
} wifi_ctx_t;

static wifi_ctx_t g = {0};      // 全局唯一实例
#define WIFI_OK_BIT BIT0        // EventGroup 里的标志位：表示 STA 成功拿到 IP

//wifi账号密码存取
//保存SSID/密码到 NVS
static void nvs_save_creds(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open("wifi",NVS_READWRITE,&h)!=ESP_OK) {
        ESP_LOGE(TAG,"Failed to open NVS namespace 'wifi'");
        return;
    }
    nvs_set_str(h,"ssid",ssid);
    nvs_set_str(h,"pass",pass);
    nvs_commit(h);
    nvs_close(h);
}

// 从 NVS 读取 SSID/密码
static bool nvs_load_creds(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz) {
    nvs_handle_t h;
    if (nvs_open("wifi",NVS_READONLY,&h)!=ESP_OK) {
        ESP_LOGW(TAG,"No NVS namespace 'wifi'");
        return false;
    }
    size_t ssid_len=ssid_sz, pass_len=pass_sz;
    esp_err_t e1 = nvs_get_str(h,"ssid",ssid,&ssid_len);
    esp_err_t e2 = nvs_get_str(h,"pass",pass,&pass_len);
    nvs_close(h);
    return (e1==ESP_OK && e2==ESP_OK && ssid[0]);
}

// 清除 NVS 里保存的 SSID/密码
static void nvs_erase_creds(void) {
    nvs_handle_t h;
    if (nvs_open("wifi",NVS_READWRITE,&h)!=ESP_OK) {
        ESP_LOGW(TAG,"No NVS namespace 'wifi'");
        return;
    }
    nvs_erase_key(h,"ssid");
    nvs_erase_key(h,"pass");
    nvs_commit(h);
    nvs_close(h);
}


//--------SoftAP/STA模式切换相关--------//
// 启动 SoftAP 模式
static void start_ap(void){
    wifi_config_t ap={0};
    strcpy((char*)ap.ap.ssid,AP_SSID);
    strcpy((char*)ap.ap.password,AP_PASS);
    ap.ap.ssid_len=strlen((char*)ap.ap.ssid);
    ap.ap.authmode=WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection=4;
    ap.ap.channel=1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP: SSID=%s PASS=%s (IP 192.168.4.1)", AP_SSID, AP_PASS);
    g.state = WIFI_CONN_AP_PORTAL; 
    if (g.cb) g.cb(g.state, g.cb_user);
}
static void test_esp_event_cb(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if (event_base==WIFI_EVENT && event_id==WIFI_EVENT_STA_DISCONNECTED){
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGW(TAG, "STA_DISCONNECTED reason=%d", e->reason);
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base==IP_EVENT && event_id==IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t* e = (ip_event_got_ip_t*)event_data;
        g.ip = e->ip_info.ip;
        xEventGroupSetBits(g.evt, WIFI_OK_BIT);
        ESP_LOGI(TAG, "GOT_IP " IPSTR, IP2STR(&g.ip));
    }
}

//尝试启动STA模式连接
static bool try_sta_once(const char* ssid, const char* pass, int wait_ms){
    esp_event_handler_instance_t wifi_event_handler;
    esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,test_esp_event_cb,NULL,&wifi_event_handler);
    esp_event_handler_instance_t ip_event_handler;
    esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,test_esp_event_cb,NULL,&ip_event_handler);
    wifi_config_t sta={0};
    strcpy((char*)sta.sta.ssid, ssid);
    strcpy((char*)sta.sta.password, pass);
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    sta.sta.scan_method = WIFI_FAST_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "STA: Connecting to SSID=%s ...", ssid);
    xEventGroupClearBits(g.evt, WIFI_OK_BIT);
    ESP_ERROR_CHECK(esp_wifi_connect());
    EventBits_t bits = xEventGroupWaitBits(g.evt, WIFI_OK_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(wait_ms));
    return (bits & WIFI_OK_BIT);
}

//-------配网HTTP页面相关-------//

static httpd_handle_t http_start(void);
static void http_stop(void);
static void wifi_switch_task(void* arg);

static const char *HTML_INDEX =
"<!doctype html><meta charset=utf-8>"
"<title>CatFeeder Wi-Fi Setup</title>"
"<body style='font-family:sans-serif;max-width:480px;margin:2rem auto'>"
"<h2>Connect to Wi-Fi</h2>"
"<label>SSID：<select id=s></select></label><br><br>"
"<label>Password：<input id=p type=password style='width:100%'></label><br><br>"
"<button onclick='save()'>connect</button>"
"<pre id=l></pre>"
"<script>"
"async function load(){let r=await fetch('/scan');let a=await r.json();let s=document.getElementById('s');a.forEach(x=>{let o=document.createElement('option');o.text=x;s.add(o);});}"
"async function save(){let b=JSON.stringify({ssid:document.getElementById('s').value,password:document.getElementById('p').value});"
"let r=await fetch('/provision',{method:'POST',headers:{'Content-Type':'application/json'},body:b});document.getElementById('l').textContent=await r.text();}"
"load();</script>";

static esp_err_t index_get(httpd_req_t *req){
    httpd_resp_set_type(req,"text/html");
    httpd_resp_send(req,HTML_INDEX,strlen(HTML_INDEX));
    return ESP_OK;
}
static esp_err_t scan_get(httpd_req_t *req){
    wifi_scan_config_t sc = {.ssid=NULL,.bssid=NULL,.channel=0,.show_hidden=false,
                           .scan_type=WIFI_SCAN_TYPE_ACTIVE,
                           .scan_time={.active={.min=80,.max=200}}
                        };
    

    ESP_ERROR_CHECK(esp_wifi_scan_start(&sc,true));
    uint16_t n=0; 
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&n));
    wifi_ap_record_t *aps=calloc(n,sizeof(*aps));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&n,aps));
    char *buf = malloc(1024); // 足够大
    size_t off=0; int added=0; buf[off++]='[';
    for (int i=0; i<n; ++i){
        if (aps[i].primary<1 || aps[i].primary>14) continue;    // 只要 2.4G
        const char* s=(const char*)aps[i].ssid; if(!s||!s[0]) continue;
        bool dup=false;
        for (int j=0;j<i;j++){
            if(strcmp((char*)aps[j].ssid,s)==0 && aps[j].primary<=14){ dup=true; break; }}
            if (dup) continue;
            if (off+strlen(s)+4>=1000) break; // 防止溢出
            if (added++) buf[off++]=',';
            off+=sprintf(buf+off,"\"%s\"",s);
    }
    buf[off++]=']'; buf[off]=0;
    httpd_resp_set_type(req,"application/json");
    httpd_resp_send(req,buf,off);
    free(aps); free(buf);
    return ESP_OK;
}

static esp_err_t provision_post (httpd_req_t *req){
    char body[256]; 
    size_t need=req->content_len; 
    if (need>255) need=255;
    size_t got=0; 
    while(got<need){ 
        int r=httpd_req_recv(req, body+got, need-got); 
        if(r<=0){ 
            httpd_resp_send_err(req,400,"recv"); 
            return ESP_OK;
        } 
        got+=r; 
    }
    body[got]=0;
    ESP_LOGI(TAG, "provision_post body=%s", body);

    char ssid[MAX_SSID+1]={0}, pass[MAX_PASS+1]={0};
    char* ps=strstr(body,"\"ssid\""); 
    char* pp=strstr(body,"\"password\"");
    ESP_LOGI(TAG, "ps head: %.30s", ps);
    ESP_LOGI(TAG, "pp head: %.30s", pp);

    if(!ps||!pp){ 
        httpd_resp_send_err(req,400,"bad json"); 
        return ESP_OK;
    }
    int n1 = sscanf(ps, "\"ssid\" : \"%32[^\"]\"", ssid);
    int n2 = sscanf(pp, "\"password\" : \"%64[^\"]\"", pass);

    ESP_LOGI(TAG, "sscanf ret: ssid=%d pass=%d", n1, n2);

    if(!ssid[0]){ 
        httpd_resp_send_err(req,400,"empty ssid");
        return ESP_OK; 
    }

    nvs_save_creds(ssid, pass);
    httpd_resp_set_type(req,"text/plain");
    httpd_resp_sendstr(req, "OK: connecting...\nSwitch back to your home Wi-Fi.");
    
    // 切回 STA
    strncpy(g_pending_ssid, ssid, sizeof(g_pending_ssid) - 1);
    strncpy(g_pending_pass, pass, sizeof(g_pending_pass) - 1);
    BaseType_t created = xTaskCreate(
        wifi_switch_task,    // 任务函数
        "wifi_switch",       // 任务名（调试可见）
        4096,                // 栈大小（字节数，够用了；若 TLS/更多日志可加大）
        NULL,                // 参数
        5,                   // 优先级（略高于 httpd 默认优先级即可）
        NULL                 // 不需要拿句柄
    );
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi_switch task");
    // 兜底：如果任务没创建成功，至少不要在 httpd 线程里 stop
    
    }
    return ESP_OK;
}



static httpd_handle_t http_start(void){
    if (g.httpd) return g.httpd; // 已经启动
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&g.httpd,&config)==ESP_OK){
        httpd_uri_t u1={.uri="/", .method=HTTP_GET, .handler=index_get};
        httpd_uri_t u2={.uri="/scan", .method=HTTP_GET, .handler=scan_get};
        httpd_uri_t u3={.uri="/provision", .method=HTTP_POST, .handler=provision_post};
        httpd_register_uri_handler(g.httpd,&u1);
        httpd_register_uri_handler(g.httpd,&u2);
        httpd_register_uri_handler(g.httpd,&u3);
        ESP_LOGI(TAG,"HTTP started :80");
    }
    return g.httpd;
}
static void http_stop(void){
    if (g.httpd){
        ESP_LOGI(TAG,"HTTP stopping ..."); 
        httpd_stop(g.httpd); 
        g.httpd=NULL; 
        ESP_LOGI(TAG,"HTTP stopped"); 
        
    }
}


static void wifi_switch_task(void* arg){
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGI(TAG,"Switching to STA mode SSID=%s ...", g_pending_ssid);
    http_stop();            // 停止配网HTTP（避免端口冲突）
    esp_wifi_stop();
    if (try_sta_once(g_pending_ssid, g_pending_pass, 10000)) {
        g.state = WIFI_CONN_STA_OK;
        ESP_LOGI(TAG, "STA connected, IP=" IPSTR, IP2STR(&g.ip));
        if (g.cb) g.cb(g.state, g.cb_user);
        // 可选：此处启动“正常工作”的 HTTP（/ping /echo …）
        // start_http_server_normal();
    } else {
        ESP_LOGW(TAG, "Connect failed, back to AP portal");
        start_ap();
        http_start();  // 重新开启配网页面
    }

    // 4) 清理并退出任务
    memset(g_pending_ssid, 0, sizeof(g_pending_ssid));
    memset(g_pending_pass, 0, sizeof(g_pending_pass));
    vTaskDelete(NULL);

}


// ---- 事件 ----
static void on_got_ip(void* arg, esp_event_base_t base, int32_t id, void* data){
  ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
  g.ip = e->ip_info.ip;
  xEventGroupSetBits(g.evt, WIFI_OK_BIT);
  ESP_LOGI(TAG, "GOT_IP " IPSTR, IP2STR(&g.ip));
}
static void on_sta_disc(void* arg, esp_event_base_t base, int32_t id, void* data){
  wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t*)data;
  ESP_LOGW(TAG, "STA_DISCONNECTED reason=%d", e->reason);
}

//-------对外API-------//
void wifi_conn_init(wifi_conn_cb_t cb, void* user){
  g.cb = cb; g.cb_user = user; g.state = WIFI_CONN_IDLE;
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  g.evt = xEventGroupCreate();
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL);
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_sta_disc, NULL);
}

void wifi_conn_start(void){
  g.state = WIFI_CONN_STA_TRY; if (g.cb) g.cb(g.state, g.cb_user);

  char ssid[MAX_SSID+1]={0}, pass[MAX_PASS+1]={0};
  bool has = nvs_load_creds(ssid,sizeof(ssid), pass,sizeof(pass));
  if (!has) {
    // 没有已保存凭据 -> 直接AP配网
    start_ap();
    http_start();
    return;
  }

  // 用已保存凭据尝试连网
  if (try_sta_once(ssid, pass, STA_TRY_MS)) {
    g.state = WIFI_CONN_STA_OK; if (g.cb) g.cb(g.state, g.cb_user);
  } else {
    start_ap();
    http_start();
  }
}

void wifi_conn_get_info(wifi_conn_info_t* out){
  if (!out) return;
  out->state = g.state;
  out->ip = g.ip;
}
