#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t sony_controller_menu_init(void);
void sony_controller_menu_key(unsigned code, bool held);
typedef enum {
  SONY_PRESET_QUEUED = 1,
  SONY_PRESET_UNASSIGNED,
  SONY_PRESET_ERROR,
} sony_preset_status_t;
void sony_controller_menu_preset_status(sony_preset_status_t status);
