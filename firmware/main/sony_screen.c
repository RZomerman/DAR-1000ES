#include "sony_screen.h"
#include "sony_capture_replay.h"
#include "sony_text.h"
#include "sony_vfd.h"
#include "sony_pins.h"
#include "sony_spectrum.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRID_DATA     SONY_GRID_DATA
#define GRID_CLOCK    SONY_GRID_CLOCK
#define GRID_CLEAR    SONY_CLEAR
#define STROBE        SONY_STROBE
#define SEGMENT_DATA  SONY_SEGMENT_DATA
#define SEGMENT_CLOCK SONY_SEGMENT_CLOCK
#define SLOT_US       301
#define TEST_FRAMES   64

static const sony_pin_profile_t pin_profiles[] = {
    {.id = "original",
     .name = "Original / TXS0108E",
     .pins = {GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_15, GPIO_NUM_16,
              GPIO_NUM_7}},
    {.id = "ahct-v2",
     .name = "AHCT v2",
     .pins = {GPIO_NUM_2, GPIO_NUM_1, GPIO_NUM_40, GPIO_NUM_38, GPIO_NUM_21,
              GPIO_NUM_39}},
};
static atomic_uint pin_profile_index;

const sony_pin_profile_t *sony_pins_get(void) {
  return &pin_profiles[atomic_load(&pin_profile_index)];
}

#define SCREEN_PINS (sony_pins_get()->pins)
static const uint64_t frequency[] = {
    0x03A1E8C400ULL, 0x078210F800ULL, 0x046319B400ULL,
    0x0783F8B800ULL, 0x05B318C400ULL, 0x043D18F800ULL,
    0x0783F8B800ULL, 0x0084236800ULL, 0x004217843FULL};
static const uint64_t no_dsr[] = {0x045257C62FULL,
                                  0x03A307062EULL,
                                  0x03E5294A4FULL,
                                  0,
                                  0x03A318B800ULL,
                                  0x04639ACE31ULL,
                                  0,
                                  0,
                                  0};
static uint64_t words[26];
static sony_text_t scrolling_text;
static bool display_dim = true;
static atomic_bool running;
static atomic_bool stop_requested;
static atomic_uint completed_frames;
static atomic_int last_error;
static atomic_int active_held_slot = -1;
static atomic_uint held_switches;
static atomic_uint held_last_interval_us;
static atomic_int pin_levels[6];
static bool capture_requested;
static bool frequency_capture_requested;
static bool mono_test_requested;
static atomic_bool display_requested;
static atomic_bool timing_requested;
static sony_timing_t display_timing;
static atomic_uint timing_revision;
static atomic_uint submitted_timing_revision;
static portMUX_TYPE frame_lock = portMUX_INITIALIZER_UNLOCKED;
static bool follow_radio;
static bool follow_spectrum;
static sony_radio_status_t last_radio_status;
static unsigned scrolling_width = 9;

static esp_err_t show_text(const char *utf8, bool scroll, bool dim,
                          unsigned width, const sony_radio_status_t *radio) {
  if (!atomic_load(&running) || !atomic_load(&display_requested) ||
      atomic_load(&stop_requested))
    return ESP_ERR_INVALID_STATE;
  sony_text_t *text = calloc(1, sizeof(*text));
  if (!text)
    return ESP_ERR_NO_MEM;
  if (!sony_text_parse(text, utf8, esp_timer_get_time())) {
    free(text);
    return ESP_ERR_INVALID_ARG;
  }
  text->scroll = scroll;
  esp_err_t err = ESP_OK;
  portENTER_CRITICAL(&frame_lock);
  if (!atomic_load(&running) || !atomic_load(&display_requested) ||
      atomic_load(&stop_requested) ||
      (radio && (!follow_radio || !atomic_load(&timing_requested) ||
                 display_timing.hold || display_timing.pair))) {
    err = ESP_ERR_INVALID_STATE;
  } else if ((!radio && follow_spectrum) || !scrolling_text.active ||
             scrolling_text.scroll != scroll ||
             scrolling_width != width ||
             scrolling_text.length != text->length ||
             memcmp(scrolling_text.glyphs, text->glyphs,
                    text->length * sizeof(text->glyphs[0]))) {
    scrolling_text = *text;
    scrolling_width = width;
    if (!radio || !follow_spectrum)
      sony_text_frame_width(&scrolling_text, esp_timer_get_time(), words, width);
  }
  if (err == ESP_OK) {
    display_dim = dim;
    if (radio) {
      last_radio_status = *radio;
      sony_radio_status_apply_view(words, radio, follow_spectrum);
    } else {
      if (follow_spectrum)
        sony_radio_status_apply_view(words, &last_radio_status, false);
      follow_radio = false;
      follow_spectrum = false;
    }
  }
  portEXIT_CRITICAL(&frame_lock);
  free(text);
  if (err == ESP_OK && !radio)
    sony_spectrum_enable(false);
  return err;
}

esp_err_t sony_vfd_show(const char *utf8, bool scroll, bool dim) {
  return show_text(utf8, scroll, dim, 9, NULL);
}

esp_err_t sony_vfd_show_radio(const char *utf8,
                              const sony_radio_status_t *status) {
  if (!status)
    return ESP_ERR_INVALID_ARG;
  return show_text(utf8, true, true, 22, status);
}

