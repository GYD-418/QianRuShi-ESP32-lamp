#ifndef GPIO_CONTROLLER_H
#define GPIO_CONTROLLER_H

#include "mcp_server.h"
#include "uart_peer_sender.h"

#include <driver/gpio.h>
#include <esp_log.h>

#include <string>

/**
 * Generic GPIO + MCP tool; UART peer gets CMD:<NAME>_ON / CMD:<NAME>_OFF.
 * device_name: lower-case (lamp, fan, relay1) — matches esp-uart device_ctrl.c
 */
class GpioController {
public:
    GpioController(const char* device_name, gpio_num_t gpio_num)
        : device_name_(device_name), gpio_num_(gpio_num) {
        gpio_config_t config = {
            .pin_bit_mask = (1ULL << gpio_num_),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(gpio_num_, 0);

        const std::string tool_prefix = std::string("self.") + device_name_ + ".";

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            tool_prefix + "get_state",
            std::string("Get power state of ") + device_name_,
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                return power_ ? "{\"power\": true}" : "{\"power\": false}";
            });

        mcp_server.AddTool(
            tool_prefix + "turn_on",
            std::string("Turn on ") + device_name_,
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                SetPower(true);
                return true;
            });

        mcp_server.AddTool(
            tool_prefix + "turn_off",
            std::string("Turn off ") + device_name_,
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                SetPower(false);
                return true;
            });
    }

private:
    void SetPower(bool on) {
        power_ = on;
        gpio_set_level(gpio_num_, on ? 1 : 0);
        UartPeerSender::SendDeviceCmd(device_name_.c_str(), on);
    }

    std::string device_name_;
    gpio_num_t gpio_num_;
    bool power_ = false;
};

#endif
