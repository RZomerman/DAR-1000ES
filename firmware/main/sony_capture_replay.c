#include "sony_capture_replay.h"
#include "sony_display_frame.h"
#include "sony_pins.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#define SCAN_PREFIX   (16 + 40 * 64 + 16)
#define SCAN_CAPACITY (8 * 26 * 2500 + 4096)

struct sony_scan_engine {
  esp_lcd_i80_bus_handle_t bus;
  esp_lcd_panel_io_handle_t io;
  uint8_t *templates[2];
  uint8_t *buffers[2];
  size_t bytes;
  size_t continuation_bytes;
  size_t samples;
  size_t *falls;
  unsigned submitted;
  unsigned in_flight;
  bool timed;
  bool held;
  sony_timing_t timing;
  uint64_t words[26];
};

extern const uint8_t replay_start[] asm("_binary_mono_blink_rle_start");
extern const uint8_t replay_end[] asm("_binary_mono_blink_rle_end");
extern const uint8_t frequency_start[] asm("_binary_frequency_rle_start");
extern const uint8_t frequency_end[] asm("_binary_frequency_rle_end");

static uint32_t read_u32(const uint8_t *data) {
  return data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 |
         (uint32_t)data[3] << 24;
}

static bool transfer_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *event, void *context) {
  (void)io;
  (void)event;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR((TaskHandle_t)context, &woken);
  return woken == pdTRUE;
}

