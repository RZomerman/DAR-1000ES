#pragma once
#include "esp_err.h"

esp_err_t sony_uart_link_init(void);

#ifdef CONFIG_SONY_CONTROLLER_NODE
#include "sony_station_wire.h"
#include "sony_uart_protocol.h"
typedef struct {
  uint32_t generation, sample_rate;
  unsigned channels, preset, genre_labels;
  bool acknowledged, fresh, playing, failed, airplay, connected, airplay_active;
  char station[64], title[65];
  sony_uart_metadata_t metadata;
  uint8_t levels[22];
  uint32_t rms[2], peak[2], held[2];
} sony_audio_link_status_t;
esp_err_t sony_uart_link_select_station(const sony_station_command_t *station,
                                       unsigned preset);
void sony_uart_link_audio_status(sony_audio_link_status_t *status);
bool sony_uart_link_current_station(sony_station_command_t *station);
uint32_t sony_uart_link_request_check(void);
bool sony_uart_link_check_result(uint32_t token, bool *connected);
#ifdef CONFIG_SONY_STARTUP_ISOLATED_DISPLAY
void sony_uart_link_startup_sound(bool play);
#endif
#endif

#if !defined(CONFIG_SONY_UART_AUDIO_NODE) && !defined(CONFIG_SONY_SPLIT_AUDIO_NODE)
#include "esp_http_server.h"
esp_err_t sony_uart_link_register_http(httpd_handle_t server);
#endif
