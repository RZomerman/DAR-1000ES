#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t sony_screen_init(void);
esp_err_t sony_screen_register_http(httpd_handle_t server);
