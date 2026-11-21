#include "sony_controller_menu.h"
#include "sony_controller_radio.h"
#include "sony_screen.h"
#include "sony_search_index.h"
#include "sony_radio_status.h"
#include "sony_uart_link.h"
#include "wifi.h"
#include "radio.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_COUNTRIES = 256, MAX_TAGS = 100, MAX_STATIONS = 100, MAX_RESPONSE = 196608 };
typedef struct { char name[64], code[3]; } country_t;
typedef struct { char name[64]; } tag_t;
typedef struct {
  unsigned type, generation, slot;
  char country[3], tag[64];
  sony_station_command_t station;
} job_t;
typedef struct {
  unsigned type, generation, slot, count;
  bool ok, partial, exists, wifi_ok, link_ok;
  uint32_t clickcount[MAX_STATIONS];
  union {
    country_t countries[MAX_COUNTRIES];
    tag_t tags[MAX_TAGS];
    sony_station_command_t stations[MAX_STATIONS];
  } data;
} result_t;
typedef struct {
  unsigned code, error_generation, preset_status, progress_generation, progress_count;
  bool held;
  result_t *result;
} event_t;
enum { JOB_COUNTRIES = 1, JOB_TAGS, JOB_TOP, JOB_BROWSE, JOB_CHECK_SLOT, JOB_SAVE,
       JOB_CHECK, JOB_SEARCH_INDEX, JOB_SEARCH_RESOLVE };
enum { IDLE, LOADING_COUNTRIES, COUNTRIES, LOADING_TAGS, TAGS, LOADING_TOP,
       TOP, LOADING_BROWSE, BROWSE, STORE, CHECK_SLOT, REPLACE, SAVING, CHECKING,
       ERROR_SCREEN, SAVE_ERROR_SCREEN, LOADING_INDEX, SEARCH_EDIT, SEARCH_STATIONS,
       LOADING_RESOLVE };
static QueueHandle_t events, jobs;
static unsigned screen, request_generation, current_index, selected_slot;
static unsigned country_return, check_return, browse_origin, store_return;
static unsigned country_count, tag_count, station_count, active_letter;
static bool partial;
static char applied_country[3], applied_tag[64], category_tag[64], prefix[23];
static country_t countries[MAX_COUNTRIES];
static tag_t tags[MAX_TAGS];
static sony_station_command_t stations[MAX_STATIONS], snapshot;
static int candidate = -1;
static int64_t notice_until_us;
static sony_search_index_t search_index;
static atomic_uint search_epoch;
static size_t search_first, search_count, search_position;
static bool search_available[128], search_has_cursor;
static unsigned search_cursor, search_progress;
static const char search_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

static void menu_indicators(void) {
  bool storing = screen == STORE || screen == CHECK_SLOT || screen == REPLACE;
  unsigned view = storing ? store_return : screen;
  bool browsing = view == TOP || view == BROWSE || view == LOADING_TOP ||
                  view == LOADING_BROWSE || view == LOADING_INDEX ||
                  view == SEARCH_EDIT || view == SEARCH_STATIONS ||
                  view == LOADING_RESOLVE;
  sony_screen_menu_indicators(browsing, storing);
}

static void notice(const char *text) {
  menu_indicators();
  sony_screen_menu_text(text);
  notice_until_us = esp_timer_get_time() + 1500000;
}

typedef struct { char *data; size_t length; bool overflow; } response_t;
static esp_err_t http_event(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0)
    return ESP_OK;
  response_t *response = event->user_data;
  size_t count = (size_t)event->data_len;
  if (count > MAX_RESPONSE - response->length) {
    response->overflow = true;
    return ESP_FAIL;
  }
  memcpy(response->data + response->length, event->data, count);
  response->length += count;
  response->data[response->length] = 0;
  return ESP_OK;
}

static void url_component(const char *text, char *out, size_t capacity) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') {
      if (n + 1 >= capacity) break;
      out[n++] = *p;
    } else {
      if (n + 3 >= capacity) break;
      out[n++] = '%'; out[n++] = hex[*p >> 4]; out[n++] = hex[*p & 15];
    }
  }
  out[n] = 0;
}

static const char *json_string(cJSON *object, const char *field) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
  return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static bool supported(const char *codec, const char *url) {
  if (strcasecmp(codec, "MP3") && strcasecmp(codec, "AAC") &&
      strcasecmp(codec, "AAC+"))
    return false;
  for (const char *p = url; *p; ++p)
    if (!strncasecmp(p, ".m3u", 4) || !strncasecmp(p, ".pls", 4))
      return false;
  return true;
}

