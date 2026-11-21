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
#ifdef CONFIG_SONY_CONTROLLER_NODE
#include "freertos/queue.h"
#endif
#if defined(CONFIG_SONY_UART_AUDIO_NODE) || defined(CONFIG_SONY_SPLIT_AUDIO_NODE)
#define SONY_UART_RECEIVER 1
#endif
#if defined(SONY_UART_RECEIVER) || defined(CONFIG_SONY_CONTROLLER_NODE)
#define SONY_UART_BUFFERED 1
#include "driver/uart.h"
#include "esp_log.h"
#endif
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
#include "radio.h"
#include "audio_output.h"
#include "sony_station_wire.h"
#include "sony_spectrum.h"
#include <string.h>
#endif

#ifndef SONY_UART_RECEIVER
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#endif

#ifndef SONY_UART_BUFFERED
static uart_dev_t *const link_hw = UART_LL_GET_HW(UART_NUM_1);
#endif
#if !defined(CONFIG_SONY_SPLIT_AUDIO_NODE) && !defined(CONFIG_SONY_CONTROLLER_NODE)
static const unsigned map_pins[] = {6, 4, 7, 15};

static bool valid_map_pin(unsigned pin) {
  for (unsigned i = 0; i < 4; ++i)
    if (pin == map_pins[i])
      return true;
  return false;
}
#endif

static bool send_message(sony_uart_message_t message) {
  uint8_t frame[SONY_UART_FRAME_SIZE];
  sony_uart_encode(&message, frame);
#ifdef SONY_UART_BUFFERED
  return uart_write_bytes(UART_NUM_1, frame, sizeof(frame)) == sizeof(frame);
#else
  if (uart_ll_get_txfifo_len(link_hw) < sizeof(frame))
    return false;
  uart_ll_write_txfifo(link_hw, frame, sizeof(frame));
  return true;
#endif
}

#ifdef CONFIG_SONY_STARTUP_ISOLATED_DISPLAY
void sony_uart_link_startup_sound(bool play) {
  send_message((sony_uart_message_t){.type = SONY_UART_STARTUP,
      .value = play ? SONY_STARTUP_SOUND_PLAY : SONY_STARTUP_SOUND_CANCEL});
}
#endif

#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
static uint32_t pack_pair(const uint32_t value[2]) {
  uint32_t left = value[0] > 32768 ? 32768 : value[0];
  uint32_t right = value[1] > 32768 ? 32768 : value[1];
  return left | (right << 16);
}

static void send_telemetry(uint32_t generation, int64_t now, int64_t *next_title,
                           unsigned *title_offset, char title[64]) {
  radio_link_status_t status = {0};
  sony_spectrum_status_t spectrum = {0};
  radio_link_status(&status);
  sony_spectrum_get_status(&spectrum);
  if (!status.playing || spectrum.generation != status.generation) {
    memset(spectrum.levels, 0, sizeof(spectrum.levels));
    memset(spectrum.rms, 0, sizeof(spectrum.rms));
    memset(spectrum.peak, 0, sizeof(spectrum.peak));
    memset(spectrum.held, 0, sizeof(spectrum.held));
  }
  unsigned rate = status.sample_rate == 32000 ? 1 :
                  status.sample_rate == 44100 ? 2 :
                  status.sample_rate == 48000 ? 3 : 0;
  sony_uart_message_t message = {
      .type = SONY_UART_AUDIO_STATE, .sequence = generation,
      .value = (status.playing ? 1U : 0U) | (status.failed ? 2U : 0U) |
               (status.airplay ? 4U : 0U) | (status.channels << 8) | (rate << 16)};
  send_message(message);
  for (unsigned group = 0; group < 3; ++group) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 8 && group * 8 + i < SONY_SPECTRUM_COLUMNS; ++i)
      value |= (uint32_t)(spectrum.levels[group * 8 + i] & 15) << (i * 4);
    message.type = SONY_UART_EQ0 + group;
    message.value = value;
    send_message(message);
  }
  message.type = SONY_UART_RMS; message.value = pack_pair(spectrum.rms);
  send_message(message);
  message.type = SONY_UART_PEAK; message.value = pack_pair(spectrum.peak);
  send_message(message);
  message.type = SONY_UART_HELD; message.value = pack_pair(spectrum.held);
  send_message(message);
  if (now < *next_title) return;
  *next_title = now + 200000;
  if (!*title_offset) {
    memset(title, 0, 64);
    strlcpy(title, status.title, 64);
  }
  for (unsigned part = 0; part < 4; ++part) {
    unsigned index = (*title_offset)++;
    message.type = SONY_UART_TITLE0 + index;
    message.value = 0;
    for (unsigned i = 0; i < 4; ++i)
      message.value |= (uint32_t)(uint8_t)title[index * 4 + i] << (8 * i);
    send_message(message);
  }
  if (*title_offset == 16)
    *title_offset = 0;
}

