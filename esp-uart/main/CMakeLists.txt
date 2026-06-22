idf_component_register(SRCS "uart_comm.c" "device_ctrl.c" "huawei_iot.c" "esp-uart.c"
                    INCLUDE_DIRS "."
                    REQUIRES mqtt esp_wifi nvs_flash esp_netif json esp_driver_uart esp_driver_gpio)
