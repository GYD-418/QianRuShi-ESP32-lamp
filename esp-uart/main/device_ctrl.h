#pragma once

#include <stdbool.h>

/* 外接负载 GPIO，按实际接线修改 */
#define DEVICE_LAMP_GPIO    2
#define DEVICE_FAN_GPIO     1
#define DEVICE_RELAY1_GPIO  8
#define DEVICE_RELAY2_GPIO  9
#define DEVICE_BUZZER_GPIO  10
#define DEVICE_PUMP_GPIO    11

typedef void (*device_ctrl_state_cb_t)(const char *name, bool on);

void device_ctrl_init(void);

/** 设备状态变化时回调（如 lamp），用于上报华为云 */
void device_ctrl_set_state_callback(device_ctrl_state_cb_t cb);

/** 查询设备当前开关状态，不存在返回 false */
bool device_ctrl_get_state(const char *name, bool *out_on);

/** 解析一行 UART 命令，已处理返回 true */
bool device_ctrl_handle_line(const char *line);