static esp_err_t run_waveform(bool frequency, bool mono_test,
                              const uint64_t *words, bool dim,
                              uint8_t *final_state, uint32_t *frames) {
  const uint8_t *start = frequency ? frequency_start : replay_start;
  const uint8_t *end = frequency ? frequency_end : replay_end;
  if (!final_state || !frames || end - start < 17 || memcmp(start, "SCR2", 4))
    return ESP_ERR_INVALID_SIZE;
  uint32_t samples = read_u32(start + 4);
  *frames = read_u32(start + 8);
  if (!samples || samples > 8200000)
    return ESP_ERR_INVALID_SIZE;
  uint32_t original_samples = samples;
  if (words)
    samples = 8 * 26 * 2500 + 4096;
  const size_t prefix = 16 + 40 * 64 + 16;
  const size_t suffix = 160;
  size_t bytes = (prefix + samples + suffix + 63U) & ~63U;
  uint8_t *wave =
      heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!wave)
    return ESP_ERR_NO_MEM;
  size_t offset = 0;
  for (const uint8_t *run = start + 17; run < end; run += 3) {
    if (end - run < 3) {
      free(wave);
      return ESP_ERR_INVALID_SIZE;
    }
    size_t count = run[0] | (size_t)run[1] << 8;
    if (!count || run[2] > 63 || (!words && count > samples - offset)) {
      free(wave);
      return ESP_ERR_INVALID_SIZE;
    }
    if (words && count > samples - offset)
      count = samples - offset;
    memset(wave + prefix + offset, run[2], count);
    offset += count;
    if (words && offset == samples)
      break;
  }
  if (offset != samples) {
    free(wave);
    return ESP_ERR_INVALID_SIZE;
  }
  if (words) {
    size_t boundary =
        sony_display_waveform_brightness(wave + prefix, samples, words, dim);
    if (!boundary || boundary >= original_samples) {
      free(wave);
      return ESP_ERR_INVALID_SIZE;
    }
    samples = boundary;
    bytes = (prefix + samples + suffix + 63U) & ~63U;
    *frames = 8;
  }
  if (mono_test) {
    /* Keep captured clocks/blanking/grid timing, replacing only serial DATA.
     * Each slot shifts the word displayed at the following latch window. */
    unsigned clocks = 0;
    uint8_t data = 0;
    for (size_t i = 0; i < samples; ++i) {
      uint8_t state = wave[prefix + i];
      if (i && (wave[prefix + i - 1] & 1) && !(state & 1)) {
        unsigned next_slot = clocks / 40 + 1;
        uint64_t word =
            next_slot % 26 == 24 ? (next_slot / 26 < 65 ? 0x08 : 0x04) : 0;
        data = ((word >> (39 - clocks % 40)) & 1) << 1;
        ++clocks;
      }
      wave[prefix + i] = (state & ~0x02) | data;
    }
  }
  /* CL LOW blanks grids, STB HIGH exposes the registers. Forty deliberate
   * shifts replace all prior contents: preload slot zero's segment word and
   * a grid one at OUT1, matching the captured grid-marker boundary. */
  uint64_t initial_segment = 0;
  for (unsigned i = 0; i < 5; ++i)
    initial_segment |= (uint64_t)start[12 + i] << (8 * i);
  if (words)
    initial_segment = words[0];
  memset(wave, 0x29, 16);
  size_t pos = 16;
  for (int bit = 39; bit >= 0; --bit) {
    uint8_t state =
        0x29 | (((initial_segment >> bit) & 1) << 1) | (bit == 0 ? 0x10 : 0);
    memset(wave + pos, state, 16);
    memset(wave + pos + 16, state & ~0x21, 16);
    memset(wave + pos + 32, state, 32);
    pos += 64;
  }
  memset(wave + pos, wave[prefix], 16);
  /* End with actual grid CL LOW and latch held. Leave both clocks HIGH. */
  *final_state = 0x21;
  memset(wave + prefix + samples, *final_state, bytes - prefix - samples);
  /* Invert the two peripheral clock-data lanes so peripheral idle zero
   * produces physical clock HIGH, including completion and handover. */
  for (size_t i = 0; i < bytes; ++i)
    wave[i] ^= 0x21;

  esp_err_t err = gpio_set_level(SONY_CLEAR, 0);
  if (err == ESP_OK)
    err = gpio_set_level(SONY_STROBE, 0);
  if (err == ESP_OK)
    err = gpio_set_level(SONY_SEGMENT_DATA, 0);
  if (err == ESP_OK)
    err = gpio_set_level(SONY_GRID_DATA, 0);
  gpio_config_t clear_config = {.pin_bit_mask = 1ULL << SONY_CLEAR,
                                .mode = GPIO_MODE_OUTPUT};
  if (err == ESP_OK)
    err = gpio_config(&clear_config);
  const int clocks[] = {SONY_SEGMENT_CLOCK, SONY_GRID_CLOCK};
  for (unsigned i = 0; i < 2 && err == ESP_OK; ++i) {
    err = gpio_set_level(clocks[i], 1);
    gpio_config_t config = {.pin_bit_mask = 1ULL << clocks[i],
                            .mode = GPIO_MODE_OUTPUT};
    if (err == ESP_OK)
      err = gpio_config(&config);
  }
  if (err != ESP_OK) {
    free(wave);
    return err;
  }

  esp_lcd_i80_bus_handle_t bus = NULL;
  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_i80_bus_config_t bus_config = {
      .dc_gpio_num = 14,
      .wr_gpio_num = 13,
      .clk_src = LCD_CLK_SRC_DEFAULT,
      /* Clock lanes initially go to unconnected pins, not Sony. */
      .data_gpio_nums = {12, SONY_SEGMENT_DATA, SONY_CLEAR, SONY_STROBE,
                         SONY_GRID_DATA, 12, 12, 12},
      .bus_width = 8,
      .max_transfer_bytes = bytes,
      .dma_burst_size = 64,
  };
  err = esp_lcd_new_i80_bus(&bus_config, &bus);
  if (err == ESP_OK) {
    esp_rom_gpio_connect_out_signal(SONY_SEGMENT_CLOCK, LCD_DATA_OUT0_IDX, true,
                                    false);
    esp_rom_gpio_connect_out_signal(SONY_GRID_CLOCK, LCD_DATA_OUT5_IDX, true,
                                    false);
    esp_lcd_panel_io_i80_config_t config = {
        .cs_gpio_num = -1,
        .pclk_hz = 8000000,
        .trans_queue_depth = 1,
        .on_color_trans_done = transfer_done,
        .user_ctx = xTaskGetCurrentTaskHandle(),
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_i80(bus, &config, &io);
  }
  if (err == ESP_OK) {
    /* One DMA transaction avoids software gaps between slots and frames.
     * -1 suppresses a command cycle on the six waveform data lines. */
    err = esp_lcd_panel_io_tx_color(io, -1, wave, bytes);
    if (err == ESP_OK)
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  /* Detach clocks before deleting the peripheral; the GPIO registers already
   * contain HIGH, so ownership changes do not create a clock pulse. */
  for (unsigned i = 0; i < 2; ++i) {
    gpio_set_level(clocks[i], 1);
    esp_rom_gpio_connect_out_signal(clocks[i], SIG_GPIO_OUT_IDX, false, false);
  }
  gpio_set_level(SONY_CLEAR, 0);
  esp_rom_gpio_connect_out_signal(SONY_CLEAR, SIG_GPIO_OUT_IDX, false, false);
  if (io) {
    esp_err_t cleanup = esp_lcd_panel_io_del(io);
    if (err == ESP_OK)
      err = cleanup;
  }
  if (bus) {
    esp_err_t cleanup = esp_lcd_del_i80_bus(bus);
    if (err == ESP_OK)
      err = cleanup;
  }
  free(wave);
  /* Restore GPIO ownership without claiming these levels blank the VFD. */
  const int gpios[] = {SONY_SEGMENT_CLOCK, SONY_SEGMENT_DATA,
                              SONY_CLEAR,         SONY_STROBE,
                              SONY_GRID_DATA,     SONY_GRID_CLOCK};
  for (unsigned bit = 0; bit < 6; ++bit) {
    esp_err_t cleanup = gpio_set_level(gpios[bit], (*final_state >> bit) & 1);
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << gpios[bit],
                         .mode = GPIO_MODE_OUTPUT};
    if (cleanup == ESP_OK)
      cleanup = gpio_config(&cfg);
    if (err == ESP_OK)
      err = cleanup;
  }
  for (int gpio = 12; gpio <= 14; ++gpio) {
    esp_err_t cleanup = gpio_reset_pin(gpio);
    if (err == ESP_OK)
      err = cleanup;
  }
  return err;
}