esp_err_t sony_vfd_show_spectrum(const uint8_t levels[22]) {
  if (!levels)
    return ESP_ERR_INVALID_ARG;
  esp_err_t err = ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&frame_lock);
  if (follow_radio && follow_spectrum && atomic_load(&running) &&
      atomic_load(&display_requested) && atomic_load(&timing_requested) &&
      !atomic_load(&stop_requested) && !display_timing.hold &&
      !display_timing.pair) {
    sony_spectrum_frame(levels, words);
    err = ESP_OK;
  }
  portEXIT_CRITICAL(&frame_lock);
  return err;
}

static esp_err_t set(gpio_num_t pin, int level) {
  esp_err_t err = gpio_set_level(pin, level);
  if (err == ESP_OK)
    for (size_t i = 0; i < 6; ++i)
      if (SCREEN_PINS[i] == pin)
        atomic_store(&pin_levels[i], level);
  return err;
}

static esp_err_t pulse(gpio_num_t clock) {
  esp_err_t err = set(clock, 0);
  if (err != ESP_OK)
    return err;
  esp_rom_delay_us(1);
  err = set(clock, 1);
  esp_rom_delay_us(1);
  return err;
}

static esp_err_t segment_word(uint64_t word) {
  for (int bit = 39; bit >= 0; --bit) {
    esp_err_t err = set(SEGMENT_DATA, (word >> bit) & 1);
    if (err != ESP_OK)
      return err;
    err = pulse(SEGMENT_CLOCK);
    if (err != ESP_OK)
      return err;
  }
  return ESP_OK;
}

/* Flush both 40-bit registers and latch zeros, without assuming CLEAR polarity.
 * This is not a hardware fail-safe against a stalled CPU or loss of power. */
static esp_err_t blank_registers(void) {
  esp_err_t err = set(STROBE, 0);
  if (err == ESP_OK)
    err = set(GRID_DATA, 0);
  if (err == ESP_OK)
    err = segment_word(0);
  for (int i = 0; i < 40 && err == ESP_OK; ++i)
    err = pulse(GRID_CLOCK);
  if (err == ESP_OK)
    err = set(STROBE, 1);
  esp_rom_delay_us(3);
  if (err == ESP_OK)
    err = set(STROBE, 0);
  return err;
}

static esp_err_t hold_idle_outputs(void) {
  const gpio_num_t idle_order[] = {
      GRID_CLEAR, STROBE, GRID_DATA, SEGMENT_DATA, GRID_CLOCK, SEGMENT_CLOCK};
  static const int idle_levels[] = {0, 0, 0, 0, 1, 1};
  uint64_t mask = 0;
  for (size_t i = 0; i < sizeof(idle_order) / sizeof(idle_order[0]); ++i) {
    esp_err_t err = set(idle_order[i], idle_levels[i]);
    if (err != ESP_OK)
      return err;
  }
  for (size_t i = 0; i < 6; ++i)
    mask |= 1ULL << SCREEN_PINS[i];
  gpio_config_t cfg = {.pin_bit_mask = mask,
                       .mode = GPIO_MODE_OUTPUT,
                       .pull_up_en = GPIO_PULLUP_DISABLE,
                       .pull_down_en = GPIO_PULLDOWN_DISABLE,
                       .intr_type = GPIO_INTR_DISABLE};
  return gpio_config(&cfg);
}

static void restore_idle_outputs(void) {
  esp_err_t err = hold_idle_outputs();
  if (err != ESP_OK)
    atomic_store(&last_error, err);
}

static esp_err_t enable_pins(void) {
  for (size_t i = 0; i < 6; ++i) {
    esp_err_t err =
        set(SCREEN_PINS[i],
            SCREEN_PINS[i] == GRID_CLOCK || SCREEN_PINS[i] == SEGMENT_CLOCK);
    if (err != ESP_OK)
      return err;
    err = gpio_set_direction(SCREEN_PINS[i], GPIO_MODE_OUTPUT);
    if (err != ESP_OK)
      return err;
  }
  return blank_registers();
}

static esp_err_t scan_slot(unsigned slot) {
  int64_t start = esp_timer_get_time();
  esp_err_t err = set(STROBE, 0);
  if (err == ESP_OK)
    err = set(GRID_DATA, slot == 0);
  esp_rom_delay_us(3);
  if (err == ESP_OK)
    err = pulse(GRID_CLOCK);
  if (err == ESP_OK)
    err = set(GRID_CLEAR, 1);
  if (err == ESP_OK)
    err = segment_word(words[slot]);
  int64_t remaining = SLOT_US - 12 - (esp_timer_get_time() - start);
  if (remaining > 0)
    esp_rom_delay_us((uint32_t)remaining);
  if (err == ESP_OK)
    err = set(GRID_CLEAR, 0);
  esp_rom_delay_us(12);
  if (err == ESP_OK)
    err = set(STROBE, 1);
  esp_rom_delay_us(3);
  return err;
}

