#include "sony_keypad.h"
#include "sony_screen.h"
#include "radio.h"
#ifdef CONFIG_SONY_CONTROLLER_NODE
#include "sony_controller_menu.h"
#endif

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  KEYPAD_ADDR = 0x34, KEYPAD_SCL = 9, KEYPAD_SDA = 10,
  REG_CFG = 0x01, REG_INT = 0x02, REG_COUNT = 0x03, REG_EVENT = 0x04,
  REG_GPIO_DATA_1 = 0x14, REG_GPIO_DATA_2 = 0x15, REG_GPIO_DATA_3 = 0x16,
  REG_KP_ROW = 0x1d, REG_KP_COL = 0x1e, REG_KP_COL_HI = 0x1f,
  EVENT_CAPACITY = 32
};

typedef struct {
  uint32_t sequence;
  uint8_t raw;
  int64_t time_ms;
  bool virtual_input;
} key_event_t;

static SemaphoreHandle_t lock;
static bool present, scanning, restore_on_boot;
static unsigned rows, columns, errors, overflows;
static uint8_t hardware_rows, hardware_columns, hardware_columns_hi;
static uint8_t chip_cfg, chip_status, chip_count, chip_gpio[3];
static esp_err_t last_error;
static uint32_t sequence;
static key_event_t events[EVENT_CAPACITY];
static bool virtual_enabled;
static uint32_t virtual_session;
static int64_t virtual_seen_ms;

enum { DISPLAY_KEY_CODE = 58, CHECK_KEY_CODE = 25, MONO_KEY_CODE = 29,
       BUTTON_CHECK_ACTION = 18, BUTTON_MONO_ACTION = 19,
#ifdef CONFIG_SONY_CONTROLLER_NODE
       BUTTON_DEBOUNCE_MS = 20, KEYPAD_POLL_MS = 10,
#else
       BUTTON_DEBOUNCE_MS = 60,
       KEYPAD_POLL_MS = 40,
#endif
       BUTTON_ARM_MS = 200, BUTTON_LONG_MS = 1200 };

typedef struct {
  uint64_t down[2];
  int64_t change_ms, pressed_ms;
  uint8_t key_code;
  bool armed, candidate, stable, contaminated, long_sent;
} display_button_t;

static display_button_t display_button;

static const struct {
  const char *name;
  uint8_t code;
} key_bindings[] = {
    {"1",5}, {"2",6}, {"3",7}, {"4",8}, {"5",1}, {"6",2}, {"7",3}, {"8",9},
    {"9",15}, {"10",16}, {"11",17}, {"12",18}, {"13",11}, {"14",12},
    {"15",13}, {"16",19}, {"CHECK",25}, {"DISPLAY MODE",58},
    {"NEWS",35}, {"AFFAIRS",36}, {"INFO",37}, {"SPORT",38}, {"EDUCATE",31},
    {"DRAMA",32}, {"CULTURE",33}, {"SCIENCE",39}, {"VARIED",45},
    {"POP M",46}, {"ROCK M",47}, {"M.O.R. M",48}, {"MUSIC",28}, {"SPEECH",21},
    {"LIGHT M",41}, {"CLASSICS",42}, {"OTHER M",43}, {"UNDEFINED",49},
    {"MEMORY",51}, {"MHz/CLEAR",52}, {"MONO MODE",MONO_KEY_CODE}, {"BLOCK -",59}, {"BLOCK +",53},
    {"FINE -",23}, {"FINE +",22}, {"DIRECT",26}, {"LINK",27},
};

static int preset_number(unsigned code) {
  for (unsigned i = 0; i < 16; ++i)
    if (key_bindings[i].code == code) return (int)i + 1;
  return 0;
}

#ifdef CONFIG_SONY_CONTROLLER_NODE
static bool menu_key(unsigned code) {
  for (unsigned i = 16; i < sizeof(key_bindings) / sizeof(key_bindings[0]); ++i)
    if (key_bindings[i].code == code)
      return true;
  return false;
}
#endif

static void display_button_reset(display_button_t *button, int64_t now) {
  memset(button, 0, sizeof(*button));
  button->change_ms = now;
}

