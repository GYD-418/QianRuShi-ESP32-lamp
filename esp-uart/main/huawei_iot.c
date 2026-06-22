#include "huawei_iot.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "device_ctrl.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "huawei_iot_config.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "huawei_iot";

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_URI_BUF_SIZE  160
#define TOPIC_BUF_SIZE     192
#define PAYLOAD_BUF_SIZE   512

static EventGroupHandle_t s_wifi_event_group;
static esp_mqtt_client_handle_t s_mqtt_client;
static bool s_mqtt_ready;

static bool json_to_bool(const cJSON *item, bool *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint != 0;
        return true;
    }
    if (cJSON_IsString(item)) {
        const char *v = item->valuestring;
        *out = (strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0 ||
                strcmp(v, "1") == 0 || strcmp(v, "开") == 0 || strcmp(v, "开灯") == 0);
        return true;
    }
    return false;
}

static bool apply_lamp(bool on)
{
    char line[24];
    snprintf(line, sizeof(line), "CMD:LAMP_%s", on ? "ON" : "OFF");
    return device_ctrl_handle_line(line);
}

static void report_lamp_property(bool on)
{
    char topic[TOPIC_BUF_SIZE];
    char payload[PAYLOAD_BUF_SIZE];

    if (!s_mqtt_ready || s_mqtt_client == NULL) {
        return;
    }

    /* 物模型 lamp / lamp-stat：1=开 0=关（与华为云控制台一致） */
    int stat = on ? 1 : 0;
    snprintf(topic, sizeof(topic), "$oc/devices/%s/sys/properties/report", HUAWEI_IOT_DEVICE_ID);
    snprintf(payload, sizeof(payload),
             "{\"services\":[{\"service_id\":\"%s\",\"properties\":{\"lamp\":%d}}]}",
             HUAWEI_IOT_SERVICE_ID, stat);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 0, 0);
    ESP_LOGI(TAG, "report lamp=%d (msg_id=%d)", stat, msg_id);
}

static void on_device_state_changed(const char *name, bool on)
{
    if (name != NULL && strcmp(name, "lamp") == 0) {
        report_lamp_property(on);
    }
}

static bool parse_lamp_from_properties(const cJSON *props, bool *out_on)
{
    const cJSON *lamp = cJSON_GetObjectItem(props, "lamp");
    if (cJSON_IsNumber(lamp)) {
        *out_on = (lamp->valueint != 0);
        return true;
    }
    return json_to_bool(lamp, out_on);
}

/** 物模型命令 lamp_control，下发参数 lamp-stat：1=开 0=关 */
static bool lamp_stat_to_on(int stat)
{
    return stat != 0;
}

static bool handle_lamp_control_paras(const cJSON *paras)
{
    const cJSON *lamp_stat = cJSON_GetObjectItem(paras, "lamp-stat");
    if (!cJSON_IsNumber(lamp_stat)) {
        return false;
    }
    return apply_lamp(lamp_stat_to_on(lamp_stat->valueint));
}

static bool handle_properties_set_json(cJSON *root)
{
    bool handled = false;
    bool on = false;

    const cJSON *lamp_top = cJSON_GetObjectItem(root, "lamp");
    if (json_to_bool(lamp_top, &on)) {
        return apply_lamp(on);
    }

    const cJSON *services = cJSON_GetObjectItem(root, "services");
    if (!cJSON_IsArray(services)) {
        return false;
    }

    cJSON *svc = NULL;
    cJSON_ArrayForEach(svc, services) {
        if (!cJSON_IsObject(svc)) {
            continue;
        }
        const cJSON *sid = cJSON_GetObjectItem(svc, "service_id");
        const cJSON *props = cJSON_GetObjectItem(svc, "properties");
        if (!cJSON_IsObject(props)) {
            continue;
        }
        if (cJSON_IsString(sid) && strcmp(sid->valuestring, HUAWEI_IOT_SERVICE_ID) != 0) {
            continue;
        }
        if (parse_lamp_from_properties(props, &on)) {
            handled = apply_lamp(on) || handled;
        }
    }
    return handled;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi got IP");
    }
}

static esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi connecting to \"%s\"...", CONFIG_WIFI_SSID);
    return xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                               pdMS_TO_TICKS(30000)) & WIFI_CONNECTED_BIT ? ESP_OK : ESP_FAIL;
}

static bool try_exec_cmd_line(const char *cmd)
{
    char line[64];
    if (cmd == NULL) {
        return false;
    }
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    return device_ctrl_handle_line(line);
}

