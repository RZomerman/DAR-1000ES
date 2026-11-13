#pragma once

#include "cJSON.h"
#include "esp_err.h"

esp_err_t sony_manual_controls_init(void);
esp_err_t sony_manual_controls_set(const char *signal, int level);
esp_err_t sony_manual_controls_replay_startup(void);
esp_err_t sony_manual_controls_status(cJSON *root);