static void display_button_event(display_button_t *button, uint8_t raw,
                                 int64_t now) {
  unsigned code = raw & 0x7f;
  if (code < 1 || code > 80)
    return;
  unsigned index = code - 1;
  uint64_t bit = UINT64_C(1) << (index % 64);
  bool pressed = (raw & 0x80) != 0;
  if (pressed)
    button->down[index / 64] |= bit;
  else
    button->down[index / 64] &= ~bit;
  if (!button->armed)
    button->change_ms = now;
  if (!button->key_code && pressed && button->armed &&
      (code == DISPLAY_KEY_CODE || code == CHECK_KEY_CODE || code == MONO_KEY_CODE ||
       preset_number(code)
#ifdef CONFIG_SONY_CONTROLLER_NODE
       || menu_key(code)
#endif
       ))
    button->key_code = code;
  if (code != button->key_code) {
    if (pressed && (button->candidate || button->stable))
      button->contaminated = true;
    return;
  }
  if (pressed == button->candidate)
    return;
  if (!pressed && button->candidate && !button->stable &&
      now - button->change_ms >= BUTTON_DEBOUNCE_MS) {
    button->stable = true;
    button->pressed_ms = button->change_ms;
    button->long_sent = false;
  }
  button->candidate = pressed;
  button->change_ms = now;
  if (pressed) {
    uint64_t other_low = button->down[0] & ~bit;
    button->contaminated = (button->stable && button->contaminated) ||
                           other_low || button->down[1];
  }
}

/* Returns -1 for no action, 0/1 for display, 2-17 for presets, 18/19 for CHECK/MONO. */
static int display_button_poll(display_button_t *button, int64_t now) {
  if (!button->armed) {
    if (!button->down[0] && !button->down[1] &&
        now - button->change_ms >= BUTTON_ARM_MS)
      button->armed = true;
    return -1;
  }
  if (now - button->change_ms < BUTTON_DEBOUNCE_MS)
    return -1;
  if (button->candidate == button->stable) {
    if (!button->stable)
      button->key_code = 0;
    else if ((button->key_code == DISPLAY_KEY_CODE
#ifdef CONFIG_SONY_CONTROLLER_NODE
              || button->key_code == 51 || button->key_code == 26 ||
                 button->key_code == CHECK_KEY_CODE
#endif
              ) && !button->contaminated &&
             !button->long_sent && now - button->pressed_ms >=
#ifdef CONFIG_SONY_CONTROLLER_NODE
                 (button->key_code == CHECK_KEY_CODE ? 2000 : BUTTON_LONG_MS)
#else
                 BUTTON_LONG_MS
#endif
             ) {
      button->long_sent = true;
#ifdef CONFIG_SONY_CONTROLLER_NODE
      if (button->key_code == 51 || button->key_code == 26 ||
          button->key_code == CHECK_KEY_CODE)
        return 0x200 | button->key_code;
#endif
      return 1;
    }
    return -1;
  }
  button->stable = button->candidate;
  if (button->stable) {
    button->pressed_ms = button->change_ms;
    button->long_sent = false;
#ifdef CONFIG_SONY_CONTROLLER_NODE
    unsigned code = button->key_code;
    if (!button->contaminated && code != DISPLAY_KEY_CODE &&
        code != CHECK_KEY_CODE && code != 51 && code != 26) {
      button->long_sent = true;
      if (code == MONO_KEY_CODE)
        return BUTTON_MONO_ACTION;
      if (menu_key(code))
        return 0x100 | code;
      return preset_number(code) + 1;
    }
#endif
    return -1;
  }
  unsigned code = button->key_code;
  button->key_code = 0;
  if (button->contaminated || button->long_sent)
    return -1;
  if (code == CHECK_KEY_CODE)
#ifdef CONFIG_SONY_CONTROLLER_NODE
    return button->change_ms - button->pressed_ms >= 2000 ? (0x200 | code) : (0x100 | code);
#else
    return BUTTON_CHECK_ACTION;
#endif
  if (code == MONO_KEY_CODE)
    return BUTTON_MONO_ACTION;
#ifdef CONFIG_SONY_CONTROLLER_NODE
  if (code != DISPLAY_KEY_CODE && menu_key(code))
    return ((code == 51 || code == 26) &&
            button->change_ms - button->pressed_ms >= BUTTON_LONG_MS)
               ? (0x200 | code) : (0x100 | code);
#endif
  if (code != DISPLAY_KEY_CODE)
    return preset_number(code) + 1;
  return button->change_ms - button->pressed_ms >= BUTTON_LONG_MS ? 1 : 0;
}