static bool handle_command_paras(const cJSON *paras)
{
    bool on = false;

    if (!cJSON_IsObject(paras)) {
        return false;
    }

    if (handle_lamp_control_paras(paras)) {
        return true;
    }

    if (json_to_bool(cJSON_GetObjectItem(paras, "lamp"), &on)) {
        return apply_lamp(on);
    }

    const cJSON *lamp_num = cJSON_GetObjectItem(paras, "lamp");
    if (cJSON_IsNumber(lamp_num)) {
        return apply_lamp(lamp_stat_to_on(lamp_num->valueint));
    }

    const cJSON *dev = cJSON_GetObjectItem(paras, "device");
    const cJSON *on_num = cJSON_GetObjectItem(paras, "on");
    if (cJSON_IsString(dev) && cJSON_IsNumber(on_num)) {
        char line[48];
        snprintf(line, sizeof(line), "CMD:%s_%s", dev->valuestring, on_num->valueint ? "ON" : "OFF");
        for (char *p = line + 4; *p; p++) {
            if (*p >= 'a' && *p <= 'z') {
                *p = (char)(*p - 'a' + 'A');
            }
        }
        return try_exec_cmd_line(line);
    }

    if (json_to_bool(cJSON_GetObjectItem(paras, "on"), &on) ||
        json_to_bool(cJSON_GetObjectItem(paras, "value"), &on)) {
        return apply_lamp(on);
    }

    const cJSON *switch_v = cJSON_GetObjectItem(paras, "switch");
    if (cJSON_IsString(switch_v)) {
        return try_exec_cmd_line(
            strcmp(switch_v->valuestring, "ON") == 0 ? "CMD:LAMP_ON" : "CMD:LAMP_OFF");
    }

    return false;
}

/** 华为云 sys/commands 下发，控制 lamp；*lamp_control_cmd 表示需带 lamp-control 响应 */
static bool handle_cloud_command_json(const char *payload, int len, bool *lamp_control_cmd)
{
    char buf[PAYLOAD_BUF_SIZE];
    cJSON *root = NULL;
    bool handled = false;

    if (lamp_control_cmd != NULL) {
        *lamp_control_cmd = false;
    }

    if (len <= 0 || len >= (int)sizeof(buf)) {
        return false;
    }
    memcpy(buf, payload, len);
    buf[len] = '\0';

    root = cJSON_Parse(buf);
    if (root == NULL) {
        if (strstr(buf, "CMD:") != NULL || strstr(buf, "开灯") != NULL || strstr(buf, "关灯") != NULL) {
            return try_exec_cmd_line(buf);
        }
        return false;
    }

    const cJSON *cmd_name = cJSON_GetObjectItem(root, "command_name");
    const cJSON *paras = cJSON_GetObjectItem(root, "paras");

    if (cJSON_IsString(cmd_name)) {
        const char *name = cmd_name->valuestring;
        char line[48];

        if (strcasecmp(name, "lamp_control") == 0) {
            if (lamp_control_cmd != NULL) {
                *lamp_control_cmd = true;
            }
            handled = handle_lamp_control_paras(paras);
        }

        if (!handled) {
            snprintf(line, sizeof(line), "CMD:%s", name);
            handled = try_exec_cmd_line(line);
        }

        if (!handled) {
            if (strcasecmp(name, "lamp_on") == 0 || strcasecmp(name, "turn_on") == 0) {
                handled = apply_lamp(true);
            } else if (strcasecmp(name, "lamp_off") == 0 || strcasecmp(name, "turn_off") == 0) {
                handled = apply_lamp(false);
            } else if (strcasecmp(name, "lamp") == 0) {
                handled = handle_command_paras(paras);
            }
        }
    }

    if (!handled) {
        handled = handle_command_paras(paras);
    }

    cJSON_Delete(root);
    return handled;
}

static bool handle_properties_set_payload(const char *payload, int len)
{
    char buf[PAYLOAD_BUF_SIZE];
    cJSON *root = NULL;
    bool handled = false;

    if (len <= 0 || len >= (int)sizeof(buf)) {
        return false;
    }
    memcpy(buf, payload, len);
    buf[len] = '\0';

    root = cJSON_Parse(buf);
    if (root == NULL) {
        return false;
    }

    handled = handle_properties_set_json(root);
    cJSON_Delete(root);
    return handled;
}

