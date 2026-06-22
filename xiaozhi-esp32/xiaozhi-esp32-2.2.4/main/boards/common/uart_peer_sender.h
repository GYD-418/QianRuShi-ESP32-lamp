#ifndef UART_PEER_SENDER_H
#define UART_PEER_SENDER_H

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>

#include <cctype>
#include <cstring>
#include <string>

/**
 * UART link to esp-uart peer board (115200, line-based protocol).
 * See esp-uart/README.md — e.g. CMD:LAMP_ON\r\n
 */
class UartPeerSender {
public:
    static bool Init(gpio_num_t tx_pin, gpio_num_t rx_pin,
                     uart_port_t port = UART_NUM_1, int baud_rate = 115200) {
        if (ready_) {
            return true;
        }
        port_ = port;
        const uart_config_t cfg = {
            .baud_rate = baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        esp_err_t err = uart_driver_install(port_, 1024, 0, 0, nullptr, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
            return false;
        }
        ESP_ERROR_CHECK(uart_param_config(port_, &cfg));
        ESP_ERROR_CHECK(uart_set_pin(port_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ready_ = true;
        ESP_LOGI(TAG, "UART%d peer TX=GPIO%d RX=GPIO%d %d baud", port_, tx_pin, rx_pin, baud_rate);
        return true;
    }

    static bool IsReady() {
        return ready_;
    }

    static bool SendLine(const char* line) {
        if (!ready_ || line == nullptr) {
            return false;
        }
        std::string msg(line);
        msg += "\r\n";
        const int n = uart_write_bytes(port_, msg.c_str(), msg.size());
        if (n < 0) {
            ESP_LOGW(TAG, "uart_write_bytes failed");
            return false;
        }
        ESP_LOGI(TAG, "sent: %s", line);
        return true;
    }

    /** device name is lower-case (lamp, fan); wire format is CMD:LAMP_ON */
    static bool SendDeviceCmd(const char* device, bool turn_on) {
        if (!ready_ || device == nullptr || device[0] == '\0') {
            return false;
        }
        std::string upper;
        for (const char* p = device; *p; ++p) {
            upper += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        }
        const std::string line = "CMD:" + upper + (turn_on ? "_ON" : "_OFF");
        return SendLine(line.c_str());
    }

private:
    inline static const char* TAG = "UartPeer";
    inline static bool ready_ = false;
    inline static uart_port_t port_ = UART_NUM_1;
};

#endif
