#include "sony_search_index.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_rom_crc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

enum { SEARCH_COUNTRY_CAPACITY = 30000, SEARCH_NL_CAPACITY = 8192,
       SEARCH_PAGE_SIZE = 1000, SEARCH_OBJECT_MAX = 32768 };

typedef struct {
  sony_search_index_t *index;
  size_t capacity, length, returned;
  char *object;
  unsigned state, depth;
  bool quoted, escaped, overflow, invalid;
  const atomic_uint *epoch;
  unsigned expected_epoch;
} search_page_t;

static bool uuid_parse(const char *text, uint8_t bytes[16]) {
  if (strlen(text) != 36) return false;
  unsigned out = 0, nibble = 0;
  for (unsigned i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i] != '-') return false;
      continue;
    }
    unsigned char ch = (unsigned char)text[i];
    if (!isxdigit(ch)) return false;
    unsigned value = ch >= '0' && ch <= '9' ? (unsigned)(ch - '0') : (unsigned)(tolower(ch) - 'a' + 10);
    if (!(nibble++ & 1)) bytes[out] = (uint8_t)(value << 4);
    else bytes[out++] |= (uint8_t)value;
  }
  return out == 16;
}

void sony_search_index_uuid(const sony_search_entry_t *entry, char uuid[37]) {
  static const char hex[] = "0123456789abcdef";
  unsigned output = 0;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) uuid[output++] = '-';
    uuid[output++] = hex[entry->uuid[i] >> 4];
    uuid[output++] = hex[entry->uuid[i] & 15];
  }
  uuid[output] = 0;
}

static int entry_compare(const void *left, const void *right) {
  const sony_search_entry_t *a = left, *b = right;
  int order = strcasecmp(a->name, b->name);
  return order ? order : memcmp(a->uuid, b->uuid, sizeof(a->uuid));
}

void sony_search_index_range(const sony_search_index_t *index, const char *prefix,
                             size_t *first, size_t *count) {
  size_t n = strlen(prefix), lo = 0, hi = index->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (strncasecmp(index->entries[mid].name, prefix, n) < 0) lo = mid + 1;
    else hi = mid;
  }
  *first = lo;
  hi = index->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (strncasecmp(index->entries[mid].name, prefix, n) <= 0) lo = mid + 1;
    else hi = mid;
  }
  *count = lo - *first;
}

void sony_search_index_chars(const sony_search_index_t *index, const char *prefix,
                             bool available[128]) {
  memset(available, 0, 128 * sizeof(*available));
  size_t first, count, n = strlen(prefix);
  sony_search_index_range(index, prefix, &first, &count);
  for (size_t i = first; i < first + count; ++i) {
    const char *name = index->entries[i].name;
    if (strlen(name) <= n) continue;
    unsigned char ch = (unsigned char)name[n];
    if (ch < 128) available[(unsigned char)toupper(ch)] = true;
  }
}

static bool append_object(search_page_t *page) {
  page->object[page->length] = 0;
  cJSON *object = cJSON_Parse(page->object);
  if (!cJSON_IsObject(object)) {
    cJSON_Delete(object);
    return false;
  }
  ++page->returned;
  cJSON *name = cJSON_GetObjectItemCaseSensitive(object, "name");
  cJSON *uuid = cJSON_GetObjectItemCaseSensitive(object, "stationuuid");
  uint8_t bytes[16];
  if (cJSON_IsString(name) && name->valuestring && name->valuestring[0] &&
      cJSON_IsString(uuid) && uuid->valuestring && uuid_parse(uuid->valuestring, bytes)) {
    if (page->index->count == page->capacity) {
      page->overflow = true;
      cJSON_Delete(object);
      return false;
    }
    sony_search_entry_t *entry = &page->index->entries[page->index->count++];
    memset(entry, 0, sizeof(*entry));
    size_t length = strlen(name->valuestring);
    if (length > sizeof(entry->name) - 1) {
      length = sizeof(entry->name) - 1;
      while (length && ((unsigned char)name->valuestring[length] & 0xc0) == 0x80) --length;
    }
    memcpy(entry->name, name->valuestring, length);
    memcpy(entry->uuid, bytes, sizeof(bytes));
  }
  cJSON_Delete(object);
  return true;
}

static esp_err_t search_event(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
  search_page_t *page = event->user_data;
  if (page->invalid) return ESP_FAIL;
  if (atomic_load(page->epoch) != page->expected_epoch) {
    page->invalid = true;
    return ESP_FAIL;
  }
  const unsigned char *data = event->data;
  for (int i = 0; i < event->data_len; ++i) {
    unsigned char ch = data[i];
    if (page->state == 3) {
      if (page->length == SEARCH_OBJECT_MAX) { page->invalid = true; return ESP_FAIL; }
      page->object[page->length++] = (char)ch;
      if (page->quoted) {
        if (page->escaped) page->escaped = false;
        else if (ch == '\\') page->escaped = true;
        else if (ch == '"') page->quoted = false;
      } else if (ch == '"') page->quoted = true;
      else if (ch == '{') ++page->depth;
      else if (ch == '}' && --page->depth == 0) {
        if (!append_object(page)) { page->invalid = true; return ESP_FAIL; }
        page->state = 4;
      }
      continue;
    }
    if (isspace(ch)) continue;
    if (page->state == 0 && ch == '[') page->state = 1;
    else if ((page->state == 1 || page->state == 2) && ch == '{') {
      page->length = 1; page->object[0] = '{'; page->depth = 1;
      page->quoted = page->escaped = false; page->state = 3;
    } else if ((page->state == 1 || page->state == 4) && ch == ']') page->state = 5;
    else if (page->state == 4 && ch == ',') page->state = 2;
    else { page->invalid = true; return ESP_FAIL; }
  }
  return ESP_OK;
}

