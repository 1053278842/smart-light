#include "mqtt_ssl.h"

#define MQTT_SSL_TAG "MQTT_SSL"

#define CONFIG_BROKER_BIN_SIZE_TO_SEND 4096

// 全局MQTT客户端句柄，用于资源管理
static esp_mqtt_client_handle_t g_mqtt_client = NULL;

typedef void (*topic_handler_t)(const char *payload);

void handle_blink(const char *payload)
{
    ESP_LOGI(MQTT_SSL_TAG, "收到亮灯请求！ data: %s", payload);
    // 解析 JSON 字符串
    cJSON *root = cJSON_Parse(payload);
    if (!root)
    {
        printf("JSON 解析失败: %s\n", payload);
        return;
    }

    // freelog 字段
    cJSON *freelog = cJSON_GetObjectItem(root, "freelog");
    if (cJSON_IsBool(freelog))
    {
        printf("freelog = %s\n", cJSON_IsTrue(freelog) ? "true" : "false");
    }

    // device 数组
    cJSON *devices = cJSON_GetObjectItem(root, "device");
    if (cJSON_IsArray(devices))
    {
        int size = cJSON_GetArraySize(devices);
        for (int i = 0; i < size; i++)
        {
            cJSON *item = cJSON_GetArrayItem(devices, i);
            if (!item)
                continue;

            cJSON *id = cJSON_GetObjectItem(item, "id");
            cJSON *status = cJSON_GetObjectItem(item, "status");
            cJSON *type = cJSON_GetObjectItem(item, "type");
            cJSON *minduty = cJSON_GetObjectItem(item, "minduty");
            cJSON *maxduty = cJSON_GetObjectItem(item, "maxduty");

            printf("设备 %d: id=%d, status=%s, type=%s, minduty=%d, maxduty=%d\n",
                   i,
                   id ? id->valueint : -1,
                   (cJSON_IsTrue(status) ? "true" : "false"),
                   (cJSON_IsString(type) ? type->valuestring : "null"),
                   minduty ? minduty->valueint : -1,
                   maxduty ? maxduty->valueint : -1);
            if (!id)
            {
                printf("为传递id");
                continue;
            }
            if (cJSON_IsTrue(status))
            {
                if (cJSON_IsString(type) && type->valuestring)
                {
                    if (minduty && maxduty)
                    {
                        light_manager_set_duty_range(id ? id->valueint : -1, minduty->valueint, maxduty->valueint);
                    }
                    light_manager_control(id ? id->valueint : -1, type->valuestring);
                }
            }
            else
            {
                light_manager_control(id ? id->valueint : -1, "off");
            }
        }
    }
    else
    {
        printf("device 字段不存在或不是数组\n");
    }

    // 释放内存
    cJSON_Delete(root);
}

void handle_config(const char *payload)
{
    ESP_LOGI(MQTT_SSL_TAG, "执行 CONFIG: %s", payload);
}

struct
{
    const char *topic;
    topic_handler_t handler;
} topic_table[] = {
    {"/ll/washroom/light/light001/down/control", handle_blink},
    {"/ll/washroom/light/light001/down/blink", handle_blink},
};

static void public_online(esp_mqtt_client_handle_t client, int online)
{
    char json_buf[256];
    const char *online_str = online ? "true" : "false";

    snprintf(json_buf, sizeof(json_buf),
             "{ \"online\": %s }",
             online_str);

    esp_mqtt_client_publish(client, "ll/washroom/light/light001/up/online", json_buf, 0, 1, 1);
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(MQTT_SSL_TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(MQTT_SSL_TAG, "MQTT_EVENT_CONNECTED");
        // 上线发布online状态
        public_online(client, 1);
        // 流水灯
        msg_id = esp_mqtt_client_subscribe(client, "/ll/washroom/light/light001/down/control", 0);
        // 板载灯
        msg_id = esp_mqtt_client_subscribe(client, "/ll/washroom/light/light001/down/blink", 0);

        // led_init(); // 确保 PWM 初始化
        ESP_LOGI(MQTT_SSL_TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(MQTT_SSL_TAG, "MQTT_EVENT_DISCONNECTED");
        public_online(client, 0);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(MQTT_SSL_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(MQTT_SSL_TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(MQTT_SSL_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(MQTT_SSL_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(MQTT_SSL_TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        char *topic = strndup(event->topic, event->topic_len);
        char *payload = strndup(event->data, event->data_len);

        for (int i = 0; i < sizeof(topic_table) / sizeof(topic_table[0]); i++)
        {
            if (strcmp(topic, topic_table[i].topic) == 0)
            {
                topic_table[i].handler(payload); // 调用对应的处理函数
                break;
            }
        }
        // 解析 JSON 字符串，执行 "freelog" 打印内存功能
        cJSON *root = cJSON_Parse(payload);
        if (root)
        {
            cJSON *freelog = cJSON_GetObjectItem(root, "freelog");
            if (cJSON_IsBool(freelog))
            {
                ESP_LOGI(MQTT_SSL_TAG, "Free heap: %d bytes", esp_get_free_heap_size());
            }
        }
        // 释放内存
        cJSON_Delete(root);
        free(topic);
        free(payload);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(MQTT_SSL_TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGI(MQTT_SSL_TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(MQTT_SSL_TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(MQTT_SSL_TAG, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGI(MQTT_SSL_TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        }
        else
        {
            ESP_LOGW(MQTT_SSL_TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(MQTT_SSL_TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void start_mqtt_ssl(void)
{
    // 如果已存在客户端，先清理
    if (g_mqtt_client != NULL)
    {
        esp_mqtt_client_stop(g_mqtt_client);
        esp_mqtt_client_destroy(g_mqtt_client);
        g_mqtt_client = NULL;
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_SSL_URL, // MQTT 服务器地址（如 "mqtts://121.36.251.16:8883"）

        // --------------------------
        // 核心：配置 Broker 身份验证（验证服务器）
        // --------------------------
        .broker.verification = {
            .use_global_ca_store = false,                      // 不使用全局 CA 存储（手动指定 CA 证书）
            .certificate = (const char *)MQTT_SSL_CA_CERT_PEM, // 指向 CA 证书数据（强制转换为 char*，因结构体要求）
            .skip_cert_common_name_check = false               // 不跳过服务器证书 CN 校验（建议保持 false，提升安全性）
        },
        .session.last_will = {.topic = "ll/washroom/light/light001/up/online", .msg = "{ \"online\": false }", .msg_len = 0, .qos = 1, .retain = 1}};
    ESP_LOGI(MQTT_SSL_TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_mqtt_client);
}

void stop_mqtt_ssl(void)
{
    if (g_mqtt_client != NULL)
    {
        ESP_LOGI(MQTT_SSL_TAG, "Stopping MQTT client and freeing resources");
        esp_mqtt_client_stop(g_mqtt_client);
        esp_mqtt_client_destroy(g_mqtt_client);
        g_mqtt_client = NULL;
    }
}