static void bus_delay(void) { esp_rom_delay_us(5); }

static esp_err_t clock_high(void) {
  gpio_set_level(KEYPAD_SCL, 1);
  for (unsigned i = 0; i < 200; ++i) {
    if (gpio_get_level(KEYPAD_SCL)) {
      bus_delay();
      return ESP_OK;
    }
    esp_rom_delay_us(5);
  }
  return ESP_ERR_TIMEOUT;
}

static esp_err_t bus_start(void) {
  gpio_set_level(KEYPAD_SDA, 1);
  bus_delay();
  esp_err_t err = clock_high();
  if (err != ESP_OK)
    return err;
  if (!gpio_get_level(KEYPAD_SDA))
    return ESP_ERR_INVALID_STATE;
  gpio_set_level(KEYPAD_SDA, 0);
  bus_delay();
  gpio_set_level(KEYPAD_SCL, 0);
  return ESP_OK;
}

static void bus_stop(void) {
  gpio_set_level(KEYPAD_SDA, 0);
  bus_delay();
  clock_high();
  gpio_set_level(KEYPAD_SDA, 1);
  bus_delay();
}

static void bus_recover(void) {
  gpio_set_level(KEYPAD_SDA, 1);
  if (gpio_get_level(KEYPAD_SDA))
    return;
  for (unsigned i = 0; i < 9 && !gpio_get_level(KEYPAD_SDA); ++i) {
    gpio_set_level(KEYPAD_SCL, 0);
    bus_delay();
    if (clock_high() != ESP_OK)
      return;
  }
  bus_stop();
}

static esp_err_t bus_send(uint8_t byte) {
  for (unsigned bit = 0; bit < 8; ++bit) {
    gpio_set_level(KEYPAD_SDA, (byte & 0x80) != 0);
    byte <<= 1;
    bus_delay();
    esp_err_t err = clock_high();
    gpio_set_level(KEYPAD_SCL, 0);
    if (err != ESP_OK)
      return err;
  }
  gpio_set_level(KEYPAD_SDA, 1);
  bus_delay();
  esp_err_t err = clock_high();
  bool nack = gpio_get_level(KEYPAD_SDA);
  gpio_set_level(KEYPAD_SCL, 0);
  if (err != ESP_OK)
    return err;
  return nack ? ESP_ERR_NOT_FOUND : ESP_OK;
}

static esp_err_t bus_receive(uint8_t *value) {
  uint8_t result = 0;
  gpio_set_level(KEYPAD_SDA, 1);
  for (unsigned bit = 0; bit < 8; ++bit) {
    bus_delay();
    esp_err_t err = clock_high();
    result = (uint8_t)((result << 1) | (gpio_get_level(KEYPAD_SDA) != 0));
    gpio_set_level(KEYPAD_SCL, 0);
    if (err != ESP_OK)
      return err;
  }
  bus_delay();
  esp_err_t err = clock_high();
  gpio_set_level(KEYPAD_SCL, 0);
  if (err == ESP_OK)
    *value = result;
  return err;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *value) {
  esp_err_t err = bus_start();
  if (err == ESP_OK) err = bus_send(KEYPAD_ADDR << 1);
  if (err == ESP_OK) err = bus_send(reg);
  if (err == ESP_OK) err = bus_start();
  if (err == ESP_OK) err = bus_send((KEYPAD_ADDR << 1) | 1);
  if (err == ESP_OK) err = bus_receive(value);
  bus_stop();
  return err;
}

static esp_err_t write_reg(uint8_t reg, uint8_t value) {
  esp_err_t err = bus_start();
  if (err == ESP_OK) err = bus_send(KEYPAD_ADDR << 1);
  if (err == ESP_OK) err = bus_send(reg);
  if (err == ESP_OK) err = bus_send(value);
  bus_stop();
  return err;
}

static void record_key(uint8_t raw, int64_t now, bool virtual_input, bool feed) {
  ++sequence;
  events[(sequence - 1) % EVENT_CAPACITY] =
      (key_event_t){sequence, raw, now, virtual_input};
  if (feed)
    display_button_event(&display_button, raw, now);
}

