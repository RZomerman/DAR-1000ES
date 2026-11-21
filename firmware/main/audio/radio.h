#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t radio_init(void);
bool radio_airplay_active(void);
bool radio_pcm_active(void);
uint32_t radio_generation(void);
size_t radio_read(int16_t *pcm, size_t frames, uint32_t *rate);
esp_err_t radio_register_http(httpd_handle_t server);
/* Selects a configured physical preset, numbered 1-16. */
esp_err_t radio_select_preset(unsigned number);
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
#include "sony_uart_protocol.h"
esp_err_t radio_select_url(const char *name, const char *url);
typedef struct {
  char title[65];
  uint32_t generation;
  uint32_t sample_rate;
  unsigned channels;
  bool playing, failed, airplay;
} radio_link_status_t;
void radio_link_status(radio_link_status_t *status);
void radio_link_metadata(sony_uart_metadata_t *metadata);
#endif
