#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sony_radio_status.h"
#include "sony_display_modes.h"

/* Requires an already-authorized display scan. Safe to call without a browser.
 */
esp_err_t sony_vfd_show(const char *utf8, bool scroll, bool dim);
/* Only updates an explicitly enabled radio scan; never starts output. */
esp_err_t sony_vfd_show_radio(const char *utf8,
                              const sony_radio_status_t *status);
esp_err_t sony_vfd_show_radio_mode(const char *utf8,
                                  const sony_radio_status_t *status,
                                  sony_display_mode_t mode);
esp_err_t sony_vfd_show_spectrum(const uint8_t levels[22]);