static void dispatch_button(int action) {
#ifdef CONFIG_SONY_CONTROLLER_NODE
  if (action >= 0x100)
    sony_controller_menu_key((unsigned)action & 0xff, (action & 0x200) != 0);
  else if (action >= 2 && action <= 17)
    sony_controller_menu_key(key_bindings[action - 2].code, false);
  else
#endif
  if (action == BUTTON_CHECK_ACTION)
    (void)sony_screen_start_from_check();
  else if (action == BUTTON_MONO_ACTION)
    (void)sony_screen_startup_press();
  else if (action >= 2)
    (void)radio_select_preset((unsigned)action - 1);
  else if (action >= 0)
    (void)sony_screen_display_button_press(action == 1);
}

static void poll_virtual(int64_t now) {
  if (now - virtual_seen_ms > 6000) {
    virtual_enabled = false;
    display_button_reset(&display_button, now);
    sony_screen_stop();
  } else {
    dispatch_button(display_button_poll(&display_button, now));
  }
}

static esp_err_t disable_scan(void) {
  esp_err_t err = write_reg(REG_KP_ROW, 0);
  esp_err_t next = write_reg(REG_KP_COL, 0);
  if (err == ESP_OK)
    err = next;
  next = write_reg(REG_KP_COL_HI, 0);
  if (err == ESP_OK)
    err = next;
  scanning = false;
  rows = columns = 0;
  display_button_reset(&display_button, esp_timer_get_time() / 1000);
  return err;
}

static esp_err_t configure_scan(unsigned r, unsigned c) {
  uint8_t cfg;
  esp_err_t err = read_reg(REG_CFG, &cfg);
  if (err == ESP_OK)
    err = write_reg(REG_KP_ROW, 0);
  if (err == ESP_OK)
    err = write_reg(REG_KP_COL, (uint8_t)((1U << (c > 8 ? 8 : c)) - 1));
  if (err == ESP_OK)
    err = write_reg(REG_KP_COL_HI, c > 8 ? (uint8_t)((1U << (c - 8)) - 1) : 0);
  if (err == ESP_OK)
    err = write_reg(REG_KP_ROW, (uint8_t)((1U << r) - 1));
  if (err == ESP_OK)
    err = write_reg(REG_CFG, cfg | 0x09);
  if (err != ESP_OK) {
    disable_scan();
    return err;
  }
  rows = r;
  columns = c;
  scanning = true;
  display_button_reset(&display_button, esp_timer_get_time() / 1000);
  return ESP_OK;
}

static void keypad_task(void *unused) {
  (void)unused;
  for (;;) {
    int button_action = -1;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (virtual_enabled) {
      poll_virtual(esp_timer_get_time() / 1000);
      xSemaphoreGive(lock);
      vTaskDelay(pdMS_TO_TICKS(KEYPAD_POLL_MS));
      continue;
    }
    uint8_t cfg;
    esp_err_t err = read_reg(REG_CFG, &cfg);
    if (err == ESP_OK)
      err = read_reg(REG_KP_ROW, &hardware_rows);
    if (err == ESP_OK)
      err = read_reg(REG_KP_COL, &hardware_columns);
    if (err == ESP_OK)
      err = read_reg(REG_KP_COL_HI, &hardware_columns_hi);
    if (err == ESP_OK)
      chip_cfg = cfg;
    if (err == ESP_OK && scanning) {
      uint8_t status, count;
      err = read_reg(REG_INT, &status);
      if (err == ESP_OK)
        err = read_reg(REG_COUNT, &count);
      if (err == ESP_OK) {
        chip_status = status;
        chip_count = count;
        count &= 0x0f;
        if (count > 10)
          err = ESP_ERR_INVALID_RESPONSE;
      }
      if (err == ESP_OK && (status & 0x08))
        display_button_reset(&display_button, esp_timer_get_time() / 1000);
      for (unsigned i = 0; err == ESP_OK && i < count; ++i) {
        uint8_t raw;
        err = read_reg(REG_EVENT, &raw);
        if (err == ESP_OK && raw) {
          record_key(raw, esp_timer_get_time() / 1000, false, !(status & 0x08));
        }
      }
      if (err == ESP_OK && (status & 0x08))
        ++overflows;
      if (err == ESP_OK && (status & 0x09))
        err = write_reg(REG_INT, status & 0x09);
    }
    if (err == ESP_OK)
      err = read_reg(REG_GPIO_DATA_1, &chip_gpio[0]);
    if (err == ESP_OK)
      err = read_reg(REG_GPIO_DATA_2, &chip_gpio[1]);
    if (err == ESP_OK)
      err = read_reg(REG_GPIO_DATA_3, &chip_gpio[2]);
    if (err != ESP_OK) {
      if (present)
        ++errors;
      present = false;
      if (scanning)
        disable_scan();
    } else {
      present = true;
      if (scanning)
        button_action = display_button_poll(&display_button, esp_timer_get_time() / 1000);
    }
    last_error = err;
    dispatch_button(button_action);
    xSemaphoreGive(lock);
    vTaskDelay(pdMS_TO_TICKS(KEYPAD_POLL_MS));
  }
}

