#pragma once

#include <stdbool.h>
#include <stddef.h>

void radio_display_text(char *output, size_t capacity, const char *title,
                        const char *station, bool receiving, bool failed);
