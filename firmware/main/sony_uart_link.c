#include "sony_uart_link.h"
#include "sony_uart_protocol.h"
#include "driver/gpio.h"
#include "hal/uart_ll.h"
#include "esp_private/esp_clk.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef CONFIG_SONY_UART_AUDIO_NODE
#include "driver/uart.h"
#include "esp_log.h"
#endif

#ifndef CONFIG_SONY_UART_AUDIO_NODE
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#endif

#ifndef CONFIG_SONY_UART_AUDIO_NODE
static uart_dev_t *const link_hw = UART_LL_GET_HW(UART_NUM_1);
#endif
static const unsigned map_pins[] = {6, 4, 7, 15};

static bool valid_map_pin(unsigned pin) {
  for (unsigned i = 0; i < 4; ++i)
    if (pin == map_pins[i])
      return true;
  return false;
}

static bool send_message(sony_uart_message_t message) {
  uint8_t frame[SONY_UART_FRAME_SIZE];
  sony_uart_encode(&message, frame);
#ifdef CONFIG_SONY_UART_AUDIO_NODE
  return uart_write_bytes(UART_NUM_1, frame, sizeof(frame)) == sizeof(frame);
#else
  if (uart_ll_get_txfifo_len(link_hw) < sizeof(frame))
    return false;
  uart_ll_write_txfifo(link_hw, frame, sizeof(frame));
  return true;
#endif
}