esp_err_t sony_keypad_init(void) {
  lock = xSemaphoreCreateMutex();
  if (!lock)
    return ESP_ERR_NO_MEM;
  gpio_set_level(KEYPAD_SDA, 1);
  gpio_set_level(KEYPAD_SCL, 1);
  gpio_config_t config = {.pin_bit_mask = (1ULL << KEYPAD_SDA) | (1ULL << KEYPAD_SCL),
                          .mode = GPIO_MODE_INPUT_OUTPUT_OD,
                          .pull_up_en = GPIO_PULLUP_DISABLE,
                          .pull_down_en = GPIO_PULLDOWN_DISABLE};
  esp_err_t err = gpio_config(&config);
  if (err != ESP_OK)
    return err;
  bus_recover();
  uint8_t remembered = 0;
  nvs_handle_t nvs;
  if (nvs_open("keypad", NVS_READONLY, &nvs) == ESP_OK) {
    (void)nvs_get_u8(nvs, "scan69", &remembered);
    nvs_close(nvs);
  }
  restore_on_boot = remembered == 1;
  if (disable_scan() == ESP_OK && restore_on_boot)
    (void)configure_scan(6, 9);
  if (xTaskCreatePinnedToCore(keypad_task, "keypad", 5120, NULL, 2, NULL, 0) != pdPASS)
    return ESP_ERR_NO_MEM;
  return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  xSemaphoreTake(lock, portMAX_DELAY);
  cJSON_AddBoolToObject(root, "present", present);
  cJSON_AddBoolToObject(root, "scanning", scanning);
  cJSON_AddBoolToObject(root, "virtual_enabled", virtual_enabled);
  cJSON_AddNumberToObject(root, "virtual_session", virtual_session);
  cJSON_AddNumberToObject(root, "button_debounce_ms", BUTTON_DEBOUNCE_MS);
  cJSON_AddNumberToObject(root, "button_hold_ms", BUTTON_LONG_MS);
  cJSON *bindings = cJSON_AddArrayToObject(root, "bindings");
  if (bindings) {
    for (unsigned i = 0; i < sizeof(key_bindings) / sizeof(key_bindings[0]); ++i) {
      cJSON *binding = cJSON_CreateObject();
      if (!binding) break;
      cJSON_AddStringToObject(binding, "name", key_bindings[i].name);
      cJSON_AddNumberToObject(binding, "code", key_bindings[i].code);
      cJSON_AddBoolToObject(binding, "action",
#ifdef CONFIG_SONY_CONTROLLER_NODE
          true);
#else
          i < 16 || key_bindings[i].code == CHECK_KEY_CODE ||
          key_bindings[i].code == DISPLAY_KEY_CODE || key_bindings[i].code == MONO_KEY_CODE);
#endif
      cJSON_AddItemToArray(bindings, binding);
    }
  }
  cJSON_AddBoolToObject(root, "restore_on_boot", restore_on_boot);
#ifdef CONFIG_SONY_CONTROLLER_NODE
  cJSON_AddBoolToObject(root, "fixed_6x9", true);
