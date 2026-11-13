#pragma once

#include "driver/gpio.h"
#include <stdbool.h>

typedef struct {
  const char *id;
  const char *name;
  /* GDATA, GCLK, CLEAR, STB, SDATA, SCLK; stable while output runs. */
  gpio_num_t pins[6];
} sony_pin_profile_t;

const sony_pin_profile_t *sony_pins_get(void);

#define SONY_GRID_DATA     (sony_pins_get()->pins[0])
#define SONY_GRID_CLOCK    (sony_pins_get()->pins[1])
#define SONY_CLEAR         (sony_pins_get()->pins[2])
#define SONY_STROBE        (sony_pins_get()->pins[3])
#define SONY_SEGMENT_DATA  (sony_pins_get()->pins[4])
#define SONY_SEGMENT_CLOCK (sony_pins_get()->pins[5])
