#ifndef __LAMP_CONTROLLER_H__
#define __LAMP_CONTROLLER_H__

#include "mcp_server.h"
#include "uart_peer_sender.h"

#include <driver/gpio.h>

class LampController {
private:
    bool power_ = false;
    gpio_num_t gpio_num_;

    void SetPower(bool on) {
        power_ = on;
        if (gpio_num_ != GPIO_NUM_NC) {
            gpio_set_level(gpio_num_, on ? 1 : 0);
        }
        UartPeerSender::SendDeviceCmd("lamp", on);
    }

public:
    /** gpio_num == GPIO_NUM_NC: UART peer only, no local GPIO */
    explicit LampController(gpio_num_t gpio_num = GPIO_NUM_NC) : gpio_num_(gpio_num) {
        if (gpio_num_ != GPIO_NUM_NC) {
            gpio_config_t config = {
                .pin_bit_mask = (1ULL << gpio_num_),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            ESP_ERROR_CHECK(gpio_config(&config));
            gpio_set_level(gpio_num_, 0);
        }

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.lamp.get_state", "Get the power state of the lamp", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return power_ ? "{\"power\": true}" : "{\"power\": false}";
        });

        mcp_server.AddTool("self.lamp.turn_on", "Turn on the lamp", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            SetPower(true);
            return true;
        });

        mcp_server.AddTool("self.lamp.turn_off", "Turn off the lamp", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            SetPower(false);
            return true;
        });
    }
};

#endif // __LAMP_CONTROLLER_H__
