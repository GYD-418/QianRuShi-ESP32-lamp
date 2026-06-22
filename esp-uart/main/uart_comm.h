#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"

/*
 * 对接 xiaozhi-esp32 bread-compact-wifi (uart_time_sender.h)
 * 接线：小智 GPIO17(TX) → 本机 GPIO21(RX)
 *       小智 GPIO21(RX) ← 本机 GPIO17(TX)，GND 共地，115200
 * 勿用丝印 43/44（与 USB 芯片共用）
 */
#define UART_COMM_PORT      UART_NUM_1
#define UART_COMM_TX_PIN    17
#define UART_COMM_RX_PIN    21
#define UART_COMM_BAUD      115200

void uart_comm_init(void);
int uart_comm_send(const uint8_t *data, size_t len);
int uart_comm_send_str(const char *str);
int uart_comm_receive(uint8_t *buf, size_t max_len, uint32_t timeout_ms);
void uart_comm_start_recv_task(void);
