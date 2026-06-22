#include "uart_comm.h"

#include "device_ctrl.h"
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "uart_comm";

#define UART_RX_BUF_SIZE  1024
#define LINE_BUF_SIZE     128

static char s_line[LINE_BUF_SIZE];
static size_t s_line_len;

static void process_line(const char *line)
{
    if (strcmp(line, "TIME_UART_READY") == 0) {
        ESP_LOGI(TAG, "xiaozhi UART ready, waiting for TIME:...");
        return;
    }
    if (strncmp(line, "TIME:", 5) == 0) {
        ESP_LOGI(TAG, "xiaozhi time -> %s", line + 5);
        return;
    }
    if (device_ctrl_handle_line(line)) {
        uart_comm_send_str("OK\r\n");
        return;
    }
    ESP_LOGW(TAG, "unknown: %s", line);
}

static void feed_bytes(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        const char c = (char)data[i];
        if (c == '\r' || c == '\n') {
            if (s_line_len > 0) {
                s_line[s_line_len] = '\0';
                process_line(s_line);
                s_line_len = 0;
            }
            continue;
        }
        if (s_line_len < LINE_BUF_SIZE - 1) {
            s_line[s_line_len++] = c;
        }
    }
}

void uart_comm_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = UART_COMM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_COMM_PORT, UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_COMM_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_COMM_PORT, UART_COMM_TX_PIN, UART_COMM_RX_PIN,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART%d peer link: TX=GPIO%d RX=GPIO%d %d baud (xiaozhi)",
             UART_COMM_PORT, UART_COMM_TX_PIN, UART_COMM_RX_PIN, UART_COMM_BAUD);
}

int uart_comm_send(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    return uart_write_bytes(UART_COMM_PORT, (const char *)data, len);
}

int uart_comm_send_str(const char *str)
{
    if (str == NULL) {
        return 0;
    }
    return uart_comm_send((const uint8_t *)str, strlen(str));
}

int uart_comm_receive(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    if (buf == NULL || max_len == 0) {
        return 0;
    }
    return uart_read_bytes(UART_COMM_PORT, buf, max_len, pdMS_TO_TICKS(timeout_ms));
}

static void uart_recv_task(void *arg)
{
    uint8_t buf[256];

    (void)arg;
    ESP_LOGI(TAG, "listening for xiaozhi TIME messages...");

    while (true) {
        const int len = uart_comm_receive(buf, sizeof(buf), 100);
        if (len > 0) {
            feed_bytes(buf, len);
        }
    }
}

void uart_comm_start_recv_task(void)
{
    xTaskCreate(uart_recv_task, "uart_recv", 4096, NULL, 10, NULL);
}