static esp_err_t gpio_pair_cycle(uint8_t *wave, const uint64_t frame[26],
                                 const sony_timing_t *timing) {
  size_t bytes =
      sony_timing_generate(wave, SONY_TIMING_CAPACITY, timing, frame, false);
  if (!bytes)
    return ESP_ERR_INVALID_SIZE;
  const gpio_num_t lanes[] = {SEGMENT_CLOCK, SEGMENT_DATA, GRID_CLEAR,
                                     STROBE,        GRID_DATA,    GRID_CLOCK};
  uint8_t prior = 0x21;
  esp_err_t err = ESP_OK;
  for (size_t pos = 0; pos < bytes && err == ESP_OK;) {
    if (atomic_load(&stop_requested))
      break;
    uint8_t state = wave[pos];
    size_t end = pos + 1;
    while (end < bytes && wave[end] == state)
      ++end;
    uint32_t us = (uint32_t)(((end - pos) * SONY_TIMING_TICK_NS + 999) / 1000);
    uint8_t blanked = state & ~4U;
    for (unsigned bit = 0; bit < 6 && err == ESP_OK; ++bit)
      if ((prior ^ blanked) & (1U << bit))
        err = set(lanes[bit], (blanked >> bit) & 1);
    if (err != ESP_OK)
      break;
    if (state & 4) {
      /* Keep software preemption out of the short visible interval.
       * This is not protection against CPU/power failure. */
      portENTER_CRITICAL(&frame_lock);
      if (!atomic_load(&stop_requested)) {
        err = set(GRID_CLEAR, 1);
        if (err == ESP_OK)
          esp_rom_delay_us(us);
      }
      esp_err_t blank = set(GRID_CLEAR, 0);
      portEXIT_CRITICAL(&frame_lock);
      if (err == ESP_OK)
        err = blank;
    } else {
      esp_rom_delay_us(us);
    }
    prior = blanked;
    pos = end;
  }
  esp_err_t blank = set(GRID_CLEAR, 0);
  return err == ESP_OK ? blank : err;
}

