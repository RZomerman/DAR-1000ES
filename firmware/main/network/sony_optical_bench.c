#include "sony_optical_bench.h"

#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

_Static_assert(CONFIG_SPDIF_DO_IO == 15, "Bench optical data must use GPIO15");

static esp_err_t chip2_replay(httpd_req_t *req, bool with_init) {
  const gpio_num_t att = GPIO_NUM_11, shift = GPIO_NUM_12, latch = GPIO_NUM_13;
  const gpio_num_t init = GPIO_NUM_10, oe = GPIO_NUM_14;
  const uint16_t words[] = {0x442c, 0x0400};  // Captured bits, first bit at bit 0.
  static portMUX_TYPE timing_lock = portMUX_INITIALIZER_UNLOCKED;
  gpio_config_t outputs = {.pin_bit_mask = (1ULL << att) | (1ULL << shift) |
                                           (1ULL << latch) | (1ULL << oe),
                            .mode = GPIO_MODE_OUTPUT};
  gpio_set_level(oe, 1);
  gpio_set_level(att, 0);
  gpio_set_level(shift, 0);
  gpio_set_level(latch, 0);
  if (with_init) {
    gpio_set_level(init, 0);
    outputs.pin_bit_mask |= 1ULL << init;
  }
  if (gpio_config(&outputs) != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Chip 2 setup failed.");
  gpio_set_level(oe, 0);
  // Bench-only reset hypothesis: keep INIT low while the buffer is enabled.
  vTaskDelay(pdMS_TO_TICKS(100));
  if (with_init) {
    gpio_set_level(init, 1);
    // Approximate the captured 809 ms to the first short LATCH window.
    vTaskDelay(pdMS_TO_TICKS(810));
  }
  portENTER_CRITICAL(&timing_lock);
  for (unsigned w = 0; w < 2; ++w) {
    gpio_set_level(latch, 1);
    esp_rom_delay_us(8);
    for (unsigned b = 0; b < 16; ++b) {
      gpio_set_level(att, (words[w] >> b) & 1);
      esp_rom_delay_us(2);
      gpio_set_level(shift, 1);
      esp_rom_delay_us(2);
      gpio_set_level(shift, 0);
    }
    esp_rom_delay_us(5);
    gpio_set_level(latch, 0);
    if (!w) esp_rom_delay_us(32);
  }
  portEXIT_CRITICAL(&timing_lock);
  vTaskDelay(pdMS_TO_TICKS(30));
  gpio_set_level(oe, 1);
  outputs.pin_bit_mask &= ~(1ULL << oe);
  outputs.mode = GPIO_MODE_INPUT;
  gpio_config(&outputs);
  return httpd_resp_sendstr(req, with_init ?
      "INIT held LOW, released HIGH, and two captured words replayed." :
      "Two captured words replayed; INIT untouched.");
}

static esp_err_t pcm_pin_levels(httpd_req_t *req, const gpio_num_t *pins,
                                unsigned count, gpio_num_t enable) {
  gpio_config_t outputs = {.mode = GPIO_MODE_OUTPUT};
  esp_err_t err = ESP_OK;
  for (unsigned i = 0; i < count && err == ESP_OK; ++i) {
    outputs.pin_bit_mask |= 1ULL << pins[i];
    err = gpio_set_level(pins[i], 0);
  }
  if (err == ESP_OK) err = gpio_config(&outputs);
  if (err == ESP_OK) err = gpio_set_level(enable, 1);
  if (err == ESP_OK) err = gpio_set_direction(enable, GPIO_MODE_OUTPUT);
  for (int oe = 1; oe >= 0 && err == ESP_OK; --oe) {
    gpio_set_level(enable, oe);
    vTaskDelay(pdMS_TO_TICKS(160));
    for (unsigned i = 0; i < count; ++i) {
      gpio_set_level(pins[i], 1);
      vTaskDelay(pdMS_TO_TICKS(80 + 40 * i));
      gpio_set_level(pins[i], 0);
      vTaskDelay(pdMS_TO_TICKS(60));
    }
  }
  gpio_set_level(enable, 1);
  outputs.mode = GPIO_MODE_INPUT;
  gpio_config(&outputs);
  gpio_set_direction(enable, GPIO_MODE_INPUT);
  if (err != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "PCM pin setup failed.");
  return httpd_resp_sendstr(req, "PCM pin test complete; GPIOs released.");
}

esp_err_t sony_optical_bench_init(void) {
  gpio_set_level(GPIO_NUM_5, 1);
  gpio_config_t config = {.pin_bit_mask = 1ULL << 5, .mode = GPIO_MODE_OUTPUT};
  esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) return err;
  config.pin_bit_mask = (1ULL << 4) | (1ULL << 6) | (1ULL << 7);
  gpio_set_level(GPIO_NUM_4, 0);
  gpio_set_level(GPIO_NUM_6, 0);
  gpio_set_level(GPIO_NUM_7, 0);
  return gpio_config(&config);
}