static void send_metadata(uint32_t generation, int64_t now, int64_t *next,
                          unsigned *offset, sony_uart_metadata_t *metadata) {
  if (!*offset) {
    if (now < *next) return;
    *next = now + 2000000;
    radio_link_metadata(metadata);
  }
  const unsigned char *data = (const unsigned char *)metadata;
  for (unsigned i = 0; i < 8 && *offset < SONY_UART_METADATA_PARTS; ++i) {
    unsigned part = (*offset)++;
    sony_uart_message_t message = {
        .type = SONY_UART_METADATA0 + part, .sequence = generation};
    for (unsigned byte = 0; byte < 4; ++byte)
      message.value |= (uint32_t)data[part * 4 + byte] << (8 * byte);
    if (!send_message(message)) {
      *offset = 0;
      return;
    }
  }
  if (*offset == SONY_UART_METADATA_PARTS) *offset = 0;
}
#endif

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
#elif !defined(CONFIG_SONY_SPLIT_AUDIO_NODE)
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t sent, replies, timeouts, bad_frames, last_map_pin;
static uint32_t requested_pin;
static bool mapping;
static int64_t last_reply_us;
#ifdef CONFIG_SONY_CONTROLLER_NODE
typedef struct {
  size_t size;
  uint32_t generation;
  uint8_t bytes[SONY_STATION_WIRE_MAX];
} station_packet_t;
static QueueHandle_t station_queue;
static uint32_t check_requested, check_inflight, check_completed;
static bool check_connected;
static uint32_t raw_received, rx_samples, rx_low_samples;
static sony_audio_link_status_t audio_status;
static int64_t last_pong_us;
static int64_t source_status_us;
static bool source_airplay;
static sony_station_command_t current_station;
static int64_t audio_status_us;
static int64_t meter_us[6];
static char title_staging[64];
static unsigned title_next;
static sony_uart_metadata_t metadata_staging;
static unsigned metadata_next;

bool sony_uart_link_current_station(sony_station_command_t *station) {
  portENTER_CRITICAL(&state_lock);
  bool available = current_station.generation && audio_status.acknowledged &&
      audio_status.playing && !audio_status.airplay && audio_status_us &&
      esp_timer_get_time() - audio_status_us < 2000000;
  if (available)
    *station = current_station;
  portEXIT_CRITICAL(&state_lock);
  return available;
}

void sony_uart_link_audio_status(sony_audio_link_status_t *status) {
  if (!status)
    return;
  int64_t now = esp_timer_get_time();
  int64_t meter_times[6];
  portENTER_CRITICAL(&state_lock);
  *status = audio_status;
  memcpy(meter_times, meter_us, sizeof(meter_times));
  status->fresh = audio_status_us && now - audio_status_us < 2000000;
  status->connected = last_pong_us && now - last_pong_us < 3000000;
  status->airplay_active = source_status_us && source_status_us >= audio_status_us &&
                          now - source_status_us < 3000000
      ? source_airplay : status->fresh && status->airplay;
  portEXIT_CRITICAL(&state_lock);
  if (!status->fresh || !status->playing) {
    status->playing = false;
    memset(status->levels, 0, sizeof(status->levels));
    memset(status->rms, 0, sizeof(status->rms));
    memset(status->peak, 0, sizeof(status->peak));
    memset(status->held, 0, sizeof(status->held));
  } else {
    for (unsigned group = 0; group < 3; ++group)
      if (!meter_times[group] || now - meter_times[group] >= 1000000)
        for (unsigned i = group * 8; i < group * 8 + 8 && i < 22; ++i)
          status->levels[i] = 0;
    uint32_t *values[] = {status->rms, status->peak, status->held};
    for (unsigned i = 0; i < 3; ++i)
      if (!meter_times[i + 3] || now - meter_times[i + 3] >= 1000000)
        memset(values[i], 0, sizeof(status->rms));
  }
  if (!status->fresh)
    memset(&status->metadata, 0, sizeof(status->metadata));
}