static void scan_task(void *unused) {
  (void)unused;
  if (atomic_load(&display_requested)) {
    sony_scan_engine_t *engine = NULL;
    bool timed = atomic_load(&timing_requested);
    bool engine_held = false;
    bool engine_blanked = false;
    esp_err_t err = ESP_OK;
    unsigned queued = 0;
    unsigned pending_cycles[2] = {0};
    uint8_t *gpio_wave = NULL;
    unsigned schedule_revision = 0;
    int64_t schedule_start = 0;
    int64_t last_switch = 0;
    int prior_held_slot = -1;
    while (!atomic_load(&stop_requested) && err == ESP_OK) {
      uint64_t frame[26];
      bool dim;
      sony_timing_t timing;
      unsigned revision;
      portENTER_CRITICAL(&frame_lock);
      if (!follow_spectrum)
        sony_text_frame_width(&scrolling_text, esp_timer_get_time(), words,
                              scrolling_width);
      memcpy(frame, words, sizeof(frame));
      dim = display_dim;
      timing = display_timing;
      revision = atomic_load(&timing_revision);
      portEXIT_CRITICAL(&frame_lock);
      if (timed && timing.hold && timing.alternate_ms) {
        int64_t now = esp_timer_get_time();
        if (!schedule_start || schedule_revision != revision) {
          schedule_start = now;
          schedule_revision = revision;
        }
        timing.slot = sony_timing_held_slot(&timing, now - schedule_start);
      } else {
        schedule_start = 0;
      }
      if (timed && timing.hold && timing.slot == SONY_TIMING_BLANK_SLOT) {
        if (engine && !engine_blanked) {
          err = sony_scan_blank(engine);
          engine_blanked = err == ESP_OK;
          queued = 0;
          pending_cycles[0] = pending_cycles[1] = 0;
        }
        atomic_store(&active_held_slot, -1);
        if (err == ESP_OK)
          atomic_store(&submitted_timing_revision, revision);
        vTaskDelay(1);
        continue;
      }
      if (timed && timing.pair) {
        atomic_store(&active_held_slot, -1);
        prior_held_slot = -1;
        if (engine) {
          err = sony_scan_close(engine);
          engine = NULL;
          queued = 0;
          engine_blanked = false;
          pending_cycles[0] = pending_cycles[1] = 0;
          if (err != ESP_OK)
            break;
        }
        if (!gpio_wave) {
          err = hold_idle_outputs();
          if (err != ESP_OK)
            break;
          gpio_wave = heap_caps_malloc(SONY_TIMING_CAPACITY,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          if (!gpio_wave) {
            err = ESP_ERR_NO_MEM;
            break;
          }
        }
        err = gpio_pair_cycle(gpio_wave, frame, &timing);
        if (err == ESP_OK && !atomic_load(&stop_requested)) {
          atomic_fetch_add(&completed_frames, sony_timing_info(&timing).cycles);
          atomic_store(&submitted_timing_revision, revision);
        }
        /* Yield only while blanked, never during a visible grid interval. */
        vTaskDelay(1);
        continue;
      }
      free(gpio_wave);
      gpio_wave = NULL;
      if (!engine || (timed && engine_held != (bool)timing.hold)) {
        err = sony_scan_close(engine);
        engine = NULL;
        if (err != ESP_OK)
          break;
        engine_held = timed && timing.hold;
        err = timed ? sony_scan_open_timed(&engine, engine_held)
                    : sony_scan_open(&engine);
        if (err != ESP_OK)
          break;
        queued = 0;
        pending_cycles[0] = pending_cycles[1] = 0;
      }
      unsigned cycles = timed ? sony_timing_info(&timing).cycles : 8;
      err = timed ? sony_scan_submit_timed(engine, frame, &timing)
                  : sony_scan_submit(engine, frame, dim);
      if (err == ESP_OK) {
        engine_blanked = false;
        if (timed && timing.hold) {
          if (prior_held_slot != (int)timing.slot) {
            int64_t now = esp_timer_get_time();
            if (prior_held_slot >= 0) {
              atomic_fetch_add(&held_switches, 1);
              atomic_store(&held_last_interval_us,
                           (unsigned)(now - last_switch));
            }
            last_switch = now;
            prior_held_slot = (int)timing.slot;
          }
          atomic_store(&active_held_slot, timing.slot);
        } else {
          atomic_store(&active_held_slot, -1);
          prior_held_slot = -1;
        }
        if (queued >= 2)
          atomic_fetch_add(&completed_frames, pending_cycles[queued % 2]);
        pending_cycles[queued % 2] = cycles;
        ++queued;
        if (timed)
          atomic_store(&submitted_timing_revision, revision);
      }
    }
    esp_err_t cleanup = sony_scan_close(engine);
    free(gpio_wave);
    if (err == ESP_OK) {
      err = cleanup;
      if (err == ESP_OK && !timed)
        atomic_fetch_add(&completed_frames,
                         pending_cycles[0] + pending_cycles[1]);
    }
    esp_err_t idle_err = hold_idle_outputs();
    if (err == ESP_OK)
      err = idle_err;
    atomic_store(&last_error, err);
    atomic_store(&active_held_slot, -1);
    sony_spectrum_enable(false);
    atomic_store(&running, false);
    vTaskDelete(NULL);
    return;
  }
  if (capture_requested) {
    uint8_t final_state = 0;
    uint32_t frames = 0;
    esp_err_t err =
        sony_capture_replay(frequency_capture_requested, mono_test_requested,
                            &final_state, &frames);
    atomic_store(&last_error, err);
    if (err == ESP_OK) {
      static const unsigned bits[] = {4, 5, 2, 3, 1, 0};
      for (unsigned i = 0; i < 6; ++i)
        atomic_store(&pin_levels[i], (final_state >> bits[i]) & 1);
      atomic_store(&completed_frames, frames);
    } else {
      restore_idle_outputs();
    }
    atomic_store(&running, false);
    vTaskDelete(NULL);
    return;
  }
  esp_err_t err = enable_pins();
  for (unsigned frame = 0;
       frame < TEST_FRAMES && err == ESP_OK && !atomic_load(&stop_requested);
       ++frame) {
    for (unsigned slot = 0; slot < 26 && err == ESP_OK; ++slot)
      err = scan_slot(slot);
    if (err == ESP_OK)
      atomic_fetch_add(&completed_frames, 1);
    /* Latch zeros before yielding; actual chip blanking still needs
     * verification. */
    esp_err_t blank = blank_registers();
    if (err == ESP_OK)
      err = blank;
    vTaskDelay(1);
  }
  esp_err_t blank = blank_registers();
  if (err == ESP_OK)
    err = blank;
  atomic_store(&last_error, err);
  restore_idle_outputs();
  atomic_store(&running, false);
  vTaskDelete(NULL);
}

static esp_err_t release_profile(const sony_pin_profile_t *profile) {
  uint64_t mask = 0;
  for (unsigned i = 0; i < 6; ++i)
    mask |= 1ULL << profile->pins[i];
  gpio_config_t config = {.pin_bit_mask = mask,
                          .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_DISABLE,
                          .pull_down_en = GPIO_PULLDOWN_DISABLE};
  return gpio_config(&config);
}

static esp_err_t select_pin_profile(const char *id) {
  unsigned selected = 0;
  for (; selected < sizeof(pin_profiles) / sizeof(pin_profiles[0]); ++selected)
    if (!strcmp(id, pin_profiles[selected].id))
      break;
  if (selected == sizeof(pin_profiles) / sizeof(pin_profiles[0]))
    return ESP_ERR_INVALID_ARG;
  if (atomic_load(&running))
    return ESP_ERR_INVALID_STATE;
  unsigned previous = atomic_load(&pin_profile_index);
  if (previous == selected)
    return ESP_OK;
  esp_err_t err = hold_idle_outputs();
  if (err == ESP_OK)
    err = release_profile(&pin_profiles[previous]);
  if (err != ESP_OK) {
    restore_idle_outputs();
    return err;
  }
  atomic_store(&pin_profile_index, selected);
  err = hold_idle_outputs();
  if (err != ESP_OK) {
    release_profile(&pin_profiles[selected]);
    atomic_store(&pin_profile_index, previous);
    restore_idle_outputs();
  }
  return err;
}

esp_err_t sony_screen_init(void) {
  display_timing = sony_timing_defaults();
  for (size_t i = 0; i < 6; ++i) {
    atomic_store(&pin_levels[i], 0);
  }
  return hold_idle_outputs();
}

static esp_err_t status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  cJSON_AddBoolToObject(root, "running", atomic_load(&running));
  cJSON_AddStringToObject(root, "pin_profile", sony_pins_get()->id);
  cJSON_AddStringToObject(root, "pin_profile_name", sony_pins_get()->name);
  cJSON_AddBoolToObject(
      root, "levels_dynamic",
      atomic_load(&running) &&
          (capture_requested || atomic_load(&display_requested)));
  cJSON_AddBoolToObject(root, "display_running",
                        atomic_load(&running) &&
                            atomic_load(&display_requested));
  cJSON *frame_words = cJSON_AddArrayToObject(root, "words");
  if (!frame_words) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  }
  uint64_t frame[26];
  portENTER_CRITICAL(&frame_lock);
  memcpy(frame, words, sizeof(frame));
  bool dim = display_dim;
  bool scrolling = !follow_spectrum && scrolling_text.active &&
                   scrolling_text.scroll && scrolling_text.length > scrolling_width;
  sony_timing_t timing = display_timing;
  portEXIT_CRITICAL(&frame_lock);
  cJSON_AddStringToObject(root, "brightness", dim ? "dim" : "bright");
  cJSON_AddBoolToObject(root, "scrolling", scrolling);
  cJSON_AddBoolToObject(root, "timing_running",
                        atomic_load(&running) &&
                            atomic_load(&timing_requested) &&
                            atomic_load(&display_requested));
  portENTER_CRITICAL(&frame_lock);
  bool radio_following = follow_radio;
  bool spectrum_following = follow_spectrum;
  unsigned text_width = scrolling_width;
  portEXIT_CRITICAL(&frame_lock);
  cJSON_AddBoolToObject(root, "now_playing", radio_following &&
                                               atomic_load(&running) &&
                                               !atomic_load(&stop_requested));
  cJSON_AddNumberToObject(root, "text_width", text_width);
  const char *visualization = "manual";
  if (radio_following)
    visualization = spectrum_following ? "spectrum" : "now_playing";
  cJSON_AddStringToObject(root, "visualization", visualization);
  sony_spectrum_status_t spectrum;
  sony_spectrum_get_status(&spectrum);
  cJSON *spectrum_json = cJSON_AddObjectToObject(root, "spectrum");
  cJSON *spectrum_levels = cJSON_AddArrayToObject(spectrum_json, "levels");
  cJSON *spectrum_edges = cJSON_AddArrayToObject(spectrum_json, "band_edges_hz");
  if (!spectrum_json || !spectrum_levels || !spectrum_edges) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  }
  cJSON_AddBoolToObject(spectrum_json, "initialized", spectrum.initialized);
  cJSON_AddBoolToObject(spectrum_json, "enabled", spectrum.enabled);
  cJSON_AddNumberToObject(spectrum_json, "updates", spectrum.updates);
  cJSON_AddNumberToObject(spectrum_json, "dropped_frames", spectrum.dropped_frames);
  cJSON_AddNumberToObject(spectrum_json, "last_fft_us", spectrum.last_fft_us);
  cJSON_AddNumberToObject(spectrum_json, "sample_rate", SONY_SPECTRUM_RATE);
  for (unsigned i = 0; i < SONY_SPECTRUM_COLUMNS; ++i)
    cJSON_AddItemToArray(spectrum_levels, cJSON_CreateNumber(spectrum.levels[i]));
  for (unsigned i = 0; i <= SONY_SPECTRUM_BANDS; ++i)
    cJSON_AddItemToArray(spectrum_edges, cJSON_CreateNumber(sony_spectrum_edges[i]));
  cJSON_AddNumberToObject(root, "timing_revision",
                          atomic_load(&timing_revision));
  cJSON_AddNumberToObject(root, "submitted_timing_revision",
                          atomic_load(&submitted_timing_revision));
  cJSON *timing_json = cJSON_AddObjectToObject(root, "timing");
  if (!timing_json) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  }
