#include "device_ctrl.h"
#include "esp_log.h"
#include "huawei_iot.h"
#include "uart_comm.h"

static const char *TAG = "main";

void app_main(void)
{
    device_ctrl_init();
    uart_comm_init();
    uart_comm_start_recv_task();
    huawei_iot_start();

    ESP_LOGI(TAG, "UART peer + Huawei Cloud IoT (set WiFi in menuconfig)");
}