esp_err_t sony_optical_bench_run(httpd_req_t *req) {
  char confirm[24];
  if (req->content_len || httpd_req_get_hdr_value_str(
      req, "X-Sony-Bench", confirm, sizeof(confirm)) != ESP_OK ||
      strcmp(confirm, "outputs-disconnected"))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Confirm isolated outputs.");

  char mode[sizeof("chip2-replay")];
  if (httpd_req_get_hdr_value_str(req, "X-Sony-Bench-Mode", mode,
                                  sizeof(mode)) == ESP_OK) {
    if (!strcmp(mode, "chip1")) {
      const gpio_num_t pins[] = {GPIO_NUM_42, GPIO_NUM_40, GPIO_NUM_41};
      return pcm_pin_levels(req, pins, 3, GPIO_NUM_1);
    }
    if (!strcmp(mode, "chip2")) {
      const gpio_num_t pins[] = {GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13};
      return pcm_pin_levels(req, pins, 4, GPIO_NUM_14);
    }
    if (!strcmp(mode, "chip2-replay"))
      return chip2_replay(req, false);
    if (!strcmp(mode, "chip2-init"))
      return chip2_replay(req, true);
  }

  if (httpd_req_get_hdr_value_str(req, "X-Sony-Bench-Mode", mode,
                                  sizeof(mode)) == ESP_OK && !strcmp(mode, "levels")) {
    const gpio_num_t pins[] = {GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_15};
    int low[4], high[4];
    for (unsigned i = 0; i < 4; ++i) {
      if (gpio_input_enable(pins[i]) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "GPIO readback setup failed.");
      gpio_set_level(pins[i], 0);
    }
    esp_rom_gpio_connect_out_signal(GPIO_NUM_15, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_level(GPIO_NUM_5, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    for (unsigned i = 0; i < 4; ++i) {
      low[i] = gpio_get_level(pins[i]);
      gpio_set_level(pins[i], 1);
      vTaskDelay(pdMS_TO_TICKS(80 + 40 * i));
      high[i] = gpio_get_level(pins[i]);
      gpio_set_level(pins[i], 0);
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    gpio_set_level(GPIO_NUM_5, 1);
    esp_rom_gpio_connect_out_signal(GPIO_NUM_15, I2S0O_SD_OUT_IDX, false, false);
    char result[96];
    snprintf(result, sizeof(result), "Pad LOW/HIGH: GPIO4=%d/%d GPIO6=%d/%d GPIO7=%d/%d GPIO15=%d/%d",
             low[0], high[0], low[1], high[1], low[2], high[2], low[3], high[3]);
    return httpd_resp_sendstr(req, result);
  }

  if (httpd_req_get_hdr_value_str(req, "X-Sony-Bench-Mode", mode,
                                  sizeof(mode)) == ESP_OK && !strcmp(mode, "oe")) {
    gpio_set_level(GPIO_NUM_6, 0);
    gpio_set_level(GPIO_NUM_5, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GPIO_NUM_5, 1);
    vTaskDelay(pdMS_TO_TICKS(170));
    gpio_set_level(GPIO_NUM_5, 0);
    vTaskDelay(pdMS_TO_TICKS(220));
    gpio_set_level(GPIO_NUM_5, 1);
    return httpd_resp_sendstr(req, "/OE test complete; buffer disabled.");
  }

  gpio_set_level(GPIO_NUM_5, 0);
  vTaskDelay(pdMS_TO_TICKS(160));
  gpio_set_level(GPIO_NUM_6, 1);
  vTaskDelay(pdMS_TO_TICKS(80));
  gpio_set_level(GPIO_NUM_6, 0);
  vTaskDelay(pdMS_TO_TICKS(80));
  gpio_set_level(GPIO_NUM_7, 1);
  vTaskDelay(pdMS_TO_TICKS(80));
  gpio_set_level(GPIO_NUM_7, 0);
  vTaskDelay(pdMS_TO_TICKS(80));

  // Keep the encoder running; force the output pad low during the mute phase.
  gpio_set_level(GPIO_NUM_15, 0);
  gpio_set_level(GPIO_NUM_4, 1);
  esp_rom_gpio_connect_out_signal(GPIO_NUM_15, SIG_GPIO_OUT_IDX, false, false);
  vTaskDelay(pdMS_TO_TICKS(180));
  esp_rom_gpio_connect_out_signal(GPIO_NUM_15, I2S0O_SD_OUT_IDX, false, false);
  gpio_set_level(GPIO_NUM_4, 0);
  vTaskDelay(pdMS_TO_TICKS(160));
  gpio_set_level(GPIO_NUM_5, 1);
  return httpd_resp_sendstr(req, "Bench sequence complete; /OE disabled.");
}
