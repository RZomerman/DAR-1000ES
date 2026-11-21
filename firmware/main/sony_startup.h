#pragma once

#include <stdint.h>

enum { SONY_STARTUP_ART_MS = 6250, SONY_STARTUP_DEADLINE_MS = 6900 };

void sony_startup_frame(unsigned elapsed_ms, uint64_t frame[26]);