static void accept_entry(result_t *result, unsigned type, cJSON *entry) {
  const char *name = json_string(entry, "name");
  size_t name_length = strlen(name);
  if (!name[0] || (name_length >= 64 && type != JOB_SEARCH_RESOLVE))
    return;
  if (type == JOB_COUNTRIES) {
    const char *code = json_string(entry, "iso_3166_1");
    if (strlen(code) != 2 || result->count == MAX_COUNTRIES)
      return;
    country_t *country = &result->data.countries[result->count++];
    strcpy(country->name, name);
    strcpy(country->code, code);
  } else if (type == JOB_TAGS) {
    if (result->count < MAX_TAGS)
      strcpy(result->data.tags[result->count++].name, name);
  } else {
    const char *url = json_string(entry, "url_resolved");
    const char *id = json_string(entry, "stationuuid");
    if ((type != JOB_SEARCH_RESOLVE && !supported(json_string(entry, "codec"), url)) ||
        strlen(url) > SONY_STATION_URL_MAX || strlen(id) != SONY_STATION_ID_MAX ||
        result->count == MAX_STATIONS)
      return;
    sony_station_command_t *station = &result->data.stations[result->count];
    station->generation = 1;
    strcpy(station->id, id);
    if (name_length > SONY_STATION_NAME_MAX) {
      name_length = SONY_STATION_NAME_MAX;
      while (name_length && ((unsigned char)name[name_length] & 0xc0) == 0x80) --name_length;
    }
    memcpy(station->name, name, name_length);
    station->name[name_length] = 0;
    strcpy(station->url, url);
    station->genre_labels = sony_radio_genre_labels(json_string(entry, "tags"));
    uint8_t bytes[SONY_STATION_WIRE_MAX];
    if (sony_station_encode(station, bytes, sizeof(bytes))) {
      cJSON *count = cJSON_GetObjectItemCaseSensitive(entry, "clickcount");
      result->clickcount[result->count] =
          cJSON_IsNumber(count) && count->valuedouble >= 0 && count->valuedouble <= UINT32_MAX
              ? (uint32_t)count->valuedouble : 0;
      ++result->count;
    }
  }
}

static bool parse_entries(result_t *result, unsigned type,
                          const char *data, size_t length, unsigned *returned) {
  size_t i = 0;
  while (i < length && isspace((unsigned char)data[i])) ++i;
  if (i == length || data[i++] != '[') return false;
  bool first = true;
  for (;;) {
    while (i < length && isspace((unsigned char)data[i])) ++i;
    if (i < length && data[i] == ']') {
      ++i;
      while (i < length && isspace((unsigned char)data[i])) ++i;
      return i == length;
    }
    if (!first) {
      if (i == length || data[i++] != ',') return false;
      while (i < length && isspace((unsigned char)data[i])) ++i;
    }
    if (i == length || data[i] != '{') return false;
    size_t start = i;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    do {
      char ch = data[i++];
      if (quoted) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') quoted = false;
      } else if (ch == '"') quoted = true;
      else if (ch == '{') ++depth;
      else if (ch == '}') --depth;
    } while (i < length && depth);
    if (depth || quoted) return false;
    cJSON *entry = cJSON_ParseWithLength(data + start, i - start);
    if (!cJSON_IsObject(entry)) {
      cJSON_Delete(entry);
      return false;
    }
    ++*returned;
    accept_entry(result, type, entry);
    cJSON_Delete(entry);
    first = false;
  }
}

