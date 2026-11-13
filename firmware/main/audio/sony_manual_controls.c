#include "sony_manual_controls.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

typedef struct {
  const char *name;
  gpio_num_t gpio;
  int level;
} control_t;

static control_t controls[] = {
    {"d_mute", GPIO_NUM_10, 0},
    {"a_mute", GPIO_NUM_11, 0},
    {"relay", GPIO_NUM_12, -1},
    {"init", GPIO_NUM_2, -1},
    {"att", GPIO_NUM_4, -1},
    {"shift", GPIO_NUM_5, -1},
    {"latch", GPIO_NUM_6, -1},
};
enum { D_MUTE, A_MUTE, RELAY, INIT, ATT, SHIFT, LATCH };
static const char *TAG = "sony_controls";
static SemaphoreHandle_t control_lock;

esp_err_t sony_manual_controls_init(void) {
  control_lock = xSemaphoreCreateMutex();
  if (!control_lock) return ESP_ERR_NO_MEM;

  gpio_config_t input = {
      .pin_bit_mask = (1ULL << GPIO_NUM_10) | (1ULL << GPIO_NUM_11) |
                      (1ULL << GPIO_NUM_12) | (1ULL << GPIO_NUM_2) |
                      (1ULL << GPIO_NUM_4) | (1ULL << GPIO_NUM_5) |
                      (1ULL << GPIO_NUM_6),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t err = gpio_config(&input);
  if (err != ESP_OK) return err;
  for (size_t i = 0; i < 2; ++i) {
    err = gpio_set_level(controls[i].gpio, 0);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(controls[i].gpio, GPIO_MODE_INPUT_OUTPUT);
    if (err != ESP_OK) return err;
  }
  return ESP_OK;
}

static esp_err_t set_pin(control_t *pin, int level) {
  esp_err_t err = gpio_set_level(pin->gpio, (uint32_t)level);
  if (err == ESP_OK && pin->level < 0)
    err = gpio_set_direction(pin->gpio, GPIO_MODE_INPUT_OUTPUT);
  if (err == ESP_OK) pin->level = level;
  return err;
}

esp_err_t sony_manual_controls_set(const char *signal, int level) {
  if (!signal || (level != 0 && level != 1)) return ESP_ERR_INVALID_ARG;
  control_t *pin = NULL;
  for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
    if (strcmp(signal, controls[i].name) == 0) pin = &controls[i];
  if (!pin) return ESP_ERR_INVALID_ARG;
  if (!control_lock) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(control_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t err = set_pin(pin, level);
  xSemaphoreGive(control_lock);
  return err;
}

static void wait_until(int64_t deadline) {
  int64_t remaining;
  while ((remaining = deadline - esp_timer_get_time()) > 0) {
    if (remaining > 2000) vTaskDelay(1);
    else esp_rom_delay_us((uint32_t)remaining);
  }
}

static esp_err_t shift_bits(const char *bits, unsigned high_us, unsigned low_us) {
  for (size_t i = 0; bits[i]; ++i) {
    esp_err_t err = set_pin(&controls[ATT], bits[i] == '1');
    if (err != ESP_OK) return err;
    esp_rom_delay_us(1);
    err = set_pin(&controls[SHIFT], 1);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(high_us);
    err = set_pin(&controls[SHIFT], 0);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(low_us);
  }
  return ESP_OK;
}

static esp_err_t latched_word(const char *bits) {
  esp_err_t err = set_pin(&controls[LATCH], 1);
  if (err != ESP_OK) return err;
  esp_rom_delay_us(10);
  err = shift_bits(bits, 2, 1);
  if (err != ESP_OK) return err;
  esp_rom_delay_us(7);
  return set_pin(&controls[LATCH], 0);
}

esp_err_t sony_manual_controls_replay_startup(void) {
  static const char burst[] =
      "001000101011000000000000000010000000000000000000"
      "000000011001101101000000000001111011011011110000";
  static const char first[] = "0011010000100010";
  static const char second[] = "0000000000100000";
  static const char initial_24[] = "001000101011000000000000";
  if (!control_lock) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(control_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
  if (controls[INIT].level != 1 || controls[D_MUTE].level != 0 ||
      controls[A_MUTE].level != 0) {
    xSemaphoreGive(control_lock);
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t err = set_pin(&controls[LATCH], 0);
  if (err == ESP_OK) err = set_pin(&controls[SHIFT], 0);
  if (err == ESP_OK) err = set_pin(&controls[ATT], 0);
  int64_t start = esp_timer_get_time();
  if (err == ESP_OK) {
    wait_until(start + 57229);
    err = set_pin(&controls[LATCH], 1);
  }
  if (err == ESP_OK) {
    wait_until(start + 125413);
    err = set_pin(&controls[LATCH], 0);
  }
  if (err == ESP_OK) {
    wait_until(start + 159692);
    err = shift_bits(burst, 4, 3);
  }
  if (err == ESP_OK) {
    wait_until(start + 808830);
    err = latched_word(first);
  }
  if (err == ESP_OK) {
    esp_rom_delay_us(32);
    err = latched_word(second);
  }
  if (err == ESP_OK) {
    wait_until(start + 856650);
    err = shift_bits(initial_24, 4, 3);
  }
  for (size_t i = ATT; i <= LATCH; ++i) {
    esp_err_t cleanup = set_pin(&controls[i], 0);
    if (cleanup != ESP_OK) {
      ESP_LOGE(TAG, "Cannot set %s LOW: %s", controls[i].name,
               esp_err_to_name(cleanup));
      if (err == ESP_OK) err = cleanup;
    }
  }
  xSemaphoreGive(control_lock);
  return err;
}

esp_err_t sony_manual_controls_status(cJSON *root) {
  if (!control_lock) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(control_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
  cJSON *status = cJSON_AddObjectToObject(root, "sony_controls");
  if (!status) {
    xSemaphoreGive(control_lock);
    return ESP_ERR_NO_MEM;
  }
  for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
    cJSON *pin = cJSON_AddObjectToObject(status, controls[i].name);
    if (!pin || !cJSON_AddNumberToObject(pin, "gpio", controls[i].gpio) ||
        !(controls[i].level < 0 ? cJSON_AddNullToObject(pin, "level") :
          cJSON_AddNumberToObject(pin, "level", controls[i].level))) {
      xSemaphoreGive(control_lock);
      return ESP_ERR_NO_MEM;
    }
  }
  xSemaphoreGive(control_lock);
  return ESP_OK;
}