#endif
  cJSON_AddBoolToObject(root, "hardware_scan_active", present &&
      hardware_rows && (hardware_columns || (hardware_columns_hi & 3)));
  cJSON_AddNumberToObject(root, "hardware_row_mask", hardware_rows);
  cJSON_AddNumberToObject(root, "hardware_column_mask",
                          hardware_columns | ((hardware_columns_hi & 3) << 8));
  cJSON_AddNumberToObject(root, "chip_cfg", chip_cfg);
  cJSON_AddNumberToObject(root, "chip_status", chip_status);
  cJSON_AddNumberToObject(root, "chip_key_count", chip_count);
  cJSON_AddNumberToObject(root, "gpio_rows", chip_gpio[0]);
  cJSON_AddNumberToObject(root, "gpio_cols", chip_gpio[1] | ((chip_gpio[2] & 3) << 8));
  cJSON_AddNumberToObject(root, "address", KEYPAD_ADDR);
  cJSON_AddNumberToObject(root, "rows", rows);
  cJSON_AddNumberToObject(root, "columns", columns);
  cJSON_AddNumberToObject(root, "errors", errors);
  cJSON_AddNumberToObject(root, "overflows", overflows);
  cJSON_AddStringToObject(root, "last_error", esp_err_to_name(last_error));
  cJSON *array = cJSON_AddArrayToObject(root, "events");
  if (array) {
    uint32_t first = sequence > EVENT_CAPACITY ? sequence - EVENT_CAPACITY + 1 : 1;
    for (uint32_t n = first; n <= sequence; ++n) {
      key_event_t event = events[(n - 1) % EVENT_CAPACITY];
      cJSON *item = cJSON_CreateObject();
      if (!item)
        break;
      unsigned code = event.raw & 0x7f;
      cJSON_AddNumberToObject(item, "sequence", event.sequence);
      cJSON_AddNumberToObject(item, "time_ms", event.time_ms);
      cJSON_AddNumberToObject(item, "raw", event.raw);
      cJSON_AddNumberToObject(item, "code", code);
      cJSON_AddBoolToObject(item, "pressed", (event.raw & 0x80) != 0);
      cJSON_AddBoolToObject(item, "virtual", event.virtual_input);
      if (code >= 1 && code <= 80) {
        cJSON_AddNumberToObject(item, "row", (code - 1) / 10);
        cJSON_AddNumberToObject(item, "column", (code - 1) % 10);
      }
      cJSON_AddItemToArray(array, item);
    }
  }
  xSemaphoreGive(lock);
  if (!array) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  }
  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t err = httpd_resp_sendstr(req, json);
  free(json);
  return err;
}

static esp_err_t control_handler(httpd_req_t *req) {
  char body[256];
  if (!req->content_len || req->content_len >= sizeof(body))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
  size_t used = 0;
  while (used < req->content_len) {
    int n = httpd_req_recv(req, body + used, req->content_len - used);
    if (n <= 0)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request");
    used += n;
  }
  body[used] = 0;
  cJSON *root = cJSON_Parse(body);
  cJSON *virtual_enable = cJSON_GetObjectItemCaseSensitive(root, "virtual_enable");
  cJSON *virtual_key = cJSON_GetObjectItemCaseSensitive(root, "virtual_key");
  cJSON *keepalive = cJSON_GetObjectItemCaseSensitive(root, "virtual_keepalive");
  if (virtual_enable || virtual_key || keepalive) {
    cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(root, "outputs_disconnected_confirmed");
    cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session");
    cJSON *pressed = cJSON_GetObjectItemCaseSensitive(root, "pressed");
    cJSON *cancel = cJSON_GetObjectItemCaseSensitive(root, "cancel");
    bool session_valid = cJSON_IsNumber(session) && session->valuedouble >= 1 &&
        session->valuedouble <= UINT32_MAX &&
        session->valuedouble == (uint32_t)session->valuedouble;
    bool known = false;
    if (cJSON_IsNumber(virtual_key))
      for (unsigned i = 0; i < sizeof(key_bindings) / sizeof(key_bindings[0]); ++i)
        if (virtual_key->valuedouble == key_bindings[i].code) known = true;
    bool valid = !cJSON_GetObjectItemCaseSensitive(root, "enable") &&
        (virtual_enable ? cJSON_IsBool(virtual_enable) && !virtual_key && !keepalive :
         virtual_key ? known && cJSON_IsBool(pressed) && !keepalive &&
             (!cancel || cJSON_IsBool(cancel)) && (!cJSON_IsTrue(cancel) || cJSON_IsFalse(pressed)) :
         cJSON_IsTrue(keepalive));
    if (!valid || (cJSON_IsTrue(virtual_enable) ? !cJSON_IsTrue(confirmed) : !session_valid)) {
      cJSON_Delete(root);
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
          "Confirm disconnected outputs to arm; use session and mapped press/release codes");
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool accepted;
    int64_t now = esp_timer_get_time() / 1000;
    if (cJSON_IsTrue(virtual_enable)) {
      accepted = !virtual_enabled && !scanning && !sony_screen_is_running();
      if (accepted) {
        virtual_enabled = true;
        if (++virtual_session == 0) ++virtual_session;
        display_button_reset(&display_button, now);
      }
    } else {
      accepted = virtual_enabled && session->valuedouble == virtual_session;
      if (accepted && cJSON_IsFalse(virtual_enable)) {
        virtual_enabled = false;
        display_button_reset(&display_button, now);
        sony_screen_stop();
      } else if (accepted && virtual_key) {
        uint8_t raw = (uint8_t)virtual_key->valueint | (cJSON_IsTrue(pressed) ? 0x80 : 0);
        record_key(raw, now, true, true);
        if (cJSON_IsTrue(cancel))
          display_button_reset(&display_button, now);
      }
    }
    if (accepted) virtual_seen_ms = now;
    xSemaphoreGive(lock);
    cJSON_Delete(root);
    if (!accepted) {
      httpd_resp_set_status(req, "409 Conflict");
      return httpd_resp_sendstr(req, "Stop physical scans before arming; virtual session must be current");
    }
    return status_handler(req);
  }
  cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enable");
  cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "rows");
  cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "columns");
  cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(root, "isolated_matrix_confirmed");
  cJSON *remember = cJSON_GetObjectItemCaseSensitive(root, "remember");
  bool start = cJSON_IsTrue(enabled);
