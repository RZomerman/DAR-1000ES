#include "radio.h"
#include "sony_uart_link.h"
#include "sony_controller_menu.h"
#include "sony_controller_radio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static QueueHandle_t preset_requests;
static atomic_uint generation;

esp_err_t sony_controller_get_preset(unsigned number, sony_station_command_t *station) {
  if (!station || number < 1 || number > 16)
    return ESP_ERR_INVALID_ARG;
  nvs_handle_t nvs;
  esp_err_t err = nvs_open("presets", NVS_READONLY, &nvs);
  if (err != ESP_OK)
    return err;
  char key[8], data[SONY_STATION_NAME_MAX + SONY_STATION_URL_MAX + 3];
  snprintf(key, sizeof(key), "s%02u", number);
  memset(station, 0, sizeof(*station));
  size_t size = sizeof(*station);
  err = nvs_get_blob(nvs, key, station, &size);
  if (err == ESP_OK) {
    nvs_close(nvs);
    uint8_t bytes[SONY_STATION_WIRE_MAX];
    if (size == offsetof(sony_station_command_t, genre_labels))
      station->genre_labels = 0;
    return (size == sizeof(*station) ||
            size == offsetof(sony_station_command_t, genre_labels)) &&
               sony_station_encode(station, bytes, sizeof(bytes))
               ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
  }
  if (err != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(nvs);
    return err;
  }
  snprintf(key, sizeof(key), "p%02u", number);
  size = sizeof(data);
  err = nvs_get_blob(nvs, key, data, &size);
  nvs_close(nvs);
  if (err != ESP_OK)
    return err;
  char *end = memchr(data, 0, size);
  if (!end || end == data || (size_t)(end - data) > SONY_STATION_NAME_MAX ||
      size < (size_t)(end - data) + 2 || data[size - 1] ||
      strlen(end + 1) != size - (size_t)(end - data) - 2 ||
      strlen(end + 1) > SONY_STATION_URL_MAX)
    return ESP_ERR_INVALID_RESPONSE;
  memset(station, 0, sizeof(*station));
  strcpy(station->name, data);
  strcpy(station->url, end + 1);
  station->generation = 1;
  return ESP_OK;
}

esp_err_t sony_controller_save_preset(unsigned number, const sony_station_command_t *station) {
  if (!station || number < 1 || number > 16)
    return ESP_ERR_INVALID_ARG;
  uint8_t encoded[SONY_STATION_WIRE_MAX];
  if (!sony_station_encode(station, encoded, sizeof(encoded)))
    return ESP_ERR_INVALID_ARG;
  char key[8];
  snprintf(key, sizeof(key), "s%02u", number);
  nvs_handle_t nvs;
  esp_err_t err = nvs_open("presets", NVS_READWRITE, &nvs);
  if (err == ESP_OK) {
    err = nvs_set_blob(nvs, key, station, sizeof(*station));
    if (err == ESP_OK)
      err = nvs_commit(nvs);
    nvs_close(nvs);
  }
  return err;
}

esp_err_t sony_controller_clear_preset(unsigned number) {
  if (number < 1 || number > 16)
    return ESP_ERR_INVALID_ARG;
  nvs_handle_t nvs;
  esp_err_t err = nvs_open("presets", NVS_READWRITE, &nvs);
  if (err != ESP_OK)
    return err;
  char key[8];
  for (const char *prefix = "ps"; *prefix; ++prefix) {
    snprintf(key, sizeof(key), "%c%02u", *prefix, number);
    err = nvs_erase_key(nvs, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
      break;
    err = ESP_OK;
  }
  if (err == ESP_OK)
    err = nvs_commit(nvs);
  nvs_close(nvs);
  return err;
}

static esp_err_t select_station(const sony_station_command_t *station, unsigned preset) {
  if (!station)
    return ESP_ERR_INVALID_ARG;
  sony_station_command_t command = *station;
  command.generation = atomic_fetch_add(&generation, 1) + 1;
  if (!command.generation)
    command.generation = atomic_fetch_add(&generation, 1) + 1;
  return sony_uart_link_select_station(&command, preset);
}

esp_err_t sony_controller_select_station(const sony_station_command_t *station) {
  return select_station(station, 0);
}

static void preset_task(void *unused) {
  (void)unused;
  unsigned number;
  for (;;) {
    if (xQueueReceive(preset_requests, &number, portMAX_DELAY) != pdTRUE)
      continue;
    sony_station_command_t station;
    esp_err_t err = sony_controller_get_preset(number, &station);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      sony_controller_menu_preset_status(SONY_PRESET_UNASSIGNED);
      continue;
    }
    if (err == ESP_OK)
      err = select_station(&station, number);
    sony_controller_menu_preset_status(err == ESP_OK ? SONY_PRESET_QUEUED : SONY_PRESET_ERROR);
  }
}

esp_err_t radio_init(void) {
  atomic_store(&generation, esp_random() & 0x3fffffffU);
  preset_requests = xQueueCreate(1, sizeof(unsigned));
  if (!preset_requests)
    return ESP_ERR_NO_MEM;
  return xTaskCreate(preset_task, "preset_controller", 4096, NULL, 3, NULL) == pdPASS
             ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t radio_select_preset(unsigned number) {
  if (!preset_requests || number < 1 || number > 16)
    return ESP_ERR_INVALID_ARG;
  return xQueueOverwrite(preset_requests, &number) == pdPASS ? ESP_OK : ESP_FAIL;
}