#define TIMING_STATUS(field) \
  cJSON_AddNumberToObject(timing_json, #field, timing.field)
  TIMING_STATUS(setup_ns);
  TIMING_STATUS(clock_low_ns);
  TIMING_STATUS(clock_high_ns);
  TIMING_STATUS(strobe_ns);
  TIMING_STATUS(latch_guard_ns);
  TIMING_STATUS(blank_before_ns);
  TIMING_STATUS(blank_after_ns);
  TIMING_STATUS(on_ns);
  TIMING_STATUS(off_ns);
  TIMING_STATUS(pulses);
  TIMING_STATUS(hold);
  TIMING_STATUS(slot);
  TIMING_STATUS(pair);
  TIMING_STATUS(alternate_ms);
  TIMING_STATUS(alternate_blank_steps);
#undef TIMING_STATUS
  sony_timing_info_t info = sony_timing_info(&timing);
  cJSON_AddNumberToObject(root, "timing_slot_us", info.slot_samples * 0.125);
  cJSON_AddNumberToObject(root, "timing_cycle_hz",
                          8000000.0 / info.cycle_samples);
  cJSON_AddStringToObject(root, "timing_backend",
                          timing.pair   ? "GPIO pair; blanked software gaps"
                          : timing.hold ? "GPIO preload / CLEAR DMA"
                                        : "six-line DMA");
  cJSON_AddNumberToObject(root, "active_held_slot",
                          atomic_load(&active_held_slot));
  cJSON_AddNumberToObject(root, "held_switches", atomic_load(&held_switches));
  cJSON_AddNumberToObject(root, "held_last_interval_us",
                          atomic_load(&held_last_interval_us));
  cJSON_AddNumberToObject(root, "timing_grid_duty",
                          timing.hold ? (double)timing.on_ns /
                                            (timing.on_ns + timing.off_ns)
                                      : (double)timing.pulses * timing.on_ns /
                                            (info.cycle_samples * 125.0));
  for (unsigned i = 0; i < 26; ++i) {
    char hex[11];
    snprintf(hex, sizeof(hex), "%010llX", (unsigned long long)frame[i]);
    cJSON_AddItemToArray(frame_words, cJSON_CreateString(hex));
  }
  cJSON_AddNumberToObject(root, "completed_frames",
                          atomic_load(&completed_frames));
  cJSON_AddNumberToObject(root, "last_error", atomic_load(&last_error));
  cJSON_AddNullToObject(root, "translator_enabled");
  cJSON_AddNumberToObject(root, "largest_psram_block",
                          heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  cJSON_AddStringToObject(root, "mode", "radio with authorized Sony display");
  cJSON_AddStringToObject(root, "capture_replay",
                          "8 MHz DMA; about 250 Hz; scaled Sony slot timing; "
                          "continuation chunks stream without reinitializing");
  cJSON *levels = cJSON_AddArrayToObject(root, "pins");
  if (!levels) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  }
  for (size_t i = 0; i < 6; ++i) {
    cJSON *pin = cJSON_CreateObject();
    if (!pin) {
      cJSON_Delete(root);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Out of memory");
    }
    cJSON_AddNumberToObject(pin, "gpio", SCREEN_PINS[i]);
    cJSON_AddNumberToObject(pin, "level", atomic_load(&pin_levels[i]));
    cJSON_AddItemToArray(levels, pin);
  }
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t err = httpd_resp_sendstr(req, json);
  free(json);
  return err;
}