static void mqtt_publish_response(const char *topic, int topic_len, bool ok, bool lamp_control_resp)
{
    char resp_topic[TOPIC_BUF_SIZE];
    char body[PAYLOAD_BUF_SIZE];
    char request_id[64] = { 0 };
    const char *req_key = "request_id=";

    if (topic_len >= (int)sizeof(resp_topic)) {
        return;
    }
    memcpy(resp_topic, topic, topic_len);
    resp_topic[topic_len] = '\0';

    char *req = strstr(resp_topic, req_key);
    if (req == NULL) {
        return;
    }
    req += strlen(req_key);
    char *end = strchr(req, '/');
    if (end) {
        *end = '\0';
    }
    strncpy(request_id, req, sizeof(request_id) - 1);

    if (strstr(resp_topic, "/sys/commands/") != NULL) {
        snprintf(resp_topic, sizeof(resp_topic),
                 "$oc/devices/%s/sys/commands/response/request_id=%s",
                 HUAWEI_IOT_DEVICE_ID, request_id);
    } else if (strstr(resp_topic, "/sys/properties/set") != NULL) {
        snprintf(resp_topic, sizeof(resp_topic),
                 "$oc/devices/%s/sys/properties/set/response/request_id=%s",
                 HUAWEI_IOT_DEVICE_ID, request_id);
    } else {
        return;
    }

    if (ok && lamp_control_resp) {
        bool on = false;
        device_ctrl_get_state("lamp", &on);
        snprintf(body, sizeof(body),
                 "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\","
                 "\"paras\":{\"lamp-control\":%d}}",
                 on ? 1 : 0);
    } else if (ok) {
        snprintf(body, sizeof(body),
                 "{\"result_code\":0,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{}}");
    } else {
        snprintf(body, sizeof(body),
                 "{\"result_code\":1,\"response_name\":\"COMMAND_RESPONSE\",\"paras\":{}}");
    }
    esp_mqtt_client_publish(s_mqtt_client, resp_topic, body, 0, 0, 0);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    esp_mqtt_event_handle_t event = data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to Huawei Cloud");
        s_mqtt_ready = true;
        {
            char topic[TOPIC_BUF_SIZE];
            snprintf(topic, sizeof(topic), "$oc/devices/%s/sys/commands/#", HUAWEI_IOT_DEVICE_ID);
            esp_mqtt_client_subscribe(s_mqtt_client, topic, 0);
            snprintf(topic, sizeof(topic), "$oc/devices/%s/sys/properties/set/#", HUAWEI_IOT_DEVICE_ID);
            esp_mqtt_client_subscribe(s_mqtt_client, topic, 0);

            bool lamp_on = false;
            if (device_ctrl_get_state("lamp", &lamp_on)) {
                report_lamp_property(lamp_on);
            }
        }
        break;
    case MQTT_EVENT_DATA: {
        bool is_property = (event->topic_len > 0 &&
                            strstr(event->topic, "/sys/properties/set") != NULL);
        bool lamp_control_cmd = false;
        bool ok = is_property ? handle_properties_set_payload(event->data, event->data_len)
                              : handle_cloud_command_json(event->data, event->data_len,
                                                          &lamp_control_cmd);
        if (ok) {
            ESP_LOGI(TAG, "%s executed", is_property ? "cloud property set" : "cloud command");
        } else {
            ESP_LOGW(TAG, "cloud payload (%s): %.*s", is_property ? "property" : "command",
                     event->data_len, event->data);
        }
        mqtt_publish_response(event->topic, event->topic_len, ok,
                              !is_property && lamp_control_cmd);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_mqtt_ready = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    char uri[MQTT_URI_BUF_SIZE];
    snprintf(uri, sizeof(uri), "mqtts://%s:%d", HUAWEI_IOT_HOST, HUAWEI_IOT_PORT);

    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = { .uri = uri },
            .verification = { .crt_bundle_attach = esp_crt_bundle_attach },
        },
        .credentials = {
            .username = HUAWEI_IOT_USERNAME,
            .client_id = HUAWEI_IOT_CLIENT_ID,
            .authentication = { .password = HUAWEI_IOT_PASSWORD },
        },
        .session = { .keepalive = 120 },
    };

    s_mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

static void huawei_iot_task(void *arg)
{
    (void)arg;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed, check menuconfig WIFI_SSID/PASSWORD");
        vTaskDelete(NULL);
        return;
    }

    mqtt_start();
    vTaskDelete(NULL);
}

void huawei_iot_start(void)
{
    device_ctrl_set_state_callback(on_device_state_changed);
    xTaskCreate(huawei_iot_task, "huawei_iot", 8192, NULL, 5, NULL);
}
