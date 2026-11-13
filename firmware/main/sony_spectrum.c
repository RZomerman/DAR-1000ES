#include "sony_spectrum.h"
#include "sony_vfd.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_BYTES (SONY_SPECTRUM_SIZE * 8)

static sony_spectrum_dsp_t *dsp;
static int16_t *history;
static uint8_t *storage;
static StaticStreamBuffer_t input_control;
static StreamBufferHandle_t input;
static atomic_bool initialized;
static atomic_bool enabled;
static atomic_uint epoch;
static atomic_uint source_generation;
static atomic_uint dropped_frames;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static sony_spectrum_status_t latest;

static void clear_levels(void) {
  uint8_t zeros[SONY_SPECTRUM_COLUMNS] = {0};
  sony_spectrum_dsp_reset(dsp);
  portENTER_CRITICAL(&status_lock);
  memset(latest.levels, 0, sizeof(latest.levels));
  portEXIT_CRITICAL(&status_lock);
  sony_vfd_show_spectrum(zeros);
}

static void spectrum_task(void *unused) {
  (void)unused;
  int16_t chunk[512];
  size_t cursor = 0, filled = 0;
  unsigned current_epoch = 0;
  bool active = false;
  int64_t last_analysis = 0;
  int64_t last_input = 0;
  for (;;) {
    bool on = atomic_load(&enabled);
    unsigned next_epoch = atomic_load(&epoch);
    if (!on || !active || current_epoch != next_epoch) {
      while (xStreamBufferReceive(input, chunk, sizeof(chunk), 0)) {
      }
      cursor = filled = 0;
      last_analysis = 0;
      last_input = 0;
      if (active || on)
        clear_levels();
      active = on;
      current_epoch = next_epoch;
    }
    if (on) {
      size_t bytes;
      size_t received = 0;
      while ((bytes = xStreamBufferReceive(input, chunk, sizeof(chunk), 0))) {
        size_t frames = bytes / 4;
        received += frames;
        size_t first = SONY_SPECTRUM_SIZE - cursor;
        if (first > frames)
          first = frames;
        memcpy(history + cursor * 2, chunk, first * 4);
        memcpy(history, chunk + first * 2, (frames - first) * 4);
        cursor = (cursor + frames) % SONY_SPECTRUM_SIZE;
        filled += frames;
        if (filled > SONY_SPECTRUM_SIZE)
          filled = SONY_SPECTRUM_SIZE;
      }
      int64_t now = esp_timer_get_time();
      if (received)
        last_input = now;
      if (filled && now - last_input > 250000) {
        clear_levels();
        cursor = filled = 0;
      } else if (received && filled == SONY_SPECTRUM_SIZE &&
                 now - last_analysis >= 100000) {
        uint8_t levels[SONY_SPECTRUM_COLUMNS];
        last_analysis = now;
        sony_spectrum_analyze(dsp, history, cursor, levels);
        unsigned elapsed = (unsigned)(esp_timer_get_time() - now);
        if (atomic_load(&enabled) && current_epoch == atomic_load(&epoch)) {
          sony_vfd_show_spectrum(levels);
          portENTER_CRITICAL(&status_lock);
          memcpy(latest.levels, levels, sizeof(levels));
          latest.last_fft_us = elapsed;
          ++latest.updates;
          portEXIT_CRITICAL(&status_lock);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(on ? 20 : 100));
  }
}

esp_err_t sony_spectrum_init(void) {
  if (atomic_load(&initialized))
    return ESP_OK;
  dsp = heap_caps_malloc(sizeof(*dsp), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  history = heap_caps_malloc(SONY_SPECTRUM_SIZE * 4,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  storage =
      heap_caps_malloc(INPUT_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (dsp && history && storage) {
    input =
        xStreamBufferCreateStatic(INPUT_BYTES + 1, 4, storage, &input_control);
    if (input) {
      sony_spectrum_dsp_init(dsp);
      if (xTaskCreatePinnedToCore(spectrum_task, "spectrum", 8192, NULL, 1,
                                  NULL, 0) == pdPASS) {
        atomic_store(&initialized, true);
        return ESP_OK;
      }
      vStreamBufferDelete(input);
      input = NULL;
    }
  }
  free(dsp);
  free(history);
  free(storage);
  dsp = NULL;
  history = NULL;
  storage = NULL;
  return ESP_ERR_NO_MEM;
}

void sony_spectrum_enable(bool on) {
  bool next = on && atomic_load(&initialized);
  if (atomic_exchange(&enabled, next) != next)
    atomic_fetch_add(&epoch, 1);
}

void sony_spectrum_write(const int16_t *stereo, size_t frames,
                         uint32_t generation) {
  if (!atomic_load(&enabled) || !frames)
    return;
  if (atomic_exchange(&source_generation, generation) != generation)
    atomic_fetch_add(&epoch, 1);
  size_t sent = xStreamBufferSend(input, stereo, frames * 4, 0);
  if (sent != frames * 4) {
    atomic_fetch_add(&dropped_frames, (unsigned)(frames - sent / 4));
    atomic_fetch_add(&epoch, 1);
  }
}

void sony_spectrum_get_status(sony_spectrum_status_t *status) {
  portENTER_CRITICAL(&status_lock);
  *status = latest;
  portEXIT_CRITICAL(&status_lock);
  status->initialized = atomic_load(&initialized);
  status->enabled = atomic_load(&enabled);
  status->dropped_frames = atomic_load(&dropped_frames);
}
