#pragma once
#include "esp_err.h"

esp_err_t sony_uart_link_init(void);

#ifndef CONFIG_SONY_UART_AUDIO_NODE
#include "esp_http_server.h"
esp_err_t sony_uart_link_register_http(httpd_handle_t server);
#endif
