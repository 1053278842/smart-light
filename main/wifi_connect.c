/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#define WIFI_NAMESPACE "wifi_config"

static const char *TAG = "wifi_connect";
// 定义事件组
const int WIFI_CONNECTED_EVENT = BIT0; // Station连接成功事件
const int SSID_RECIVE_EVENT = BIT1;    // Station连接成功事件
const int WIFI_FAIL_EVENT = BIT2;      // Station连接成功事件
static EventGroupHandle_t wifi_event_group;

// 全局变量存储接收的WiFi凭证
static char target_ssid[32] = {0};
static char target_pass[64] = {0};

static bool credentials_received = false;
static bool isFirstConfig = false;

// 重试次数
int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI就绪,等待连接...");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            // 连接断开-尝试重试
            if (s_retry_num < 3)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "连接失败，正在重试 %d/3", s_retry_num);
            }
            else
            {
                // 设置事件组-WIFI断开
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_EVENT);
            }
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "SoftAP transport: Started!");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "SoftAP transport: Connected!");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "SoftAP transport: Disconnected!");
            break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
}

static void initNetifAp(void)
{
    ESP_ERROR_CHECK(esp_netif_init());                // 初始化网络接口，不写网络通信可能失败
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // 创建默认事件循环，即广播
    esp_netif_create_default_wifi_ap();               // 创建AP DHCP，sta类型同函数，后缀ap->sta
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

static void initNvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "SoftAp_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

static void start_wifi(void)
{
    char service_name[14];
    get_device_service_name(service_name, sizeof(service_name));
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(service_name),
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN, // 无密码
            .max_connection = 4,        // 最大连接数
            .beacon_interval = 100,     // 信标间隔
        },
    };
    memcpy(ap_config.ap.ssid, service_name, strlen(service_name));

    // 配置 STA（从全局 target_ssid / target_pass）
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, target_ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, target_pass, sizeof(sta_config.sta.password));

    // 设置成 APSTA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 主动connect，第一次肯定报错。主要是防止APSTA已启动的情况下，http请求过来并修改参数时,由于已经sta_start过了，不能再触发connect
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %d", err);
    }
}

// 处理配网请求的回调函数
static esp_err_t config_handler(httpd_req_t *req)
{
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        ESP_LOGI(TAG, "Found URL query => %s", query);

        char ssid[32] = {0};
        char pass[64] = {0};

        // 解析参数
        if (httpd_query_key_value(query, "ssid", ssid, sizeof(ssid)) == ESP_OK &&
            httpd_query_key_value(query, "password", pass, sizeof(pass)) == ESP_OK)
        {
            strncpy(target_ssid, ssid, sizeof(target_ssid) - 1);
            strncpy(target_pass, pass, sizeof(target_pass) - 1);
            credentials_received = true;

            ESP_LOGI(TAG, "收到WiFi凭证: SSID=%s, 密码=%s", target_ssid, target_pass);
            xEventGroupSetBits(wifi_event_group, SSID_RECIVE_EVENT);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get query string");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }

    httpd_resp_send(req, "Connected ...... If the current WiFi is turned off, it means a successful . Otherwise, the parameters need to be modified", HTTPD_RESP_USE_STRLEN); // 响应客户端
    return ESP_OK;
}

// 注册HTTP路由
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        // 注册POST /config接口
        httpd_uri_t config_uri = {
            .uri = "/config",
            .method = HTTP_GET,
            .handler = config_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &config_uri);
    }
    return server;
}

static void save_wifi_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_set_str(handle, "ssid", ssid);
        nvs_set_str(handle, "password", pass);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Saved WiFi credentials to NVS");
    }
}
static bool load_wifi_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return false;

    size_t ssid_len = sizeof(target_ssid);
    size_t pass_len = sizeof(target_pass);
    err = nvs_get_str(handle, "ssid", target_ssid, &ssid_len);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return false;
    }
    err = nvs_get_str(handle, "password", target_pass, &pass_len);
    nvs_close(handle);
    return (err == ESP_OK);
}
void app_main(void)
{
    // ESP_ERROR_CHECK(nvs_flash_erase());
    // 1. 初始化 NVS
    initNvs();

    // 2. 初始化网络接口和事件
    initNetifAp();

    bool has_credentials = load_wifi_credentials();
    if (has_credentials)
    {
        ESP_LOGI(TAG, "加载存储的WiFi凭证: SSID=%s, 密码=%s", target_ssid, target_pass);
        // 已配过，但是需要判断是否能连接成功
    }
    else
    {
        ESP_LOGI(TAG, "没有存储的WiFi凭证");
        // 第一次配网，启动AP和Web配置
        isFirstConfig = true; // 标记为第一次配置
    }
    // 3. 启动 WiFi 和 Web 服务器
    start_wifi();
    httpd_handle_t service = start_webserver();

    wifi_event_group = xEventGroupCreate(); // 创建事件组
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    // 进入循环等待 STA 连接或失败
    while (1)
    {
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group,
            WIFI_CONNECTED_EVENT | WIFI_FAIL_EVENT | SSID_RECIVE_EVENT,
            pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & WIFI_CONNECTED_EVENT)
        {
            s_retry_num = 0; // 重置重试次数
            ESP_LOGI(TAG, "STA 连接成功");
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // 切换到 STA
            httpd_stop(service);                               // 停止 Web 服务器
            save_wifi_credentials(target_ssid, target_pass);   // 保存成功凭证
            break;                                             // 连接成功，退出循环
        }

        if (bits & WIFI_FAIL_EVENT)
        {
            ESP_LOGW(TAG, "STA 连接失败，启动 AP 模式等待配置");
            s_retry_num = 0;

            // 清除已接收凭证
            credentials_received = false;
            memset(target_ssid, 0, sizeof(target_ssid));
            memset(target_pass, 0, sizeof(target_pass));
        }

        if (bits & SSID_RECIVE_EVENT)
        {
            ESP_LOGI(TAG, "尝试连接SSID: %s,PASS: %s", target_ssid, target_pass);
            wifi_config_t sta_config = {0};
            strncpy((char *)sta_config.sta.ssid, target_ssid, sizeof(sta_config.sta.ssid));
            strncpy((char *)sta_config.sta.password, target_pass, sizeof(sta_config.sta.password));

            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config)); // 更新配置
            esp_wifi_connect();                                             // 切换到Station模式并连接
        }
    }

    ESP_LOGI(TAG, "WiFi 配网完成，进入主程序逻辑");
}