static void accept_audio_message(const sony_uart_message_t *message) {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&state_lock);
  if (message->type == SONY_UART_AUDIO_STATE && !message->sequence) {
    source_airplay = (message->value & 4) != 0;
    source_status_us = now;
    portEXIT_CRITICAL(&state_lock);
    return;
  }
  if (message->sequence != audio_status.generation || !audio_status.generation) {
    portEXIT_CRITICAL(&state_lock);
    return;
  }
  if (message->type == SONY_UART_ACK && message->value == 0x53544100) {
    audio_status.acknowledged = true;
  } else if (audio_status.acknowledged && message->type == SONY_UART_AUDIO_STATE) {
    audio_status.playing = (message->value & 1) != 0;
    audio_status.failed = (message->value & 2) != 0;
    bool airplay = (message->value & 4) != 0;
    if (audio_status.airplay != airplay) {
      audio_status.title[0] = 0;
      title_next = 0;
      memset(&audio_status.metadata, 0, sizeof(audio_status.metadata));
      metadata_next = SONY_UART_METADATA_PARTS;
    }
    audio_status.airplay = airplay;
    audio_status.channels = (message->value >> 8) & 3;
    unsigned rate = (message->value >> 16) & 3;
    audio_status.sample_rate = rate == 1 ? 32000 : rate == 2 ? 44100 :
                               rate == 3 ? 48000 : 0;
    audio_status_us = now;
  } else if (audio_status.acknowledged && message->type >= SONY_UART_EQ0 &&
             message->type <= SONY_UART_EQ2) {
    unsigned group = message->type - SONY_UART_EQ0;
    for (unsigned i = 0; i < 8 && group * 8 + i < 22; ++i)
      audio_status.levels[group * 8 + i] = (message->value >> (i * 4)) & 15;
    meter_us[group] = now;
  } else if (audio_status.acknowledged && message->type >= SONY_UART_RMS &&
             message->type <= SONY_UART_HELD) {
    uint32_t *target = message->type == SONY_UART_RMS ? audio_status.rms :
                       message->type == SONY_UART_PEAK ? audio_status.peak :
                                                        audio_status.held;
    target[0] = message->value & 0xffff;
    target[1] = message->value >> 16;
    meter_us[3 + message->type - SONY_UART_RMS] = now;
  } else if (audio_status.acknowledged && message->type >= SONY_UART_TITLE0 &&
             message->type <= SONY_UART_TITLE_LAST) {
    unsigned index = message->type - SONY_UART_TITLE0;
    if (!index)
      title_next = 0;
    if (index == title_next) {
      for (unsigned i = 0; i < 4; ++i)
        title_staging[index * 4 + i] = (char)(message->value >> (8 * i));
      if (++title_next == 16) {
        memcpy(audio_status.title, title_staging, sizeof(title_staging));
        audio_status.title[64] = 0;
      }
    }
  } else if (audio_status.acknowledged &&
             sony_uart_metadata_feed(&metadata_staging, &metadata_next, message)) {
    audio_status.metadata = metadata_staging;
  }
  portEXIT_CRITICAL(&state_lock);
}

uint32_t sony_uart_link_request_check(void) {
  portENTER_CRITICAL(&state_lock);
  if (++check_requested == 0) ++check_requested;
  uint32_t token = check_requested;
  portEXIT_CRITICAL(&state_lock);
  return token;
}

bool sony_uart_link_check_result(uint32_t token, bool *connected) {
  portENTER_CRITICAL(&state_lock);
  bool complete = token && token == check_completed;
  if (complete && connected)
    *connected = check_connected;
  portEXIT_CRITICAL(&state_lock);
  return complete;
}

