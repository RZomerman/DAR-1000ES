#include "sony_spectrum.h"
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
#include "sony_screen.h"
#include "sony_vfd.h"
#endif

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
static atomic_flag initializing = ATOMIC_FLAG_INIT;
static atomic_bool enabled;
static atomic_uint epoch;
static atomic_uint source_generation;
static atomic_uint dropped_frames;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static sony_spectrum_status_t latest;

static void clear_levels(void) {
  sony_spectrum_dsp_reset(dsp);
  portENTER_CRITICAL(&status_lock);
  memset(latest.levels, 0, sizeof(latest.levels));
  memset(latest.rms, 0, sizeof(latest.rms));
  memset(latest.peak, 0, sizeof(latest.peak));
  memset(latest.held, 0, sizeof(latest.held));
  latest.generation = atomic_load(&source_generation);
  portEXIT_CRITICAL(&status_lock);
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
  uint8_t zeros[SONY_SPECTRUM_COLUMNS] = {0};
  sony_vfd_show_spectrum(zeros);
  const uint32_t silence[2] = {0, 0};
  sony_vfd_show_vu(silence, silence, silence);
#endif
}

static void spectrum_task(void *unused) {
  (void)unused;
  int16_t chunk[512];
  size_t cursor = 0, filled = 0;
  unsigned current_epoch = 0;
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
  sony_display_mode_t current_mode = SONY_DISPLAY_MODE_COUNT;
#endif
  sony_vu_state_t vu;
  sony_vu_reset(&vu);
  bool active = false;
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
  const int64_t analysis_interval_us = 50000;
#else
  const int64_t analysis_interval_us = 100000;
#endif
  int64_t next_analysis = 0;
  int64_t last_input = 0;
  for (;;) {
    bool on = atomic_load(&enabled);
    unsigned next_epoch = atomic_load(&epoch);
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
    sony_display_mode_t mode = sony_screen_display_mode();
    bool mode_changed = mode != current_mode;
#else
    bool mode_changed = false;
#endif
    if (!on || !active || current_epoch != next_epoch || mode_changed) {
      while (xStreamBufferReceive(input, chunk, sizeof(chunk), 0)) {
      }
      cursor = filled = 0;
      next_analysis = 0;
      last_input = 0;
      sony_vu_reset(&vu);
      if (active || on)
        clear_levels();
      active = on;
      current_epoch = next_epoch;
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
      current_mode = mode;
#endif
    }
    if (on) {
      size_t bytes;
      size_t received = 0;
      while ((bytes = xStreamBufferReceive(input, chunk, sizeof(chunk), 0))) {
        size_t frames = bytes / 4;
        received += frames;
        if (
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
            mode == SONY_DISPLAY_EQUALIZER
#else
            true
#endif
        ) {
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
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
        else {
          uint32_t rms[2], peak[2], held[2];
          if (sony_vu_analyze(&vu, chunk, frames, rms, peak, held) &&
              atomic_load(&enabled) && current_epoch == atomic_load(&epoch))
            sony_vfd_show_vu(rms, peak, held);
        }
#else
        uint32_t rms[2], peak[2], held[2];
        if (sony_vu_analyze(&vu, chunk, frames, rms, peak, held) &&
            atomic_load(&enabled) && current_epoch == atomic_load(&epoch)) {
          portENTER_CRITICAL(&status_lock);
          memcpy(latest.rms, rms, sizeof(rms));
          memcpy(latest.peak, peak, sizeof(peak));
          memcpy(latest.held, held, sizeof(held));
          latest.generation = atomic_load(&source_generation);
          portEXIT_CRITICAL(&status_lock);
        }
#endif
      }
      int64_t now = esp_timer_get_time();
      if (received)
        last_input = now;
      if (last_input && now - last_input > 250000) {
        clear_levels();
        cursor = filled = 0;
        sony_vu_reset(&vu);
        last_input = 0;
      } else if (received &&
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
                 mode == SONY_DISPLAY_EQUALIZER &&
#endif
                 filled == SONY_SPECTRUM_SIZE &&
                 now >= next_analysis) {
        uint8_t levels[SONY_SPECTRUM_COLUMNS];
        if (!next_analysis || now - next_analysis >= analysis_interval_us)
          next_analysis = now;
        next_analysis += analysis_interval_us;
        sony_spectrum_analyze(dsp, history, cursor, levels);
        unsigned elapsed = (unsigned)(esp_timer_get_time() - now);
        if (atomic_load(&enabled) && current_epoch == atomic_load(&epoch)) {
#ifndef CONFIG_SONY_SPLIT_AUDIO_NODE
          sony_vfd_show_spectrum(levels);
#endif
          portENTER_CRITICAL(&status_lock);
          memcpy(latest.levels, levels, sizeof(levels));
          latest.generation = atomic_load(&source_generation);
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
  if (atomic_flag_test_and_set(&initializing))
    return ESP_ERR_INVALID_STATE;
  if (atomic_load(&initialized)) {
    atomic_flag_clear(&initializing);
    return ESP_OK;
  }
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
        atomic_flag_clear(&initializing);
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
  atomic_flag_clear(&initializing);
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