static esp_err_t control_handler(httpd_req_t *req) {
  char body[2048];
  if (!req->content_len || req->content_len >= sizeof(body))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "Invalid request size");
  size_t received = 0;
  while (received < req->content_len) {
    int n = httpd_req_recv(req, body + received, req->content_len - received);
    if (n <= 0)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                 "Incomplete request");
    received += n;
  }
  body[received] = 0;
  cJSON *root = cJSON_Parse(body);
  cJSON *sample = cJSON_GetObjectItemCaseSensitive(root, "sample");
  cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(root, "bench_confirmed");
  cJSON *live = cJSON_GetObjectItemCaseSensitive(root, "live_confirmed");
  cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "pin_profile");
  if (profile) {
    if (!cJSON_IsString(profile) || !cJSON_IsTrue(confirmed) || sample ||
        cJSON_GetObjectItemCaseSensitive(root, "gpio") ||
        cJSON_GetObjectItemCaseSensitive(root, "words") ||
        cJSON_GetObjectItemCaseSensitive(root, "timing")) {
      cJSON_Delete(root);
      return httpd_resp_send_err(
          req, HTTPD_400_BAD_REQUEST,
          "Select a pin profile separately, with Sony "
          "signal wires disconnected and bench confirmed");
    }
    esp_err_t err = select_pin_profile(profile->valuestring);
    cJSON_Delete(root);
    if (err == ESP_ERR_INVALID_STATE) {
      httpd_resp_set_status(req, "409 Conflict");
      return httpd_resp_sendstr(req, "Stop output before changing pin profile");
    }
    if (err == ESP_ERR_INVALID_ARG)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                 "Unknown pin profile");
    atomic_store(&last_error, err);
    if (err != ESP_OK)
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Pin profile switch failed");
    return status_handler(req);
  }
  if (cJSON_IsString(sample) && !strcmp(sample->valuestring, "blank")) {
    atomic_store(&stop_requested, true);
    portENTER_CRITICAL(&frame_lock);
    follow_radio = false;
    follow_spectrum = false;
    portEXIT_CRITICAL(&frame_lock);
    sony_spectrum_enable(false);
    if (!atomic_load(&running) && atomic_load(&pin_levels[2]) >= 0) {
      set(GRID_CLEAR, 0);
    }
    cJSON_Delete(root);
    return status_handler(req);
  }
  if (cJSON_IsString(sample) && strcmp(sample->valuestring, "stop") == 0) {
    atomic_store(&stop_requested, true);
    portENTER_CRITICAL(&frame_lock);
    follow_radio = false;
    follow_spectrum = false;
    portEXIT_CRITICAL(&frame_lock);
    sony_spectrum_enable(false);
    if (!atomic_load(&running))
      restore_idle_outputs();
    cJSON_Delete(root);
    return status_handler(req);
  }
  cJSON *gpio = cJSON_GetObjectItemCaseSensitive(root, "gpio");
  cJSON *level = cJSON_GetObjectItemCaseSensitive(root, "level");
  if (gpio) {
    int assigned = -1;
    if (cJSON_IsNumber(gpio) && gpio->valuedouble == gpio->valueint)
      for (size_t i = 0; i < 6; ++i)
        if (gpio->valueint == SCREEN_PINS[i])
          assigned = (int)i;
    if (!cJSON_IsTrue(confirmed) || !cJSON_IsNumber(gpio) || assigned < 0 ||
        !cJSON_IsNumber(level) || level->valuedouble != level->valueint ||
        level->valueint < 0 || level->valueint > 1) {
      cJSON_Delete(root);
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                 "Invalid GPIO/level or bench not confirmed");
    }
    if (atomic_load(&running)) {
      cJSON_Delete(root);
      httpd_resp_set_status(req, "409 Conflict");
      return httpd_resp_sendstr(req, "Stop scan before setting a static level");
    }
    esp_err_t err = set((gpio_num_t)gpio->valueint, level->valueint);
    cJSON_Delete(root);
    atomic_store(&last_error, err);
    if (err != ESP_OK) {
      restore_idle_outputs();
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "GPIO control failed");
    }
    return status_handler(req);
  }
  if (cJSON_IsString(sample) && !strcmp(sample->valuestring, "Text")) {
    cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    cJSON *scroll = cJSON_GetObjectItemCaseSensitive(root, "scroll");
    cJSON *dim = cJSON_GetObjectItemCaseSensitive(root, "dim");
    cJSON *width = cJSON_GetObjectItemCaseSensitive(root, "width");
    if ((!cJSON_IsTrue(confirmed) && !cJSON_IsTrue(live)) ||
        !cJSON_IsString(text) || (scroll && !cJSON_IsBool(scroll)) ||
        (dim && !cJSON_IsBool(dim)) ||
        (width && (!cJSON_IsNumber(width) || width->valuedouble != width->valueint ||
                   width->valueint < 1 || width->valueint > 22))) {
      cJSON_Delete(root);
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                 "Confirm bench/live and provide UTF-8 text");
    }
    esp_err_t err = show_text(text->valuestring, !scroll || cJSON_IsTrue(scroll),
                              !dim || cJSON_IsTrue(dim),
                              width ? (unsigned)width->valueint : 9, NULL);
    cJSON_Delete(root);
    if (err == ESP_ERR_INVALID_STATE) {
      httpd_resp_set_status(req, "409 Conflict");
      return httpd_resp_sendstr(req, "Start authorized Display scan first");
    }
    if (err != ESP_OK)
      return httpd_resp_send_err(req,
                                 err == ESP_ERR_NO_MEM
                                     ? HTTPD_500_INTERNAL_SERVER_ERROR
                                     : HTTPD_400_BAD_REQUEST,
                                 "Invalid UTF-8, text too long or no memory");
    return status_handler(req);
  }
  bool timed = cJSON_IsString(sample) && !strcmp(sample->valuestring, "Timing");
  if (cJSON_IsString(sample) &&
      (!strcmp(sample->valuestring, "Display") || timed)) {
    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "words");
    uint64_t frame[26];
    sony_timing_t timing = sony_timing_defaults();
    cJSON *radio = cJSON_GetObjectItemCaseSensitive(root, "now_playing");
    cJSON *visualization = cJSON_GetObjectItemCaseSensitive(root, "visualization");
    if (visualization &&
        (!cJSON_IsString(visualization) || !timed || !cJSON_IsTrue(radio) ||
         (strcmp(visualization->valuestring, "now_playing") &&
          strcmp(visualization->valuestring, "spectrum")))) {
      cJSON_Delete(root);
      return httpd_resp_send_err(
          req, HTTPD_400_BAD_REQUEST,
          "Choose now_playing/spectrum on a radio Timing scan");
    }
    bool spectrum = cJSON_IsString(visualization) &&
                    !strcmp(visualization->valuestring, "spectrum");
    if (radio && (!cJSON_IsBool(radio) || !timed)) {
      cJSON_Delete(root);
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                 "NOW PLAYING requires a Timing scan");
    }
    if (timed) {
      cJSON *config = cJSON_GetObjectItemCaseSensitive(root, "timing");
      bool valid_timing = cJSON_IsObject(config);
#define TIMING_PARSE(field)                                                 \
  do {                                                                      \
    cJSON *item = cJSON_GetObjectItemCaseSensitive(config, #field);         \
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||                   \
        item->valuedouble > 5000000 || item->valuedouble != item->valueint) \
      valid_timing = false;                                                 \
    else                                                                    \
      timing.field = (uint32_t)item->valueint;                              \
  } while (0)
      TIMING_PARSE(setup_ns);
      TIMING_PARSE(clock_low_ns);
      TIMING_PARSE(clock_high_ns);
      TIMING_PARSE(strobe_ns);
      TIMING_PARSE(latch_guard_ns);
      TIMING_PARSE(blank_before_ns);
      TIMING_PARSE(blank_after_ns);
      TIMING_PARSE(on_ns);
      TIMING_PARSE(off_ns);
      TIMING_PARSE(pulses);
      TIMING_PARSE(hold);
      TIMING_PARSE(slot);
      if (cJSON_GetObjectItemCaseSensitive(config, "pair"))
        TIMING_PARSE(pair);
      if (cJSON_GetObjectItemCaseSensitive(config, "alternate_ms"))
        TIMING_PARSE(alternate_ms);
      if (cJSON_GetObjectItemCaseSensitive(config, "alternate_blank_steps"))
        TIMING_PARSE(alternate_blank_steps);
#undef TIMING_PARSE
      const char *error = valid_timing ? sony_timing_validate(&timing)
                                       : "Provide all integer timing fields";
      /* Slot zero is valid as the internally selected second grid, not
       * as an alternation origin (which would underflow slot-1). */
      if (!error && timing.alternate_ms && !timing.slot)
        error = "Alternation first slot must be 1..25";
      if (!error && cJSON_IsTrue(radio) &&
          (timing.hold || timing.pair || timing.alternate_ms))
        error = "NOW PLAYING requires the full 26-slot scan";
      if (error) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
      }
    }
    bool valid = cJSON_IsTrue(confirmed) || cJSON_IsTrue(live);
    valid = valid && cJSON_IsArray(array) && cJSON_GetArraySize(array) == 26;
    for (unsigned i = 0; i < 26 && valid; ++i) {
      cJSON *item = cJSON_GetArrayItem(array, i);
      if (!cJSON_IsString(item) || strlen(item->valuestring) != 10 ||
          strspn(item->valuestring, "0123456789abcdefABCDEF") != 10) {
        valid = false;
        break;
      }
      frame[i] = strtoull(item->valuestring, NULL, 16);
    }
    if (!valid) {
      cJSON_Delete(root);
      return httpd_resp_send_err(
          req, HTTPD_400_BAD_REQUEST,
          "Confirm isolation/bench and provide 26 ten-digit hex words");
    }
    if (spectrum && sony_spectrum_init() != ESP_OK) {
      cJSON_Delete(root);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Cannot allocate spectrum analyzer");
    }
    if (atomic_load(&running)) {
      if (!atomic_load(&display_requested) || atomic_load(&stop_requested) ||
          timed != atomic_load(&timing_requested)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req,
                                  "Stop other driver before changing backend");
      }
      portENTER_CRITICAL(&frame_lock);
      bool leaving_spectrum = follow_spectrum && !spectrum;
      scrolling_text.active = false;
      follow_radio = cJSON_IsTrue(radio);
      follow_spectrum = spectrum;
      display_dim = true;
      memcpy(words, frame, sizeof(words));
      if (leaving_spectrum && follow_radio)
        sony_radio_status_apply_view(words, &last_radio_status, false);
      if (timed) {
        display_timing = timing;
        atomic_fetch_add(&timing_revision, 1);
      }
      portEXIT_CRITICAL(&frame_lock);
      sony_spectrum_enable(spectrum);
      cJSON_Delete(root);
      return status_handler(req);
    }
    if (atomic_exchange(&running, true)) {
      cJSON_Delete(root);
      httpd_resp_set_status(req, "409 Conflict");
      return httpd_resp_sendstr(req, "Scan already running");
    }
    portENTER_CRITICAL(&frame_lock);
    scrolling_text.active = false;
    follow_radio = cJSON_IsTrue(radio);
    follow_spectrum = spectrum;
    display_dim = true;
    memcpy(words, frame, sizeof(words));
    if (timed) {
      display_timing = timing;
      atomic_fetch_add(&timing_revision, 1);
    }
    portEXIT_CRITICAL(&frame_lock);
    atomic_store(&display_requested, true);
    atomic_store(&timing_requested, timed);
    atomic_store(&completed_frames, 0);
    atomic_store(&held_switches, 0);
    atomic_store(&held_last_interval_us, 0);
    atomic_store(&last_error, ESP_OK);
    atomic_store(&stop_requested, false);
    sony_spectrum_enable(spectrum);
    cJSON_Delete(root);
    if (xTaskCreatePinnedToCore(scan_task, "screen_display", 4096, NULL, 5,
                                NULL, 1) != pdPASS) {
      sony_spectrum_enable(false);
      atomic_store(&running, false);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Cannot start display");
    }
    return status_handler(req);
  }
  bool captured = cJSON_IsString(sample) &&
                  (!strcmp(sample->valuestring, "Captured blink") ||
                   !strcmp(sample->valuestring, "Captured Frequency") ||
                   !strcmp(sample->valuestring, "MONO 1/2 test"));
  bool authorized = cJSON_IsTrue(confirmed) || (captured && cJSON_IsTrue(live));
  if (!cJSON_IsString(sample) || !authorized ||
      (strcmp(sample->valuestring, "Frequency") &&
       strcmp(sample->valuestring, "No DSR") && !captured)) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "Confirm disconnected bench, or explicitly "
                               "confirm live captured replay");
  }
  if (atomic_exchange(&running, true)) {
    cJSON_Delete(root);
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_sendstr(req, "Scan already running");
  }
  atomic_store(&display_requested, false);
  capture_requested = captured;
  frequency_capture_requested =
      !strcmp(sample->valuestring, "Captured Frequency");
  mono_test_requested = !strcmp(sample->valuestring, "MONO 1/2 test");
  const uint64_t *text =
      !strcmp(sample->valuestring, "Frequency") ? frequency : no_dsr;
  portENTER_CRITICAL(&frame_lock);
  memset(words, 0, sizeof(words));
  memcpy(words + 14, text, sizeof(frequency));
  portEXIT_CRITICAL(&frame_lock);
  cJSON_Delete(root);
  atomic_store(&completed_frames, 0);
  atomic_store(&last_error, ESP_OK);
  atomic_store(&stop_requested, false);
  if (xTaskCreatePinnedToCore(scan_task, "screen_test", 4096, NULL, 5, NULL,
                              1) != pdPASS) {
    atomic_store(&running, false);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Cannot start scan");
  }
  return status_handler(req);
}

esp_err_t sony_screen_register_http(httpd_handle_t server) {
  httpd_uri_t status = {
      .uri = "/api/screen", .method = HTTP_GET, .handler = status_handler};
  httpd_uri_t control = {
      .uri = "/api/screen", .method = HTTP_POST, .handler = control_handler};
  esp_err_t err = httpd_register_uri_handler(server, &status);
  if (err == ESP_OK)
    err = httpd_register_uri_handler(server, &control);
  return err;
}