esp_err_t sony_uart_link_select_station(const sony_station_command_t *station,
                                       unsigned preset) {
  if (!station_queue)
    return ESP_ERR_INVALID_STATE;
  station_packet_t packet;
  packet.size = sony_station_encode(station, packet.bytes, sizeof(packet.bytes));
  if (!packet.size)
    return ESP_ERR_INVALID_ARG;
  packet.generation = station->generation;
  portENTER_CRITICAL(&state_lock);
  memset(&audio_status, 0, sizeof(audio_status));
  audio_status.generation = station->generation;
  current_station = *station;
  audio_status.preset = preset;
  audio_status.genre_labels = station->genre_labels & 3;
  strlcpy(audio_status.station, station->name, sizeof(audio_status.station));
  audio_status_us = 0;
  memset(meter_us, 0, sizeof(meter_us));
  title_next = 0;
  metadata_next = SONY_UART_METADATA_PARTS;
  portEXIT_CRITICAL(&state_lock);
  return xQueueOverwrite(station_queue, &packet) == pdPASS ? ESP_OK : ESP_FAIL;
}
#endif
#endif

static void link_task(void *unused) {
  (void)unused;
  sony_uart_parser_t parser = {0};
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
  sony_station_parser_t station_parser = {0};
  sony_station_command_t station;
  int64_t last_station_byte = 0;
  bool logged_controller = false;
  uint32_t command_generation = 0;
  int64_t next_telemetry = 0, next_title = 0, next_metadata = 0;
  unsigned title_offset = 0, metadata_offset = 0;
  char title[64] = {0};
  sony_uart_metadata_t metadata;
#endif
#ifndef SONY_UART_RECEIVER
  uint32_t sequence = 0;
  sony_uart_message_t pending = {0};
  int64_t next_send = 0, deadline = 0;
#ifdef CONFIG_SONY_CONTROLLER_NODE
  station_packet_t station_packet = {0};
  int64_t next_station_send = 0;
#endif
#endif
  for (;;) {
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
    int64_t telemetry_now = esp_timer_get_time();
    if (command_generation && telemetry_now >= next_telemetry) {
      send_telemetry(command_generation, telemetry_now, &next_title, &title_offset, title);
      send_metadata(command_generation, telemetry_now, &next_metadata, &metadata_offset, &metadata);
      next_telemetry = telemetry_now + 50000;
    }
#endif
#ifndef SONY_UART_RECEIVER
    int64_t now = esp_timer_get_time();
    if (pending.type && now >= deadline) {
      portENTER_CRITICAL(&state_lock);
      ++timeouts;
      mapping = false;
#ifdef CONFIG_SONY_CONTROLLER_NODE
      if (last_reply_us && now - last_reply_us >= 3000000) {
        audio_status.acknowledged = false;
        audio_status_us = 0;
      }
      if (check_inflight) {
        check_completed = check_inflight;
        check_connected = false;
        check_inflight = 0;
      }
#endif
      portEXIT_CRITICAL(&state_lock);
      pending.type = 0;
    }
#ifdef CONFIG_SONY_CONTROLLER_NODE
    if (xQueueReceive(station_queue, &station_packet, 0) == pdPASS)
      next_station_send = 0;
    portENTER_CRITICAL(&state_lock);
    bool station_acknowledged =
        audio_status.generation == station_packet.generation &&
        audio_status.acknowledged;
    portEXIT_CRITICAL(&state_lock);
    if (!pending.type && station_packet.size && !station_acknowledged &&
        now >= next_station_send) {
      uart_write_bytes(UART_NUM_1, station_packet.bytes, station_packet.size);
      next_station_send = now + 1000000;
    }
#endif
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
#ifdef CONFIG_SONY_CONTROLLER_NODE
      if (!pin && check_requested != check_completed)
        check_inflight = check_requested;
#endif
      if (written)
        ++sent;
      portEXIT_CRITICAL(&state_lock);
      deadline = now + (pin ? 6000000 : 1000000);
      next_send = now + 1000000;
    }
#endif
#ifdef CONFIG_SONY_CONTROLLER_NODE
    uint8_t bytes[256];
#else
    uint8_t bytes[64];
#endif
#ifdef SONY_UART_BUFFERED
    int count = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes),
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
                                pdMS_TO_TICKS(10));
#elif defined(SONY_UART_RECEIVER)
                                pdMS_TO_TICKS(20));
#else
                                0);
