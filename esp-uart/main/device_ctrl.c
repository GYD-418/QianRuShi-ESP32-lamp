#include "device_ctrl.h"

#include <strings.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "device_ctrl";

typedef struct {
    const char *name;
    gpio_num_t gpio;
    bool state;
} device_t;

static device_ctrl_state_cb_t s_state_cb;

static device_t s_devices[] = {
    { "lamp",   DEVICE_LAMP_GPIO,   false },
    { "fan",    DEVICE_FAN_GPIO,    false },
    { "relay1", DEVICE_RELAY1_GPIO, false },
    { "relay2", DEVICE_RELAY2_GPIO, false },
    { "buzzer", DEVICE_BUZZER_GPIO, false },
    { "pump",   DEVICE_PUMP_GPIO,   false },
};

static device_t *find_device(const char *name)
{
    for (size_t i = 0; i < sizeof(s_devices) / sizeof(s_devices[0]); i++) {
        if (strcmp(s_devices[i].name, name) == 0) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static void set_device(device_t *dev, bool on)
{
    if (dev->state == on) {
        return;
    }
    dev->state = on;
    gpio_set_level(dev->gpio, on ? 1 : 0);
    ESP_LOGI(TAG, "%s %s", dev->name, on ? "ON" : "OFF");
    if (s_state_cb != NULL) {
        s_state_cb(dev->name, on);
    }
}

void device_ctrl_set_state_callback(device_ctrl_state_cb_t cb)
{
    s_state_cb = cb;
}

bool device_ctrl_get_state(const char *name, bool *out_on)
{
    device_t *dev = find_device(name);
    if (dev == NULL || out_on == NULL) {
        return false;
    }
    *out_on = dev->state;
    return true;
}

void device_ctrl_init(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    uint64_t mask = 0;
    for (size_t i = 0; i < sizeof(s_devices) / sizeof(s_devices[0]); i++) {
        mask |= (1ULL << s_devices[i].gpio);
    }
    cfg.pin_bit_mask = mask;
    ESP_ERROR_CHECK(gpio_config(&cfg));

    for (size_t i = 0; i < sizeof(s_devices) / sizeof(s_devices[0]); i++) {
        gpio_set_level(s_devices[i].gpio, 0);
    }
    ESP_LOGI(TAG, "devices ready (lamp/fan/relay1/relay2/buzzer/pump)");
}

static bool handle_cmd_device(const char *device, bool turn_on)
{
    device_t *dev = find_device(device);
    if (dev == NULL) {
        return false;
    }
    set_device(dev, turn_on);
    return true;
}

static bool match_alias(const char *line, const char *alias, const char *device, bool turn_on)
{
    if (strcmp(line, alias) == 0) {
        return handle_cmd_device(device, turn_on);
    }
    return false;
}

bool device_ctrl_handle_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return false;
    }

    /* 协议: CMD:设备_ON / CMD:设备_OFF，例如 CMD:LAMP_ON */
    if (strncmp(line, "CMD:", 4) == 0) {
        const char *body = line + 4;
        char name[16];
        bool turn_on = false;

        if (strlen(body) > 4 && strcasecmp(body + strlen(body) - 3, "_ON") == 0) {
            turn_on = true;
            strncpy(name, body, sizeof(name) - 1);
            name[strlen(body) - 3] = '\0';
            for (char *p = name; *p; p++) {
                if (*p >= 'A' && *p <= 'Z') {
                    *p = (char)(*p - 'A' + 'a');
                }
            }
            return handle_cmd_device(name, turn_on);
        }
        if (strlen(body) > 5 && strcasecmp(body + strlen(body) - 4, "_OFF") == 0) {
            strncpy(name, body, sizeof(name) - 1);
            name[strlen(body) - 4] = '\0';
            for (char *p = name; *p; p++) {
                if (*p >= 'A' && *p <= 'Z') {
                    *p = (char)(*p - 'A' + 'a');
                }
            }
            return handle_cmd_device(name, false);
        }
        return false;
    }

    /* 中文别名，便于串口助手测试 */
    if (match_alias(line, "开灯", "lamp", true) || match_alias(line, "关灯", "lamp", false)) {
        return true;
    }
    if (match_alias(line, "开风扇", "fan", true) || match_alias(line, "关风扇", "fan", false)) {
        return true;
    }
    if (match_alias(line, "开继电器1", "relay1", true) || match_alias(line, "关继电器1", "relay1", false)) {
        return true;
    }
    if (match_alias(line, "开继电器2", "relay2", true) || match_alias(line, "关继电器2", "relay2", false)) {
        return true;
    }
    if (match_alias(line, "开蜂鸣器", "buzzer", true) || match_alias(line, "关蜂鸣器", "buzzer", false)) {
        return true;
    }
    if (match_alias(line, "开水泵", "pump", true) || match_alias(line, "关水泵", "pump", false)) {
        return true;
    }

    return false;
}