static result_t *directory_uncached(const job_t *job) {
  result_t *result = heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  response_t response = {.data = heap_caps_malloc(MAX_RESPONSE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
  if (!result || !response.data) {
    free(response.data);
    free(result);
    return NULL;
  }
  result->type = job->type;
  result->generation = job->generation;
  result->slot = job->slot;
  char encoded[192], url[512];
  url_component(job->tag, encoded, sizeof(encoded));
  if (job->type == JOB_COUNTRIES)
    snprintf(url, sizeof(url), "https://all.api.radio-browser.info/json/countries?order=name");
  else if (job->type == JOB_TAGS)
    snprintf(url, sizeof(url), "https://all.api.radio-browser.info/json/tags?order=name&limit=100");
  else if (job->type == JOB_SEARCH_RESOLVE)
    snprintf(url, sizeof(url), "https://all.api.radio-browser.info/json/stations/byuuid/%s",
             job->station.id);
  else
    snprintf(url, sizeof(url),
             "https://all.api.radio-browser.info/json/stations/search?hidebroken=true&limit=%u&order=%s%s%s%s%s%s",
             job->type == JOB_TOP ? 100 : MAX_STATIONS,
             job->type == JOB_TOP ? "clickcount&reverse=true" : "name",
             job->country[0] ? "&countrycode=" : "", job->country,
             encoded[0] ? "&tag=" : "", encoded,
             encoded[0] ? "&tagExact=true" : "");
  esp_http_client_config_t config = {
      .url = url, .event_handler = http_event, .user_data = &response,
      .timeout_ms = 7000, .crt_bundle_attach = esp_crt_bundle_attach,
      .user_agent = "DAR-1000ES-controller/0.3.50",
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = client ? esp_http_client_perform(client) : ESP_ERR_NO_MEM;
  int status = client ? esp_http_client_get_status_code(client) : 0;
  if (client)
    esp_http_client_cleanup(client);
  unsigned returned = 0;
  result->ok = err == ESP_OK && status == 200 && !response.overflow &&
               parse_entries(result, job->type, response.data, response.length, &returned);
  if (result->ok && job->type == JOB_TOP) {
    for (unsigned i = 1; i < result->count; ++i) {
      unsigned j = i;
      while (j > 0) {
        sony_station_command_t *a = &result->data.stations[j - 1], *b = &result->data.stations[j];
        int names = strcasecmp(a->name, b->name);
        bool swap = result->clickcount[j] > result->clickcount[j - 1] ||
                    (result->clickcount[j] == result->clickcount[j - 1] &&
                     (names > 0 || (!names && strcmp(a->id, b->id) > 0)));
        if (!swap) break;
        sony_station_command_t station = *a;
        *a = *b; *b = station;
        uint32_t count = result->clickcount[j - 1];
        result->clickcount[j - 1] = result->clickcount[j];
        result->clickcount[j] = count;
        --j;
      }
    }
  }
  result->partial = (job->type == JOB_TAGS || job->type == JOB_TOP ||
                     job->type == JOB_BROWSE) && returned >= 100;
  free(response.data);
  return result;
}

enum { DIRECTORY_CACHE_SIZE = 6 };
typedef struct {
  unsigned type;
  char country[3], tag[64];
  int64_t fetched_us, used_us;
  result_t *result;
} cache_entry_t;
static cache_entry_t directory_cache[DIRECTORY_CACHE_SIZE];

static result_t *directory(const job_t *job) {
  const char *country = job->type >= JOB_TOP ? job->country : "";
  const char *tag = job->type >= JOB_TOP ? job->tag : "";
  int64_t now = esp_timer_get_time();
  int64_t lifetime = job->type <= JOB_TAGS ? 86400000000LL : 600000000LL;
  cache_entry_t *replacement = &directory_cache[0];
  for (unsigned i = 0; i < DIRECTORY_CACHE_SIZE; ++i) {
    cache_entry_t *entry = &directory_cache[i];
    if (entry->result && entry->type == job->type &&
        !strcmp(entry->country, country) && !strcmp(entry->tag, tag) &&
        now - entry->fetched_us < lifetime) {
      result_t *copy = heap_caps_malloc(sizeof(*copy), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (copy) {
        memcpy(copy, entry->result, sizeof(*copy));
        copy->generation = job->generation;
        entry->used_us = now;
        return copy;
      }
      break;
    }
    if (!entry->result || (replacement->result && entry->used_us < replacement->used_us))
      replacement = entry;
  }
  result_t *result = directory_uncached(job);
  if (result && result->ok) {
    if (!replacement->result)
      replacement->result = heap_caps_malloc(sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (replacement->result) {
      memcpy(replacement->result, result, sizeof(*result));
      replacement->type = job->type;
      strcpy(replacement->country, country);
      strcpy(replacement->tag, tag);
      replacement->fetched_us = replacement->used_us = esp_timer_get_time();
    }
  }
  return result;
}

static void index_progress(size_t count, void *context) {
  const job_t *job = context;
  event_t event = {.progress_generation = job->generation, .progress_count = (unsigned)count};
  xQueueSend(events, &event, 0);
}

static void worker(void *unused) {
  (void)unused;
  job_t job;
  for (;;) {
    if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdTRUE)
      continue;
    result_t *result;
    if (job.type <= JOB_BROWSE) {
      result = directory(&job);
    } else if (job.type == JOB_SEARCH_RESOLVE) {
      result = directory_uncached(&job);
    } else if (job.type == JOB_SEARCH_INDEX) {
      result = heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (result) {
        result->type = job.type;
        result->generation = job.generation;
        result->ok = sony_search_index_load(&search_index, job.country,
                                             &search_epoch, job.slot,
                                             index_progress, &job) == ESP_OK;
      }
    } else {
      result = heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (result) {
        result->type = job.type;
        result->generation = job.generation;
        result->slot = job.slot;
        if (job.type == JOB_CHECK_SLOT) {
          sony_station_command_t current;
          esp_err_t err = sony_controller_get_preset(job.slot, &current);
          result->ok = err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
          result->exists = err == ESP_OK;
        } else if (job.type == JOB_SAVE) {
          result->ok = sony_controller_save_preset(job.slot, &job.station) == ESP_OK;
        } else {
          uint32_t token = sony_uart_link_request_check();
          (void)esp_wifi_disconnect();
          int64_t disconnect_deadline = esp_timer_get_time() + 1000000;
          while (wifi_is_connected() && esp_timer_get_time() < disconnect_deadline)
            vTaskDelay(pdMS_TO_TICKS(20));
          bool disconnected = !wifi_is_connected();
          (void)esp_wifi_connect();
          int64_t wifi_deadline = esp_timer_get_time() + 5000000;
          while (disconnected && !wifi_is_connected() && esp_timer_get_time() < wifi_deadline)
            vTaskDelay(pdMS_TO_TICKS(20));
          result->wifi_ok = disconnected && wifi_is_connected();
          int64_t deadline = esp_timer_get_time() + 2000000;
          while (!sony_uart_link_check_result(token, &result->link_ok) &&
                 esp_timer_get_time() < deadline)
            vTaskDelay(pdMS_TO_TICKS(20));
          result->ok = true;
        }
      }
    }
    if (!result) {
      event_t error = {.error_generation = job.generation};
      xQueueSend(events, &error, pdMS_TO_TICKS(100));
      continue;
    }
    event_t event = {.result = result};
    if (xQueueSend(events, &event, pdMS_TO_TICKS(100)) != pdPASS)
      free(result);
  }
}

static void start_job(unsigned type) {
  job_t job = {.type = type, .generation = ++request_generation};
  strcpy(job.country, applied_country);
  strcpy(job.tag, applied_tag);
  xQueueOverwrite(jobs, &job);
}

static void start_search(void) {
  job_t job = {.type = JOB_SEARCH_INDEX, .generation = ++request_generation,
               .slot = atomic_fetch_add(&search_epoch, 1) + 1};
  strcpy(job.country, applied_country);
  prefix[0] = 0;
  search_progress = 0;
  screen = LOADING_INDEX;
  xQueueOverwrite(jobs, &job);
}

static void search_refresh_chars(unsigned preferred) {
  sony_search_index_chars(&search_index, prefix, search_available);
  search_has_cursor = false;
  size_t length = strlen(search_alphabet);
  for (size_t i = 0; i < length; ++i) {
    size_t index = (preferred + i) % length;
    if (search_available[(unsigned char)search_alphabet[index]]) {
      search_cursor = (unsigned)index;
      search_has_cursor = true;
      break;
    }
  }
}

static void search_move_cursor(int direction, unsigned steps) {
  if (!search_has_cursor) return;
  unsigned length = (unsigned)strlen(search_alphabet);
  while (steps--) {
    do {
      search_cursor = (search_cursor + (direction > 0 ? 1 : length - 1)) % length;
    } while (!search_available[(unsigned char)search_alphabet[search_cursor]]);
  }
}

static void start_resolve(bool store) {
  if (!search_count || search_position >= search_count) return;
  job_t job = {.type = JOB_SEARCH_RESOLVE, .generation = ++request_generation,
               .slot = store ? 1 : 0};
  sony_search_index_uuid(&search_index.entries[search_first + search_position], job.station.id);
  screen = LOADING_RESOLVE;
  xQueueOverwrite(jobs, &job);
}

static void display(void) {
  menu_indicators();
  char text[96];
  switch (screen) {
  case IDLE: sony_screen_menu_text(NULL); return;
  case LOADING_COUNTRIES: case LOADING_TAGS: case LOADING_TOP: case LOADING_BROWSE:
    strcpy(text, "LOADING"); break;
  case COUNTRIES:
    snprintf(text, sizeof(text), "%s", current_index ? countries[current_index - 1].name : "ALL COUNTRIES"); break;
  case TAGS:
    snprintf(text, sizeof(text), "%s%s", partial ? "~" : "",
             current_index ? tags[current_index - 1].name : "ALL GENRES"); break;
  case TOP:
    snprintf(text, sizeof(text), "%s%s", partial ? "~" : "",
             current_index < station_count ? stations[current_index].name :
             station_count ? "BROWSE ALL" : "NO MATCH BROWSE ALL"); break;
  case BROWSE:
    if (candidate < 0)
      snprintf(text, sizeof(text), "%s%c %s", prefix, 'A' + active_letter,
               partial ? "PARTIAL" : "NO MATCH");
    else
      snprintf(text, sizeof(text), "%s%s:%c %s", partial ? "~" : "", prefix, 'A' + active_letter,
               stations[candidate].name);
    break;
  case STORE: strcpy(text, "STORE PRESET --"); break;
  case CHECK_SLOT: case SAVING: strcpy(text, "SAVING"); break;
  case REPLACE: snprintf(text, sizeof(text), "REPLACE PRESET %02u?", selected_slot); break;
  case CHECKING: strcpy(text, "CHECKING CONNECTIONS"); break;
  case LOADING_INDEX:
    snprintf(text, sizeof(text), "INDEX %s %u",
             applied_country[0] ? applied_country : SONY_SEARCH_DEFAULT_COUNTRY,
             search_progress);
    break;
  case LOADING_RESOLVE: strcpy(text, "LOADING STATION"); break;
  case SEARCH_EDIT: {
    size_t n = strlen(prefix);
    snprintf(text, sizeof(text), "%s", prefix);
    if (search_has_cursor && n < 22 && (esp_timer_get_time() / 500000) % 2 == 0) {
      char ch = search_alphabet[search_cursor];
      text[n] = ch == ' ' ? '_' : ch;
      text[n + 1] = 0;
    }
    break;
  }
  case SEARCH_STATIONS:
    snprintf(text, sizeof(text), "%s",
             search_count ? search_index.entries[search_first + search_position].name : "NO MATCH");
    break;
  case SAVE_ERROR_SCREEN: strcpy(text, "SAVE ERROR"); break;
  default: strcpy(text, "DIRECTORY ERROR"); break;
  }
  sony_screen_menu_text(text);
}

static void find_candidate(int direction) {
  size_t n = strlen(prefix);
  candidate = -1;
  for (unsigned attempt = 0; attempt < 26; ++attempt) {
    for (unsigned i = 0; i < station_count; ++i) {
      const char *name = stations[i].name;
      if (strlen(name) > n && !strncasecmp(name, prefix, n) &&
          toupper((unsigned char)name[n]) == 'A' + (int)active_letter) {
        candidate = (int)i;
        return;
      }
    }
    active_letter = (active_letter + (direction > 0 ? 1 : 25)) % 26;
  }
}

static unsigned preset_for(unsigned code) {
  static const unsigned codes[] = {5,6,7,8,1,2,3,9,15,16,17,18,11,12,13,19};
  for (unsigned i = 0; i < 16; ++i)
    if (code == codes[i]) return i + 1;
  return 0;
}

static const char *category(unsigned code) {
  static const struct { unsigned code; const char *tag; } map[] = {
      {35,"news"},{36,"politics"},{37,"information"},{38,"sports"},{31,"education"},
      {32,"drama"},{33,"culture"},{39,"science"},{45,"variety"},{46,"pop"},
      {47,"rock"},{48,"easy listening"},{28,"music"},{21,"talk"},{41,"light classical"},
      {42,"classical"},{43,"other music"},
  };
  for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
    if (map[i].code == code) return map[i].tag;
  return NULL;
}

static unsigned stable_return_screen(unsigned current) {
  switch (current) {
  case LOADING_COUNTRIES: case LOADING_TAGS: case LOADING_TOP:
  case LOADING_BROWSE: case CHECKING: case ERROR_SCREEN: case SAVE_ERROR_SCREEN:
  case LOADING_INDEX: case LOADING_RESOLVE:
    return IDLE;
  default:
    return current;
  }
}

static void handle_key(unsigned code, bool held) {
  if (screen == LOADING_INDEX || screen == LOADING_RESOLVE) {
    if (code == 52 && !held) {
      if (screen == LOADING_INDEX) atomic_fetch_add(&search_epoch, 1);
      ++request_generation;
      screen = IDLE;
      display();
    }
    return;
  }
  unsigned preset = preset_for(code);
  if (screen == SAVING || screen == CHECK_SLOT)
    return;
  if (preset) {
    if (screen == STORE && !held) {
      selected_slot = preset;
      job_t job = {.type = JOB_CHECK_SLOT, .generation = ++request_generation, .slot = preset};
      xQueueOverwrite(jobs, &job);
      screen = CHECK_SLOT;
    } else if (screen != STORE && screen != REPLACE && !held) {
      ++request_generation;
      screen = IDLE;
      radio_select_preset(preset);
    }
    display();
    return;
  }
  if ((screen == STORE && (code != 52 || held)) ||
      (screen == REPLACE && ((code != 52 && code != 51) || held)))
    return;
  if (code == 26 && !held) {
    start_search();
  } else if (code == 26 && held && screen == SEARCH_STATIONS) {
    start_resolve(false);
  } else if (code == 52 && !held) {
    ++request_generation;
    if (screen == SEARCH_STATIONS) {
      screen = SEARCH_EDIT;
    } else if (screen == SEARCH_EDIT && prefix[0]) {
      size_t n = strlen(prefix);
      char previous = prefix[n - 1];
      prefix[n - 1] = 0;
      const char *position = strchr(search_alphabet, previous);
      search_refresh_chars(position ? (unsigned)(position - search_alphabet) : 0);
    } else if (screen == BROWSE && prefix[0]) {
      prefix[strlen(prefix) - 1] = 0;
      find_candidate(1);
    } else if (screen == STORE || screen == REPLACE) {
      screen = store_return;
    } else if (screen == COUNTRIES || screen == LOADING_COUNTRIES) {
      screen = country_return;
    } else if (screen == TAGS) {
      current_index = 0;
      for (unsigned i = 0; i < country_count; ++i)
        if (!strcmp(countries[i].code, applied_country)) current_index = i + 1;
      country_return = TAGS;
      screen = COUNTRIES;
    } else if (screen == BROWSE && browse_origin == TOP) {
      strcpy(applied_tag, category_tag);
      screen = LOADING_TOP;
      start_job(JOB_TOP);
    } else if (screen == BROWSE && browse_origin == TAGS) {
      current_index = 0;
      screen = TAGS;
    } else {
      screen = IDLE;
    }
  } else if (code == 25 && !held) {
    check_return = stable_return_screen(screen);
    screen = CHECKING;
    start_job(JOB_CHECK);
  } else if (code == 27 && !held) {
    if (screen == SEARCH_EDIT) {
      sony_search_index_range(&search_index, prefix, &search_first, &search_count);
      search_position = 0;
      if (search_count) screen = SEARCH_STATIONS;
      else { notice("NO MATCH"); return; }
    } else if (screen == SEARCH_STATIONS) {
      screen = SEARCH_EDIT;
    } else if (screen == IDLE) {
      browse_origin = IDLE;
      prefix[0] = 0;
      active_letter = 0;
      screen = LOADING_BROWSE;
      start_job(JOB_BROWSE);
    } else if (screen == BROWSE && browse_origin == IDLE) {
      screen = IDLE;
    } else {
      notice("AUDIO UNAVAILABLE");
      return;
    }
  } else if (code == 49 && !held) {
    country_return = stable_return_screen(screen);
    screen = LOADING_COUNTRIES;
    start_job(JOB_COUNTRIES);
  } else if (category(code) && !held) {
    strcpy(applied_tag, category(code));
    strcpy(category_tag, applied_tag);
    screen = LOADING_TOP;
    start_job(JOB_TOP);
  } else if ((code == 22 || code == 23 || code == 53 || code == 59) && !held) {
    int direction = code == 22 || code == 53 ? 1 : -1;
    unsigned step = code == 53 || code == 59 ? 6 : 1;
    if (screen == COUNTRIES)
      current_index = (current_index + (direction > 0 ? step : (country_count + 1) - step % (country_count + 1))) % (country_count + 1);
    else if (screen == TAGS && step == 1)
      current_index = (current_index + (direction > 0 ? 1 : tag_count)) % (tag_count + 1);
    else if (screen == TOP && step == 1)
      current_index = (current_index + (direction > 0 ? 1 : station_count)) % (station_count + 1);
    else if (screen == SEARCH_EDIT)
      search_move_cursor(direction, step);
    else if (screen == SEARCH_STATIONS && search_count)
      search_position = (search_position + (direction > 0 ? step : search_count - step % search_count)) % search_count;
    else if (screen == BROWSE) {
      active_letter = (active_letter + (direction > 0 ? step : 26 - step)) % 26;
      find_candidate(direction);
    }
  } else if (code == 51) {
    if (held && screen == IDLE) {
      if (!sony_uart_link_current_station(&snapshot)) {
        notice("NO RADIO STATION");
        return;
      }
      store_return = IDLE;
      screen = STORE;
    } else if (held && screen == SEARCH_STATIONS && search_count) {
      store_return = SEARCH_STATIONS;
      start_resolve(true);
    } else if (held && ((screen == TOP && current_index < station_count) ||
                 (screen == BROWSE && candidate >= 0))) {
      snapshot = stations[screen == TOP ? current_index : (unsigned)candidate];
      store_return = screen;
      screen = STORE;
    } else if (!held && screen == COUNTRIES) {
      if (current_index) strcpy(applied_country, countries[current_index - 1].code);
      else applied_country[0] = 0;
      screen = LOADING_TAGS;
      start_job(JOB_TAGS);
    } else if (!held && screen == TAGS) {
      if (current_index) strcpy(applied_tag, tags[current_index - 1].name);
      else applied_tag[0] = 0;
      browse_origin = TAGS;
      prefix[0] = 0; active_letter = 0;
      screen = LOADING_BROWSE;
      start_job(JOB_BROWSE);
    } else if (!held && screen == TOP && current_index == station_count) {
      browse_origin = TOP;
      prefix[0] = 0; active_letter = 0;
      screen = LOADING_BROWSE;
      start_job(JOB_BROWSE);
    } else if (!held && screen == TOP && current_index < station_count) {
      if (sony_controller_select_station(&stations[current_index]) == ESP_OK) {
        screen = IDLE;
        notice("AUDIO UNCONFIRMED");
        return;
      }
    } else if (!held && screen == SEARCH_EDIT && search_has_cursor &&
               strlen(prefix) < sizeof(prefix) - 1) {
      size_t n = strlen(prefix);
      prefix[n] = search_alphabet[search_cursor]; prefix[n + 1] = 0;
      search_refresh_chars(0);
    } else if (!held && screen == BROWSE && candidate >= 0 &&
               strlen(prefix) < sizeof(prefix) - 1) {
      size_t n = strlen(prefix);
      prefix[n] = 'A' + active_letter; prefix[n + 1] = 0;
      active_letter = 0; find_candidate(1);
    } else if (!held && screen == REPLACE) {
      job_t job = {.type = JOB_SAVE, .generation = ++request_generation,
                   .slot = selected_slot, .station = snapshot};
      xQueueOverwrite(jobs, &job);
      screen = SAVING;
    }
  } else if (code == 26 && held && screen == BROWSE && candidate >= 0) {
    sony_station_command_t *station = &stations[(unsigned)candidate];
    if (sony_controller_select_station(station) == ESP_OK) {
      screen = IDLE;
      notice("AUDIO UNCONFIRMED");
      return;
    }
  }
  display();
}

static void handle_result(result_t *result) {
  if (result->generation != request_generation)
    return;
  if (!result->ok) {
    if (result->type == JOB_SEARCH_RESOLVE) {
      screen = SEARCH_STATIONS;
      notice("STATION UNAVAILABLE");
      return;
    }
    screen = result->type == JOB_SAVE || result->type == JOB_CHECK_SLOT
                 ? SAVE_ERROR_SCREEN : ERROR_SCREEN;
    display();
    return;
  }
  switch (result->type) {
  case JOB_COUNTRIES:
    current_index = 0;
    country_count = result->count;
    memcpy(countries, result->data.countries, country_count * sizeof(*countries));
    for (unsigned i = 0; i < country_count; ++i)
      if (!strcmp(countries[i].code, applied_country)) current_index = i + 1;
    screen = COUNTRIES; break;
  case JOB_TAGS:
    current_index = 0;
    tag_count = result->count;
    partial = result->partial;
    memcpy(tags, result->data.tags, tag_count * sizeof(*tags));
    screen = TAGS; break;
  case JOB_TOP:
  case JOB_BROWSE:
    current_index = 0;
    station_count = result->count;
    partial = result->partial;
    if (result->type == JOB_TOP && station_count > 10) station_count = 10;
    memcpy(stations, result->data.stations, station_count * sizeof(*stations));
    screen = result->type == JOB_TOP ? TOP : BROWSE;
    if (screen == BROWSE) find_candidate(1);
    break;
  case JOB_CHECK_SLOT:
    if (result->exists) screen = REPLACE;
    else {
      job_t job = {.type = JOB_SAVE, .generation = ++request_generation,
                   .slot = result->slot, .station = snapshot};
      xQueueOverwrite(jobs, &job);
      screen = SAVING;
    }
    break;
  case JOB_SAVE:
    screen = store_return;
    notice("STORED");
    return;
  case JOB_CHECK: {
    char text[32];
    screen = check_return;
    snprintf(text, sizeof(text), "WIFI %s LINK %s",
             result->wifi_ok ? "OK" : "OFF", result->link_ok ? "OK" : "OFF");
    notice(text);
    return;
  }
  case JOB_SEARCH_INDEX:
    if (!search_index.count) {
      screen = IDLE;
      notice("NO STATIONS");
      return;
    }
    screen = SEARCH_EDIT;
    search_refresh_chars(0);
    break;
  case JOB_SEARCH_RESOLVE:
    if (result->count != 1) {
      screen = SEARCH_STATIONS;
      notice("STATION UNAVAILABLE");
      return;
    }
    if (result->slot) {
      snapshot = result->data.stations[0];
      screen = STORE;
    } else if (sony_controller_select_station(&result->data.stations[0]) == ESP_OK) {
      screen = IDLE;
      notice("AUDIO UNCONFIRMED");
      return;
    } else {
      screen = SEARCH_STATIONS;
      notice("STATION UNAVAILABLE");
      return;
    }
    break;
  }
  display();
}

static void menu_task(void *unused) {
  (void)unused;
  event_t event;
  int64_t last_blink = -1;
  for (;;) {
    if (xQueueReceive(events, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
      if (notice_until_us && esp_timer_get_time() >= notice_until_us) {
        notice_until_us = 0;
        display();
      } else if (!notice_until_us && screen == SEARCH_EDIT) {
        int64_t blink = esp_timer_get_time() / 500000;
        if (blink != last_blink) {
          last_blink = blink;
          display();
        }
      }
      continue;
    }
    if (event.progress_generation) {
      if (event.progress_generation == request_generation && screen == LOADING_INDEX) {
        search_progress = event.progress_count;
        display();
      }
    } else if (event.result) {
      notice_until_us = 0;
      handle_result(event.result);
      free(event.result);
    } else if (event.error_generation) {
      notice_until_us = 0;
      if (event.error_generation == request_generation) {
        screen = ERROR_SCREEN;
        display();
      }
    } else if (event.preset_status) {
      notice(event.preset_status == SONY_PRESET_QUEUED ? "AUDIO UNCONFIRMED" :
             event.preset_status == SONY_PRESET_UNASSIGNED ? "UNASSIGNED" : "PRESET ERROR");
    } else {
      notice_until_us = 0;
      handle_key(event.code, event.held);
    }
  }
}

esp_err_t sony_controller_menu_init(void) {
  events = xQueueCreate(24, sizeof(event_t));
  jobs = xQueueCreate(1, sizeof(job_t));
  if (!events || !jobs)
    return ESP_ERR_NO_MEM;
  if (xTaskCreate(worker, "directory", 8192, NULL, 2, NULL) != pdPASS ||
      xTaskCreate(menu_task, "radio_menu", 8192, NULL, 2, NULL) != pdPASS)
    return ESP_ERR_NO_MEM;
  return ESP_OK;
}

void sony_controller_menu_key(unsigned code, bool held) {
  event_t event = {.code = code, .held = held};
  if (events)
    xQueueSend(events, &event, 0);
}

void sony_controller_menu_preset_status(sony_preset_status_t status) {
  event_t event = {.preset_status = status};
  if (events && status)
    xQueueSend(events, &event, 0);
}