esp_err_t sony_capture_replay(bool frequency, bool mono_test,
                              uint8_t *final_state, uint32_t *frames) {
  return run_waveform(frequency, mono_test, NULL, true, final_state, frames);
}

esp_err_t sony_display_chunk(const uint64_t words[26], uint8_t *final_state,
                             uint32_t *frames) {
  if (!words)
    return ESP_ERR_INVALID_ARG;
  return sony_display_chunk_brightness(words, true, final_state, frames);
}

esp_err_t sony_display_chunk_brightness(const uint64_t words[26], bool dim,
                                        uint8_t *final_state,
                                        uint32_t *frames) {
  if (!words)
    return ESP_ERR_INVALID_ARG;
  return run_waveform(true, false, words, dim, final_state, frames);
}

static esp_err_t blank_and_drain(sony_scan_engine_t *engine) {
  /* GPIO register writes do not override the LCD matrix. Blank the physical
   * grid output first, before clocks/data can lose peripheral ownership. */
  esp_err_t err = gpio_set_level(SONY_CLEAR, 0);
  esp_rom_gpio_connect_out_signal(SONY_CLEAR, SIG_GPIO_OUT_IDX, false, false);
  if (err == ESP_OK)
    err = gpio_set_direction(SONY_CLEAR, GPIO_MODE_OUTPUT);
  esp_rom_delay_us(2);
  while (engine->in_flight) {
    if (!ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000))) {
      err = ESP_ERR_TIMEOUT;
      break;
    }
    --engine->in_flight;
  }
  return err;
}

esp_err_t sony_scan_blank(sony_scan_engine_t *engine) {
  if (!engine || !engine->held)
    return ESP_ERR_INVALID_ARG;
  esp_err_t err = blank_and_drain(engine);
  if (err == ESP_OK)
    engine->submitted = 0; /* Force a preload and reconnect CLEAR on resume. */
  return err;
}