static esp_err_t load_country(sony_search_index_t *index, size_t capacity,
                              const char *country, const atomic_uint *epoch,
                              unsigned expected_epoch, sony_search_progress_t progress,
                              void *context) {
  char *object = heap_caps_malloc(SEARCH_OBJECT_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!object) return ESP_ERR_NO_MEM;
  esp_err_t result = ESP_OK;
  for (unsigned offset = 0;; offset += SEARCH_PAGE_SIZE) {
    if (atomic_load(epoch) != expected_epoch) { result = ESP_ERR_INVALID_STATE; break; }
    char url[256];
    snprintf(url, sizeof(url),
             "https://all.api.radio-browser.info/json/stations/search?order=name&limit=%u&offset=%u&countrycode=%s",
             SEARCH_PAGE_SIZE, offset, country);
    search_page_t page = {.index = index, .capacity = capacity, .object = object,
                          .epoch = epoch, .expected_epoch = expected_epoch};
    esp_http_client_config_t config = {
        .url = url, .event_handler = search_event, .user_data = &page,
        .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "DAR-1000ES-controller/0.3.50",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = client ? esp_http_client_perform(client) : ESP_ERR_NO_MEM;
    int status = client ? esp_http_client_get_status_code(client) : 0;
    if (client) esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || page.invalid || page.state != 5 ||
        atomic_load(epoch) != expected_epoch) {
      result = page.overflow ? ESP_ERR_NO_MEM : ESP_FAIL;
      break;
    }
    if (progress) progress(index->count, context);
    if (page.returned < SEARCH_PAGE_SIZE) break;
  }
  free(object);
  return result;
}

static esp_err_t build_index(sony_search_index_t *index, size_t capacity,
                             const char *country, const atomic_uint *epoch,
                             unsigned expected_epoch, sony_search_progress_t progress,
                             void *context) {
  index->entries = heap_caps_malloc(capacity * sizeof(*index->entries),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!index->entries) return ESP_ERR_NO_MEM;
  index->count = 0;
  esp_err_t err = load_country(index, capacity, country, epoch, expected_epoch, progress, context);
  if (err == ESP_OK) qsort(index->entries, index->count, sizeof(*index->entries), entry_compare);
  else { free(index->entries); index->entries = NULL; index->count = 0; }
  return err;
}

#define SEARCH_CACHE_PATH "/spiffs/direct-nl-v1.bin"
#define SEARCH_CACHE_TEMP "/spiffs/direct-nl-v1.tmp"
#define SEARCH_CACHE_MAGIC 0x314c4e44U

static bool search_cache_read(sony_search_index_t *index) {
  FILE *file = fopen(SEARCH_CACHE_PATH, "rb");
  if (!file) return false;
  uint32_t header[3];
  sony_search_entry_t *entries = NULL;
  bool valid = false;
  if (fread(header, sizeof(header), 1, file) == 1 &&
      header[0] == SEARCH_CACHE_MAGIC && header[1] && header[1] <= SEARCH_NL_CAPACITY) {
    size_t bytes = header[1] * sizeof(*entries);
    entries = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    valid = entries && fread(entries, 1, bytes, file) == bytes &&
            fgetc(file) == EOF && !ferror(file) &&
            esp_rom_crc32_le(0, (const uint8_t *)entries, bytes) == header[2];
  }
  fclose(file);
  if (valid) {
    index->entries = entries;
    index->count = header[1];
  } else {
    free(entries);
  }
  return valid;
}

static void search_cache_write(const sony_search_index_t *index) {
  if (!index->count) return;
  FILE *file = fopen(SEARCH_CACHE_TEMP, "wb");
  if (!file) return;
  size_t bytes = index->count * sizeof(*index->entries);
  uint32_t header[] = {SEARCH_CACHE_MAGIC, (uint32_t)index->count,
      esp_rom_crc32_le(0, (const uint8_t *)index->entries, bytes)};
  bool saved = fwrite(header, sizeof(header), 1, file) == 1 &&
               fwrite(index->entries, 1, bytes, file) == bytes &&
               fflush(file) == 0 && fsync(fileno(file)) == 0;
  if (fclose(file) != 0) saved = false;
  if (saved) {
    // SPIFFS rename does not replace an existing (unreadable) cache.
    remove(SEARCH_CACHE_PATH);
    rename(SEARCH_CACHE_TEMP, SEARCH_CACHE_PATH);
  }
  remove(SEARCH_CACHE_TEMP);
}

esp_err_t sony_search_index_load(sony_search_index_t *index, const char *country,
                                const atomic_uint *epoch, unsigned expected_epoch,
                                sony_search_progress_t progress, void *context) {
  const char *scope = country[0] ? country : SONY_SEARCH_DEFAULT_COUNTRY;
  if (index->entries && !strcmp(index->country, scope))
    return ESP_OK;
  free(index->entries);
  index->entries = NULL; index->count = 0;
  bool dutch = !strcmp(scope, SONY_SEARCH_DEFAULT_COUNTRY);
  if (atomic_load(epoch) != expected_epoch) return ESP_ERR_INVALID_STATE;
  bool restored = dutch && search_cache_read(index);
  esp_err_t err = restored ? ESP_OK :
      build_index(index, dutch ? SEARCH_NL_CAPACITY : SEARCH_COUNTRY_CAPACITY,
                  scope, epoch, expected_epoch, progress, context);
  if (err == ESP_OK) {
    snprintf(index->country, sizeof(index->country), "%s", scope);
    if (dutch && !restored && atomic_load(epoch) == expected_epoch)
      search_cache_write(index);
  }
  return err;
}
