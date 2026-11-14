#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "sony_display_modes.h"

esp_err_t sony_screen_init(void);
esp_err_t sony_screen_register_http(httpd_handle_t server);
sony_display_mode_t sony_screen_display_mode(void);
esp_err_t sony_screen_set_display_mode(sony_display_mode_t mode);
/* Call from a debounced button task, not an ISR. Never starts stopped output. */
esp_err_t sony_screen_display_mode_press(bool long_press);