esp_err_t sony_scan_close(sony_scan_engine_t *engine) {
  if (!engine)
    return ESP_OK;
  esp_err_t err = blank_and_drain(engine);
  gpio_set_level(SONY_STROBE, 0);
  gpio_set_level(SONY_SEGMENT_DATA, 0);
  gpio_set_level(SONY_GRID_DATA, 0);
  gpio_set_level(SONY_SEGMENT_CLOCK, 1);
  gpio_set_level(SONY_GRID_CLOCK, 1);
  const int pins[] = {SONY_SEGMENT_CLOCK, SONY_SEGMENT_DATA, SONY_CLEAR,
                      SONY_STROBE,        SONY_GRID_DATA,    SONY_GRID_CLOCK};
  for (unsigned i = 0; i < 6; ++i) {
    esp_rom_gpio_connect_out_signal(pins[i], SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
  }
  if (engine->io) {
    esp_err_t cleanup = esp_lcd_panel_io_del(engine->io);
    if (err == ESP_OK)
      err = cleanup;
  }
  if (engine->bus) {
    esp_err_t cleanup = esp_lcd_del_i80_bus(engine->bus);
    if (err == ESP_OK)
      err = cleanup;
  }
  for (unsigned i = 0; i < 2; ++i) {
    free(engine->templates[i]);
    free(engine->buffers[i]);
  }
  free(engine->falls);
  free(engine);
  for (int pin = 12; pin <= 14; ++pin)
    gpio_reset_pin(pin);
  return err;
}

static esp_err_t scan_open(sony_scan_engine_t **result, bool timed, bool held) {
  if (!result)
    return ESP_ERR_INVALID_ARG;
  *result = NULL;
  gpio_set_level(SONY_CLEAR, 0);
  gpio_set_level(SONY_STROBE, 0);
  gpio_set_level(SONY_SEGMENT_DATA, 0);
  gpio_set_level(SONY_GRID_DATA, 0);
  gpio_set_level(SONY_SEGMENT_CLOCK, 1);
  gpio_set_level(SONY_GRID_CLOCK, 1);
  sony_scan_engine_t *engine = calloc(1, sizeof(*engine));
  if (!engine)
    return ESP_ERR_NO_MEM;
  engine->timed = timed;
  engine->held = held;
  esp_err_t err = ESP_ERR_NO_MEM;
  if (timed) {
    engine->bytes = SONY_TIMING_CAPACITY;
    for (unsigned i = 0; i < 2; ++i) {
      engine->buffers[i] = heap_caps_aligned_alloc(
          64, engine->bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!engine->buffers[i])
        goto fail;
    }
    goto configure_bus;
  }
  const size_t capacity = (SCAN_PREFIX + SCAN_CAPACITY + 223) & ~63U;
  engine->falls = malloc((8 * 26 * 40 + 1) * sizeof(size_t));
  if (!engine->falls)
    goto fail;
  for (unsigned i = 0; i < 2; ++i) {
    engine->templates[i] = heap_caps_aligned_alloc(
        64, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    engine->buffers[i] = heap_caps_aligned_alloc(
        64, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!engine->templates[i] || !engine->buffers[i])
      goto fail;
  }
  err = ESP_ERR_INVALID_SIZE;
  if (frequency_end - frequency_start < 17 ||
      memcmp(frequency_start, "SCR2", 4))
    goto fail;
  uint8_t *raw = engine->templates[0] + SCAN_PREFIX;
  size_t offset = 0;
  for (const uint8_t *run = frequency_start + 17;
       run < frequency_end && offset < SCAN_CAPACITY; run += 3) {
    if (frequency_end - run < 3)
      goto fail;
    size_t count = run[0] | (size_t)run[1] << 8;
    if (!count || run[2] > 63)
      goto fail;
    if (count > SCAN_CAPACITY - offset)
      count = SCAN_CAPACITY - offset;
    memset(raw + offset, run[2], count);
    offset += count;
  }
  if (offset != SCAN_CAPACITY)
    goto fail;
  memcpy(engine->templates[1] + SCAN_PREFIX, raw, SCAN_CAPACITY);
  const uint64_t empty[26] = {0};
  engine->samples = sony_display_waveform(raw, SCAN_CAPACITY, empty);
  if (!engine->samples || sony_display_waveform_brightness(
                              engine->templates[1] + SCAN_PREFIX, SCAN_CAPACITY,
                              empty, false) != engine->samples)
    goto fail;
  unsigned clocks = 0;
  for (size_t i = 1; i < engine->samples; ++i)
    if ((raw[i - 1] & 1) && !(raw[i] & 1))
      engine->falls[clocks++] = i;
  if (clocks != 8 * 26 * 40)
    goto fail;
  engine->falls[clocks] = engine->samples;
  engine->bytes = (SCAN_PREFIX + engine->samples + 223) & ~63U;
  engine->continuation_bytes = (engine->samples + 63) & ~63U;
  for (unsigned i = 0; i < 2; ++i) {
    uint8_t *wave = engine->templates[i];
    memset(wave, 0x29, 16);
    size_t pos = 16;
    for (int bit = 39; bit >= 0; --bit) {
      uint8_t state = 0x29 | (bit == 0 ? 0x10 : 0);
      memset(wave + pos, state, 16);
      memset(wave + pos + 16, state & ~0x21, 16);
      memset(wave + pos + 32, state, 32);
      pos += 64;
    }
    memset(wave + pos, wave[SCAN_PREFIX], 16);
    memset(wave + SCAN_PREFIX + engine->samples, 0x21,
           engine->bytes - SCAN_PREFIX - engine->samples);
    for (size_t j = 0; j < engine->bytes; ++j)
      wave[j] ^= 0x21;
  }
configure_bus:
  gpio_set_level(SONY_CLEAR, 0);
  gpio_set_direction(SONY_CLEAR, GPIO_MODE_OUTPUT);
  for (unsigned i = 0; i < 2; ++i) {
    int pin = i ? SONY_GRID_CLOCK : SONY_SEGMENT_CLOCK;
    gpio_set_level(pin, 1);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  }
  if (held) {
    const int static_pins[] = {SONY_SEGMENT_CLOCK, SONY_GRID_CLOCK,
                               SONY_SEGMENT_DATA, SONY_GRID_DATA, SONY_STROBE};
    for (unsigned i = 0; i < 5; ++i) {
      esp_rom_gpio_connect_out_signal(static_pins[i], SIG_GPIO_OUT_IDX, false,
                                      false);
      gpio_set_direction(static_pins[i], GPIO_MODE_OUTPUT);
    }
  }
  esp_lcd_i80_bus_config_t bus_config = {
      .dc_gpio_num = 14,
      .wr_gpio_num = 13,
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_gpio_nums = {12, SONY_SEGMENT_DATA, SONY_CLEAR, SONY_STROBE,
                         SONY_GRID_DATA, 12, 12, 12},
      .bus_width = 8,
      .max_transfer_bytes = engine->bytes,
      .dma_burst_size = 64};
  if (held) {
    /* No Sony clock, DATA or STB pin belongs to LCD/DMA in held mode. */
    for (unsigned bit = 0; bit < 8; ++bit)
      bus_config.data_gpio_nums[bit] = bit == 2 ? SONY_CLEAR : 12;
  }
  err = esp_lcd_new_i80_bus(&bus_config, &engine->bus);
  if (err != ESP_OK)
    goto fail;
  if (!held) {
    esp_rom_gpio_connect_out_signal(SONY_SEGMENT_CLOCK, LCD_DATA_OUT0_IDX, true,
                                    false);
    esp_rom_gpio_connect_out_signal(SONY_GRID_CLOCK, LCD_DATA_OUT5_IDX, true,
                                    false);
  }
  esp_lcd_panel_io_i80_config_t config = {.cs_gpio_num = -1,
                                          .pclk_hz = 8000000,
                                          .trans_queue_depth = 2,
                                          .on_color_trans_done = transfer_done,
                                          .user_ctx =
                                              xTaskGetCurrentTaskHandle(),
                                          .lcd_cmd_bits = 8,
                                          .lcd_param_bits = 8};
  err = esp_lcd_new_panel_io_i80(engine->bus, &config, &engine->io);
  if (err != ESP_OK)
    goto fail;
  ulTaskNotifyTake(pdTRUE, 0);
  *result = engine;
  return ESP_OK;
fail:
  sony_scan_close(engine);
  return err;
}

esp_err_t sony_scan_open(sony_scan_engine_t **result) {
  return scan_open(result, false, false);
}

esp_err_t sony_scan_open_timed(sony_scan_engine_t **result, bool held) {
  return scan_open(result, true, held);
}

static esp_err_t wait_for_buffer(sony_scan_engine_t *engine) {
  if (engine->in_flight < 2)
    return ESP_OK;
  if (!ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000)))
    return ESP_ERR_TIMEOUT;
  --engine->in_flight;
  return ESP_OK;
}