#ifdef CONFIG_SONY_UART_AUDIO_NODE
static void map_pin(unsigned pin) {
  for (unsigned i = 0; i < 4; ++i)
    gpio_set_level(map_pins[i], 0);
  gpio_set_level(GPIO_NUM_5, 0);
  for (unsigned i = 0; i < 6; ++i) {
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  gpio_set_level(GPIO_NUM_5, 1);
}
#else
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t sent, replies, timeouts, bad_frames, last_map_pin;
static uint32_t requested_pin;
static bool mapping;
static int64_t last_reply_us;
#endif

static void link_task(void *unused) {
  (void)unused;
  sony_uart_parser_t parser = {0};
#ifndef CONFIG_SONY_UART_AUDIO_NODE
  uint32_t sequence = 0;
  sony_uart_message_t pending = {0};
  int64_t next_send = 0, deadline = 0;
#endif
  for (;;) {
#ifndef CONFIG_SONY_UART_AUDIO_NODE
    int64_t now = esp_timer_get_time();
    if (pending.type && now >= deadline) {
      portENTER_CRITICAL(&state_lock);
      ++timeouts;
      mapping = false;
      portEXIT_CRITICAL(&state_lock);
      pending.type = 0;
    }
    if (!pending.type && now >= next_send) {
      portENTER_CRITICAL(&state_lock);
      unsigned pin = requested_pin;
      requested_pin = 0;
      mapping = pin != 0;
      portEXIT_CRITICAL(&state_lock);
      pending = (sony_uart_message_t){
          .type = pin ? SONY_UART_MAP : SONY_UART_PING,
          .sequence = ++sequence, .value = pin ? pin : 0x434f4e54};
      bool written = send_message(pending);
      portENTER_CRITICAL(&state_lock);
      if (written)
        ++sent;
      portEXIT_CRITICAL(&state_lock);
      deadline = now + (pin ? 6000000 : 1000000);
      next_send = now + 1000000;
    }
#endif
    uint8_t bytes[64];
#ifdef CONFIG_SONY_UART_AUDIO_NODE
    int count = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), pdMS_TO_TICKS(20));
#else
    unsigned count = uart_ll_get_rxfifo_len(link_hw);
    if (count > sizeof(bytes))
      count = sizeof(bytes);
    uart_ll_read_rxfifo(link_hw, bytes, count);
#endif
    for (int i = 0; i < count; ++i) {
      sony_uart_message_t message;
      if (!sony_uart_feed(&parser, bytes[i], &message))
        continue;
#ifdef CONFIG_SONY_UART_AUDIO_NODE
      if (message.type == SONY_UART_PING && message.value == 0x434f4e54) {
        message.type = SONY_UART_PONG;
        message.value = 0x41554449;
        send_message(message);
      } else if (message.type == SONY_UART_MAP && valid_map_pin(message.value)) {
        map_pin(message.value);
        message.type = SONY_UART_ACK;
        send_message(message);
      }
#else
      if (pending.type && message.sequence == pending.sequence &&
          ((pending.type == SONY_UART_PING && message.type == SONY_UART_PONG &&
            message.value == 0x41554449) ||
           (pending.type == SONY_UART_MAP && message.type == SONY_UART_ACK &&
            message.value == pending.value))) {
        portENTER_CRITICAL(&state_lock);
        ++replies;
        last_reply_us = esp_timer_get_time();
        if (pending.type == SONY_UART_MAP)
          last_map_pin = pending.value;
        mapping = false;
        portEXIT_CRITICAL(&state_lock);
        pending.type = 0;
      }
#endif
    }
#ifndef CONFIG_SONY_UART_AUDIO_NODE
    portENTER_CRITICAL(&state_lock);
    bad_frames = parser.bad_frames;
    portEXIT_CRITICAL(&state_lock);
#endif
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

esp_err_t sony_uart_link_init(void) {
#ifdef CONFIG_SONY_UART_AUDIO_NODE
  ESP_LOGI("sony_audio_bench", "Disabling chip4 outputs");
  gpio_set_level(GPIO_NUM_5, 1);
  gpio_config_t oe = {.pin_bit_mask = 1ULL << 5, .mode = GPIO_MODE_OUTPUT};
  esp_err_t err = gpio_config(&oe);
  if (err != ESP_OK)
    return err;
  gpio_set_level(GPIO_NUM_5, 1);
  gpio_config_t signals = {.mode = GPIO_MODE_OUTPUT};
  for (unsigned i = 0; i < 4; ++i) {
    signals.pin_bit_mask |= 1ULL << map_pins[i];
    gpio_set_level(map_pins[i], 0);
  }
  err = gpio_config(&signals);
  if (err != ESP_OK)
    return err;
  ESP_LOGI("sony_audio_bench", "Chip4 initialized; configuring UART1");
  uart_config_t config = {
      .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_XTAL};
  err = uart_param_config(UART_NUM_1, &config);
  if (err == ESP_OK)
    err = uart_set_pin(UART_NUM_1, 43, 44, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err == ESP_OK)
    err = uart_driver_install(UART_NUM_1, 2048, 0, 0, NULL, 0);
  if (err != ESP_OK)
    return err;
#else
  esp_err_t err;
  // This finite bench protocol sends 14-byte frames at most once per second.
  // Poll UART1's FIFO instead of pulling in the full buffered UART driver.
  PERIPH_RCC_ATOMIC() {
    uart_ll_enable_bus_clock(UART_NUM_1, true);
    uart_ll_reset_register(UART_NUM_1);
  }
  uart_ll_disable_intr_mask(link_hw, UINT32_MAX);
  uart_ll_set_sclk(link_hw, UART_SCLK_XTAL);
  if (!uart_ll_set_baudrate(link_hw, 115200, esp_clk_xtal_freq()))
    return ESP_ERR_INVALID_ARG;
  uart_ll_set_data_bit_num(link_hw, UART_DATA_8_BITS);
  uart_ll_set_mode(link_hw, UART_MODE_UART);
  uart_ll_set_tx_idle_num(link_hw, 0);
  uart_ll_set_parity(link_hw, UART_PARITY_DISABLE);
  uart_ll_set_stop_bits(link_hw, UART_STOP_BITS_1);
  uart_ll_set_hw_flow_ctrl(link_hw, UART_HW_FLOWCTRL_DISABLE, 100);
  uart_ll_rxfifo_rst(link_hw);
  uart_ll_txfifo_rst(link_hw);
  gpio_set_level(GPIO_NUM_43, 1);
  gpio_config_t tx = {.pin_bit_mask = 1ULL << 43, .mode = GPIO_MODE_OUTPUT};
  err = gpio_config(&tx);
  gpio_config_t rx = {.pin_bit_mask = 1ULL << 44, .mode = GPIO_MODE_INPUT,
                      .pull_up_en = GPIO_PULLUP_ENABLE};
  if (err == ESP_OK)
    err = gpio_config(&rx);
  if (err != ESP_OK)
    return err;
  esp_rom_gpio_connect_out_signal(43, U1TXD_OUT_IDX, false, false);
  esp_rom_gpio_connect_in_signal(44, U1RXD_IN_IDX, false);
#endif
  return xTaskCreatePinnedToCore(link_task, "sony_uart", 4096, NULL, 2, NULL, 0)
      == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

#ifndef CONFIG_SONY_UART_AUDIO_NODE
static esp_err_t status_handler(httpd_req_t *req) {
  portENTER_CRITICAL(&state_lock);
  uint32_t tx = sent, rx = replies, lost = timeouts, bad = bad_frames;
  uint32_t pin = last_map_pin;
  bool busy = mapping || requested_pin;
  int64_t age = last_reply_us ? esp_timer_get_time() - last_reply_us : -1;
  portEXIT_CRITICAL(&state_lock);
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
  cJSON_AddBoolToObject(root, "connected", age >= 0 && age < 3000000);
  cJSON_AddNumberToObject(root, "sent", tx);
  cJSON_AddNumberToObject(root, "replies", rx);
  cJSON_AddNumberToObject(root, "timeouts", lost);
  cJSON_AddNumberToObject(root, "bad_frames", bad);
  cJSON_AddNumberToObject(root, "last_reply_age_ms", age < 0 ? -1 : age / 1000);
  cJSON_AddBoolToObject(root, "mapping", busy);
  cJSON_AddNumberToObject(root, "last_map_pin", pin);
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t err = httpd_resp_sendstr(req, json);
  free(json);
  return err;
}

static esp_err_t mapping_handler(httpd_req_t *req) {
  char body[128];
  if (!req->content_len || req->content_len >= sizeof(body))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid size");
  size_t used = 0;
  while (used < req->content_len) {
    int count = httpd_req_recv(req, body + used, req->content_len - used);
    if (count <= 0)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
    used += count;
  }
  body[used] = 0;
  cJSON *root = cJSON_Parse(body);
  cJSON *pin = cJSON_GetObjectItemCaseSensitive(root, "map_pin");
  cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(root, "bench_confirmed");
  bool valid = cJSON_IsTrue(confirmed) && cJSON_IsNumber(pin) &&
      pin->valuedouble == pin->valueint && pin->valueint >= 0 &&
      valid_map_pin((unsigned)pin->valueint);
  unsigned value = valid ? (unsigned)pin->valueint : 0;
  cJSON_Delete(root);
  if (!valid)
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
        "Confirm Sony audio wires disconnected and choose GPIO4/6/7/15");
  portENTER_CRITICAL(&state_lock);
  bool busy = mapping || requested_pin;
  if (!busy)
    requested_pin = value;
  portEXIT_CRITICAL(&state_lock);
  if (busy) {
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_sendstr(req, "Mapping busy");
  }
  return status_handler(req);
}

esp_err_t sony_uart_link_register_http(httpd_handle_t server) {
  httpd_uri_t status = {.uri = "/api/audio-link", .method = HTTP_GET,
                        .handler = status_handler};
  httpd_uri_t mapping_uri = {.uri = "/api/audio-link", .method = HTTP_POST,
                             .handler = mapping_handler};
  esp_err_t err = httpd_register_uri_handler(server, &status);
  return err == ESP_OK ? httpd_register_uri_handler(server, &mapping_uri) : err;
}
#endif