#endif
#else
    unsigned count = uart_ll_get_rxfifo_len(link_hw);
    if (count > sizeof(bytes))
      count = sizeof(bytes);
    uart_ll_read_rxfifo(link_hw, bytes, count);
#endif
#ifdef CONFIG_SONY_CONTROLLER_NODE
    portENTER_CRITICAL(&state_lock);
    raw_received += count;
    ++rx_samples;
    if (!gpio_get_level(GPIO_NUM_44))
      ++rx_low_samples;
    portEXIT_CRITICAL(&state_lock);
#endif
    for (int i = 0; i < count; ++i) {
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
      int64_t byte_time = esp_timer_get_time();
      if (last_station_byte && byte_time - last_station_byte > 500000)
        station_parser.used = 0;
      last_station_byte = byte_time;
      if (sony_station_feed(&station_parser, bytes[i], &station)) {
        esp_err_t result = station.generation == command_generation ? ESP_OK :
                           radio_select_url(station.name, station.url);
        if (result == ESP_OK) {
          command_generation = station.generation;
          title_offset = 0;
          metadata_offset = 0;
          next_telemetry = next_title = next_metadata = 0;
        }
        ESP_LOGI("sony_uart", "Station generation %lu: %s (%s)",
                 (unsigned long)station.generation, station.name,
                 esp_err_to_name(result));
        sony_uart_message_t ack = {.type = SONY_UART_ACK,
            .sequence = station.generation,
            .value = result == ESP_OK ? 0x53544100 : 0x53544101};
        send_message(ack);
      }
#endif
      sony_uart_message_t message;
      if (!sony_uart_feed(&parser, bytes[i], &message))
        continue;
#ifdef SONY_UART_RECEIVER
#ifdef CONFIG_SONY_OPTICAL_ISOLATED_PLAYBACK
      if (message.type == SONY_UART_STARTUP && !message.sequence &&
          message.value <= SONY_STARTUP_SOUND_PLAY) {
        audio_output_startup_sound(message.value == SONY_STARTUP_SOUND_PLAY);
        continue;
      }
#endif
      if (message.type == SONY_UART_PING && message.value == 0x434f4e54) {
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
        if (!logged_controller) {
          ESP_LOGI("sony_uart", "Controller heartbeat received");
          logged_controller = true;
        }
#endif
        message.type = SONY_UART_PONG;
        message.value = 0x41554449;
        send_message(message);
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
        // Sequence zero reports the source even without a controller station.
        send_message((sony_uart_message_t){
            .type = SONY_UART_AUDIO_STATE, .value = radio_airplay_active() ? 4U : 0U});
#endif
      }
#ifdef CONFIG_SONY_UART_AUDIO_NODE
      else if (message.type == SONY_UART_MAP && valid_map_pin(message.value)) {
        map_pin(message.value);
        message.type = SONY_UART_ACK;
        send_message(message);
      }
#endif
#else
#ifdef CONFIG_SONY_CONTROLLER_NODE
      accept_audio_message(&message);
#endif
      if (pending.type && message.sequence == pending.sequence &&
          ((pending.type == SONY_UART_PING && message.type == SONY_UART_PONG &&
            message.value == 0x41554449) ||
           (pending.type == SONY_UART_MAP && message.type == SONY_UART_ACK &&
            message.value == pending.value))) {
        portENTER_CRITICAL(&state_lock);
        ++replies;
        last_reply_us = esp_timer_get_time();
#ifdef CONFIG_SONY_CONTROLLER_NODE
        if (pending.type == SONY_UART_PING)
          last_pong_us = last_reply_us;
        if (pending.type == SONY_UART_PING && check_inflight) {
          check_completed = check_inflight;
          check_connected = true;
          check_inflight = 0;
        }
#endif
        if (pending.type == SONY_UART_MAP)
          last_map_pin = pending.value;
        mapping = false;
        portEXIT_CRITICAL(&state_lock);
        pending.type = 0;
      }
#endif
    }
#ifndef SONY_UART_RECEIVER
    portENTER_CRITICAL(&state_lock);
    bad_frames = parser.bad_frames;
    portEXIT_CRITICAL(&state_lock);
#endif
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