#ifdef CONFIG_SONY_CONTROLLER_NODE
  bool persist = start;
#else
  bool persist = start && cJSON_IsTrue(remember);
#endif
  bool valid = cJSON_IsBool(enabled) &&
      (!remember || cJSON_IsBool(remember)) &&
      (!start || (cJSON_IsTrue(confirmed) && cJSON_IsNumber(r) &&
                  r->valuedouble == r->valueint && r->valueint >= 1 && r->valueint <= 8 &&
                  cJSON_IsNumber(c) && c->valuedouble == c->valueint &&
                  c->valueint >= 1 && c->valueint <= 10 &&
#ifdef CONFIG_SONY_CONTROLLER_NODE
                  r->valueint == 6 && c->valueint == 9 &&
#endif
                  (!persist || (r->valueint == 6 && c->valueint == 9))));
  if (!valid) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
        "Confirm electrically isolated passive switches and specify 1-8 rows, 1-10 columns");
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  if (virtual_enabled) {
    xSemaphoreGive(lock);
    cJSON_Delete(root);
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_sendstr(req, "Disarm virtual keys before configuring the physical matrix");
  }
  esp_err_t err = start && !present ? ESP_ERR_NOT_FOUND : ESP_OK;
  if (!start && present)
    err = disable_scan();
  if (!start || err == ESP_OK) {
    nvs_handle_t nvs;
    esp_err_t save_err = nvs_open("keypad", NVS_READWRITE, &nvs);
    if (save_err == ESP_OK) {
      save_err = nvs_set_u8(nvs, "scan69", persist ? 1 : 0);
      if (save_err == ESP_OK) save_err = nvs_commit(nvs);
      nvs_close(nvs);
    }
    if (save_err == ESP_OK)
      restore_on_boot = persist;
    if (err == ESP_OK) err = save_err;
  }
  if (err == ESP_OK) {
    if (start)
      err = configure_scan((unsigned)r->valueint, (unsigned)c->valueint);
  }
  xSemaphoreGive(lock);
  cJSON_Delete(root);
  if (err != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Keypad unavailable or configuration failed");
  return status_handler(req);
}

esp_err_t sony_keypad_register_http(httpd_handle_t server) {
  httpd_uri_t status = {.uri = "/api/keypad", .method = HTTP_GET, .handler = status_handler};
  httpd_uri_t control = {.uri = "/api/keypad", .method = HTTP_POST, .handler = control_handler};
  esp_err_t err = httpd_register_uri_handler(server, &status);
  return err == ESP_OK ? httpd_register_uri_handler(server, &control) : err;
}