static void held_delay(uint32_t ns) {
  esp_rom_delay_us((ns + 999) / 1000);
}

static esp_err_t held_shift(gpio_num_t clock, gpio_num_t data, bool value,
                            const sony_timing_t *t) {
  esp_err_t err = gpio_set_level(data, value);
  held_delay(t->setup_ns);
  if (err == ESP_OK)
    err = gpio_set_level(clock, 0);
  held_delay(t->clock_low_ns);
  if (err == ESP_OK)
    err = gpio_set_level(clock, 1);
  held_delay(t->clock_high_ns);
  return err;
}

static esp_err_t held_preload(sony_scan_engine_t *engine,
                              const uint64_t words[26],
                              const sony_timing_t *t) {
  esp_err_t err = blank_and_drain(engine);
  if (err != ESP_OK)
    return err;
  err = gpio_set_level(SONY_STROBE, 0);
  held_delay(t->blank_before_ns);
  for (int bit = 39; bit >= 0 && err == ESP_OK; --bit)
    err = held_shift(SONY_SEGMENT_CLOCK, SONY_SEGMENT_DATA,
                     (words[t->slot] >> bit) & 1, t);
  if (err == ESP_OK)
    err = gpio_set_level(SONY_SEGMENT_DATA, 0);
  for (int bit = 39; bit >= 0 && err == ESP_OK; --bit)
    err = held_shift(SONY_GRID_CLOCK, SONY_GRID_DATA, (unsigned)bit == t->slot,
                     t);
  held_delay(t->latch_guard_ns);
  if (err == ESP_OK)
    err = gpio_set_level(SONY_STROBE, 1);
  held_delay(t->strobe_ns);
  if (err == ESP_OK)
    err = gpio_set_level(SONY_STROBE, 0);
  held_delay(t->blank_after_ns);
  if (err == ESP_OK)
    err = gpio_set_level(SONY_GRID_DATA, 0);
  if (err == ESP_OK)
    esp_rom_gpio_connect_out_signal(SONY_CLEAR, LCD_DATA_OUT2_IDX, false,
                                    false);
  return err;
}

