import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
CJSON = CORE / "packages/framework-espidf/components/json/cJSON"


class SearchIndexTests(unittest.TestCase):
    def test_default_netherlands_and_country_switching(self):
        source = (ROOT / "main/sony_search_index.c").read_text()
        load = source[source.index("#define SEARCH_CACHE_PATH"):].replace("/spiffs/", "")
        header = (ROOT / "main/sony_search_index.h").read_text()
        default = next(line for line in header.splitlines()
                       if line.startswith("#define SONY_SEARCH_DEFAULT_COUNTRY "))
        harness = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_ERR_INVALID_STATE = 1, SEARCH_COUNTRY_CAPACITY = 30000,
       SEARCH_NL_CAPACITY = 8192, MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2 };
#define heap_caps_malloc(bytes, caps) malloc(bytes)
static uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *bytes, size_t count) {
    while (count--) crc = 33 * crc + *bytes++;
    return crc;
}
typedef struct { char name[64]; unsigned char uuid[16]; } sony_search_entry_t;
typedef struct { sony_search_entry_t *entries; size_t count; char country[3]; } sony_search_index_t;
typedef void (*sony_search_progress_t)(size_t, void *);
static unsigned calls;
static char last_scope[3];
static esp_err_t build_index(sony_search_index_t *index, size_t capacity,
                             const char *scope, const atomic_uint *epoch,
                             unsigned expected, sony_search_progress_t progress,
                             void *context) {
    (void)capacity; (void)epoch; (void)expected; (void)progress; (void)context;
    ++calls; snprintf(last_scope, sizeof(last_scope), "%s", scope);
    index->entries = calloc(1, sizeof(*index->entries)); assert(index->entries);
    strcpy(index->entries[0].name, scope);
    index->count = 1; return ESP_OK;
}
""" + default + "\n" + load + r"""
int main(void) {
    sony_search_index_t index = {0};
    atomic_uint epoch = 1;
    assert(sony_search_index_load(&index, "", &epoch, 1, NULL, NULL) == ESP_OK);
    assert(calls == 1 && !strcmp(last_scope, "NL"));
    assert(sony_search_index_load(&index, "NL", &epoch, 1, NULL, NULL) == ESP_OK);
    assert(calls == 1);
    assert(sony_search_index_load(&index, "FR", &epoch, 1, NULL, NULL) == ESP_OK);
    assert(calls == 2 && !strcmp(last_scope, "FR") && !strcmp(index.country, "FR"));
    assert(sony_search_index_load(&index, "", &epoch, 1, NULL, NULL) == ESP_OK);
    assert(calls == 2 && !strcmp(index.country, "NL"));
    free(index.entries);
    index = (sony_search_index_t){0};
    assert(sony_search_index_load(&index, "", &epoch, 1, NULL, NULL) == ESP_OK);
    assert(calls == 2 && index.count == 1 && !strcmp(index.entries[0].name, "NL"));
    free(index.entries);
    index = (sony_search_index_t){0};
    FILE *file = fopen(SEARCH_CACHE_PATH, "r+b");
    assert(file && fseek(file, (long)(3 * sizeof(uint32_t)), SEEK_SET) == 0);
    assert(fputc('X', file) == 'X' && fclose(file) == 0);
    assert(sony_search_index_load(&index, "", &epoch, 1, NULL, NULL) == ESP_OK);
    assert(calls == 3 && !strcmp(index.entries[0].name, "NL"));
    free(index.entries);
    index = (sony_search_index_t){0};
    assert(sony_search_index_load(&index, "", &epoch, 1, NULL, NULL) == ESP_OK);
    assert(calls == 3 && !strcmp(index.entries[0].name, "NL"));
    free(index.entries);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "scope.c"
            path.write_text(harness)
            binary = Path(temp) / "scope"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], cwd=temp, check=True)

    @unittest.skipUnless((CJSON / "cJSON.c").exists(), "ESP-IDF cJSON is not installed")
    def test_streamed_json_chunking_limits_and_cancellation(self):
        source = (ROOT / "main/sony_search_index.c").read_text()
        parser = source[source.index("enum { SEARCH_COUNTRY_CAPACITY"):
                        source.index("static esp_err_t load_country(")]
        harness = r"""
#include <assert.h>
#include <ctype.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_FAIL = 1, HTTP_EVENT_ON_DATA = 1 };
typedef struct { int event_id, data_len; void *data, *user_data; } esp_http_client_event_t;
typedef struct { char name[64]; uint8_t uuid[16]; } sony_search_entry_t;
typedef struct { sony_search_entry_t *entries; size_t count; char country[3]; } sony_search_index_t;
""" + parser + r"""
static int feed(search_page_t *p, const char *s, size_t n) {
    esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_DATA,
        .data_len = (int)n, .data = (void *)s, .user_data = p};
    return search_event(&event);
}
int main(void) {
    sony_search_entry_t entries[4] = {0};
    sony_search_index_t index = {.entries = entries};
    char *object = malloc(SEARCH_OBJECT_MAX + 1);
    assert(object);
    atomic_uint epoch = 5;
    search_page_t p = {.index=&index, .capacity=4, .object=object,
                       .epoch=&epoch, .expected_epoch=5};
    const char *json = "[ {\"name\":\"BBC \\\\\\\"{Radio}\\\\\\\"\","
      "\"stationuuid\":\"01234567-89ab-cdef-0123-456789abcdef\","
      "\"nested\":{\"value\":1}},"
      "{\"name\":\"NPO Radio 1\",\"stationuuid\":\"fedcba98-7654-3210-fedc-ba9876543210\"} ]";
    for (size_t i = 0; i < strlen(json); ++i) assert(feed(&p, json + i, 1) == ESP_OK);
    assert(p.state == 5 && p.returned == 2 && index.count == 2);
    char uuid[37]; sony_search_index_uuid(&entries[0], uuid);
    assert(!strcmp(uuid, "01234567-89ab-cdef-0123-456789abcdef"));
    qsort(entries, 2, sizeof(entries[0]), entry_compare);
    size_t first, count;
    sony_search_index_range(&index, "npo", &first, &count);
    assert(count == 1 && !strcmp(entries[first].name, "NPO Radio 1"));
    bool chars[128];
    sony_search_index_chars(&index, "NPO", chars);
    assert(chars[' ']);
    p = (search_page_t){.index=&index,.capacity=4,.object=object,.epoch=&epoch,.expected_epoch=5};
    assert(feed(&p, "[{},]", 5) == ESP_FAIL && p.invalid);
    assert(feed(&p, "[]", 2) == ESP_FAIL && p.invalid);
    p = (search_page_t){.index=&index,.capacity=2,.object=object,.epoch=&epoch,.expected_epoch=5};
    const char *one = "[{\"name\":\"x\",\"stationuuid\":\"01234567-89ab-cdef-0123-456789abcdef\"}]";
    assert(feed(&p, one, strlen(one)) == ESP_FAIL && p.overflow);
    p = (search_page_t){.index=&index,.capacity=4,.object=object,.epoch=&epoch,.expected_epoch=5};
    atomic_store(&epoch, 6);
    assert(feed(&p, "[]", 2) == ESP_FAIL && p.invalid);
    free(object);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "index.c"
            path.write_text(harness)
            binary = Path(temp) / "index"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            "-I", str(CJSON), str(path), str(CJSON / "cJSON.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