esp_err_t sony_uart_link_init(void) {
#ifdef CONFIG_SONY_CONTROLLER_NODE
  station_queue = xQueueCreate(1, sizeof(station_packet_t));
  if (!station_queue)
    return ESP_ERR_NO_MEM;
#endif
#ifdef SONY_UART_BUFFERED
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
#else
  esp_err_t err;
#endif
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
#ifdef CONFIG_SONY_SPLIT_AUDIO_NODE
  const unsigned stack_size = 5120;
#else
  const unsigned stack_size = 4096;
#endif
  return xTaskCreatePinnedToCore(link_task, "sony_uart", stack_size, NULL, 2, NULL, 0)
      == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

#ifndef SONY_UART_RECEIVER
static esp_err_t status_handler(httpd_req_t *req) {
  portENTER_CRITICAL(&state_lock);
  uint32_t tx = sent, rx = replies, lost = timeouts, bad = bad_frames;
  uint32_t pin = last_map_pin;
  bool busy = mapping || requested_pin;
  int64_t age = last_reply_us ? esp_timer_get_time() - last_reply_us : -1;
#ifdef CONFIG_SONY_CONTROLLER_NODE
  uint32_t raw_rx = raw_received, samples = rx_samples, low = rx_low_samples;
#endif
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
#ifdef CONFIG_SONY_CONTROLLER_NODE
  cJSON_AddNumberToObject(root, "rx_bytes", raw_rx);
  cJSON_AddNumberToObject(root, "rx_samples", samples);
  cJSON_AddNumberToObject(root, "rx_low_samples", low);
  sony_audio_link_status_t audio;
  sony_uart_link_audio_status(&audio);
  cJSON_AddNumberToObject(root, "generation", audio.generation);
  cJSON_AddBoolToObject(root, "acknowledged", audio.acknowledged);
  cJSON_AddBoolToObject(root, "telemetry_fresh", audio.fresh);
  cJSON_AddBoolToObject(root, "playing", audio.playing);
  cJSON_AddBoolToObject(root, "failed", audio.failed);
  cJSON_AddBoolToObject(root, "airplay", audio.airplay);
  cJSON_AddNumberToObject(root, "sample_rate", audio.sample_rate);
  cJSON_AddNumberToObject(root, "channels", audio.channels);
  cJSON_AddStringToObject(root, "station", audio.station);
  cJSON_AddStringToObject(root, "title", audio.fresh ? audio.title : "");
  cJSON_AddStringToObject(root, "codec", audio.metadata.codec);
  cJSON_AddStringToObject(root, "bitrate_kbps", audio.metadata.bitrate);
  cJSON_AddStringToObject(root, "genre", audio.metadata.genre);
  cJSON_AddStringToObject(root, "url", audio.metadata.url);
  cJSON_AddStringToObject(root, "audio_ip", audio.metadata.ip);
  cJSON *levels = cJSON_AddArrayToObject(root, "spectrum");
  cJSON *rms = cJSON_AddArrayToObject(root, "rms");
  cJSON *peak = cJSON_AddArrayToObject(root, "peak");
  cJSON *held = cJSON_AddArrayToObject(root, "held");
  for (unsigned i = 0; i < 22; ++i)
    cJSON_AddItemToArray(levels, cJSON_CreateNumber(audio.levels[i]));
  for (unsigned i = 0; i < 2; ++i) {
    cJSON_AddItemToArray(rms, cJSON_CreateNumber(audio.rms[i]));
    cJSON_AddItemToArray(peak, cJSON_CreateNumber(audio.peak[i]));
    cJSON_AddItemToArray(held, cJSON_CreateNumber(audio.held[i]));
  }
#endif
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

#ifndef CONFIG_SONY_CONTROLLER_NODE
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
#endif

esp_err_t sony_uart_link_register_http(httpd_handle_t server) {
  httpd_uri_t status = {.uri = "/api/audio-link", .method = HTTP_GET,
                        .handler = status_handler};
#ifdef CONFIG_SONY_CONTROLLER_NODE
  return httpd_register_uri_handler(server, &status);
#else
  httpd_uri_t mapping_uri = {.uri = "/api/audio-link", .method = HTTP_POST,
                             .handler = mapping_handler};
  esp_err_t err = httpd_register_uri_handler(server, &status);
  return err == ESP_OK ? httpd_register_uri_handler(server, &mapping_uri) : err;
#endif
}
#endif