esp_err_t sony_scan_submit_timed(sony_scan_engine_t *engine,
                                 const uint64_t words[26],
                                 const sony_timing_t *timing) {
  if (!engine || !engine->timed || !words || sony_timing_validate(timing) ||
      engine->held != (bool)timing->hold)
    return ESP_ERR_INVALID_ARG;
  if (engine->held &&
      (!engine->submitted || engine->timing.slot != timing->slot ||
       engine->words[timing->slot] != words[timing->slot])) {
    esp_err_t err = held_preload(engine, words, timing);
    if (err != ESP_OK)
      return err;
  }
  esp_err_t ready = wait_for_buffer(engine);
  if (ready != ESP_OK)
    return ready;
  bool initialize =
      !engine->submitted || memcmp(&engine->timing, timing, sizeof(*timing)) ||
      (timing->hold && engine->words[timing->slot] != words[timing->slot]);
  uint8_t *wave = engine->buffers[engine->submitted % 2];
  size_t bytes = engine->held
                     ? sony_timing_generate_clear(wave, engine->bytes, timing)
                     : sony_timing_generate(wave, engine->bytes, timing, words,
                                            initialize);
  if (!bytes)
    return ESP_ERR_INVALID_SIZE;
  if (!engine->held)
    for (size_t i = 0; i < bytes; ++i)
      wave[i] ^= 0x21;
  esp_err_t err = esp_lcd_panel_io_tx_color(engine->io, -1, wave, bytes);
  if (err == ESP_OK) {
    ++engine->submitted;
    ++engine->in_flight;
    engine->timing = *timing;
    memcpy(engine->words, words, sizeof(engine->words));
  }
  return err;
}

esp_err_t sony_scan_submit(sony_scan_engine_t *engine, const uint64_t words[26],
                           bool dim) {
  if (!engine || !words)
    return ESP_ERR_INVALID_ARG;
  esp_err_t ready = wait_for_buffer(engine);
  if (ready != ESP_OK)
    return ready;
  uint8_t *wave = engine->buffers[engine->submitted % 2];
  const uint8_t *template = engine->templates[dim ? 0 : 1];
  bool initialize = engine->submitted == 0;
  size_t transfer_bytes =
      initialize ? engine->bytes : engine->continuation_bytes;
  size_t waveform_offset = initialize ? SCAN_PREFIX : 0;
  if (initialize) {
    memcpy(wave, template, engine->bytes);
  } else {
    memcpy(wave, template + SCAN_PREFIX, engine->samples);
    memset(wave + engine->samples, template[SCAN_PREFIX + engine->samples - 1],
           transfer_bytes - engine->samples);
  }
  /* Only DATA-high spans need patching: both cached templates contain zero
   * DATA. */
  if (initialize)
    for (unsigned bit = 0; bit < 40; ++bit)
      if (words[0] & (1ULL << (39 - bit)))
        for (size_t i = 16 + bit * 64; i < 16 + (bit + 1) * 64; ++i)
          wave[i] |= 2;
  for (unsigned clock = 0; clock < 8 * 26 * 40; ++clock)
    if (words[(clock / 40 + 1) % 26] & (1ULL << (39 - clock % 40)))
      for (size_t i = waveform_offset + engine->falls[clock];
           i < waveform_offset + engine->falls[clock + 1]; ++i)
        wave[i] |= 2;
  esp_err_t err =
      esp_lcd_panel_io_tx_color(engine->io, -1, wave, transfer_bytes);
  if (err == ESP_OK) {
    ++engine->submitted;
    ++engine->in_flight;
  }
  return err;
}
