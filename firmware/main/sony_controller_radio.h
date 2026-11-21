#pragma once

#include "esp_err.h"
#include "sony_station_wire.h"

esp_err_t sony_controller_select_station(const sony_station_command_t *station);
esp_err_t sony_controller_get_preset(unsigned number, sony_station_command_t *station);
esp_err_t sony_controller_save_preset(unsigned number, const sony_station_command_t *station);
esp_err_t sony_controller_clear_preset(unsigned number);
