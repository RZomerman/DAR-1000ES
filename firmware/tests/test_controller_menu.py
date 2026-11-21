import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ControllerMenuTests(unittest.TestCase):
    def test_directory_cache_expiry_keys_and_failure(self):
        source = (ROOT / "main/sony_controller_menu.c").read_text()
        types = source[source.index("enum { MAX_COUNTRIES"):
                       source.index("typedef struct {\n  unsigned code")]
        cache = source[source.index("enum { DIRECTORY_CACHE_SIZE"):
                       source.index("static void index_progress(")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sony_station_wire.h"
enum { MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2 };
static int64_t now;
static unsigned requests;
static bool fail;
static int64_t esp_timer_get_time(void) { return now; }
static void *heap_caps_malloc(size_t size, unsigned caps) {
    (void)caps; return malloc(size);
}
""" + types + r"""
enum { JOB_COUNTRIES = 1, JOB_TAGS, JOB_TOP, JOB_BROWSE };
static result_t *directory_uncached(const job_t *job) {
    ++requests;
    result_t *result = calloc(1, sizeof(*result));
    assert(result);
    result->type = job->type;
    result->generation = job->generation;
    result->ok = !fail;
    result->count = requests;
    return result;
}
""" + cache + r"""
int main(void) {
    job_t job = {.type = JOB_COUNTRIES, .generation = 1};
    strcpy(job.country, "NL");
    result_t *r = directory(&job);
    assert(r && r->count == 1); r->count = 999; free(r);
    strcpy(job.country, "ES"); job.generation = 2;
    r = directory(&job);
    assert(r && requests == 1 && r->generation == 2 && r->count == 1);
    free(r);
    job.type = JOB_TOP; strcpy(job.country, "NL"); strcpy(job.tag, "pop");
    r = directory(&job); free(r);
    r = directory(&job); assert(requests == 2 && r->count == 2); free(r);
    strcpy(job.country, "ES");
    r = directory(&job); assert(requests == 3); free(r);
    strcpy(job.tag, "rock");
    fail = true;
    r = directory(&job); assert(!r->ok); free(r);
    r = directory(&job); assert(!r->ok && requests == 5); free(r);
    fail = false;
    r = directory(&job); assert(r->ok && requests == 6); free(r);
    now += 600000001LL;
    r = directory(&job); assert(requests == 7); free(r);
    job.type = JOB_COUNTRIES;
    r = directory(&job); assert(requests == 7 && r->count == 1); free(r);
    now += 86400000000LL;
    r = directory(&job); assert(requests == 8); free(r);
    for (unsigned i = 0; i < DIRECTORY_CACHE_SIZE; ++i)
        free(directory_cache[i].result);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cache.c"
            path.write_text(harness)
            binary = Path(temp) / "cache"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            "-I", str(ROOT / "main"), str(path), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)

    def test_navigation_storage_and_stale_results(self):
        source = (ROOT / "main/sony_controller_menu.c").read_text()
        state = source[source.index("enum { MAX_COUNTRIES"):
                       source.index("typedef struct { char *data")]
        start = source[source.index("static void start_job("):
                       source.index("static void display(void)")]
        handlers = source[source.index("static void display(void)"):
                          source.index("static void menu_task(")]
        search_source = (ROOT / "main/sony_search_index.c").read_text()
        search_helpers = search_source[search_source.index("void sony_search_index_uuid("):
                                       search_source.index("static bool append_object(")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "sony_station_wire.h"
#define SONY_SEARCH_DEFAULT_COUNTRY "NL"
typedef struct { char name[64]; uint8_t uuid[16]; } sony_search_entry_t;
typedef struct {
    sony_search_entry_t *entries;
    size_t count;
    char country[3];
} sony_search_index_t;
""" + search_helpers + r"""
typedef int QueueHandle_t;
enum { ESP_OK = 0 };
static char shown[128];
static int tunes, recalls;
static int64_t now;
static bool browse_light, memory_light;
static bool playing;
static bool sony_uart_link_current_station(sony_station_command_t *station) {
    if (!playing) return false;
    *station = (sony_station_command_t){.generation = 5, .name = "On Air",
                                       .url = "https://example.test/stream"};
    return true;
}
static void sony_screen_menu_indicators(bool browsing, bool storing) {
    browse_light = browsing;
    memory_light = storing;
}
static void sony_screen_menu_text(const char *s) {
    snprintf(shown, sizeof(shown), "%s", s ? s : "");
}
static int64_t esp_timer_get_time(void) { return now; }
static int sony_controller_select_station(const sony_station_command_t *s) {
    assert(!strcmp(s->name, "SLAM")); ++tunes; return ESP_OK;
}
static int radio_select_preset(unsigned n) { recalls = n; return ESP_OK; }
static int xQueueOverwrite(int queue, const void *item);
""" + state + r"""
static job_t queued;
static int xQueueOverwrite(int queue, const void *item) {
    (void)queue; queued = *(const job_t *)item; return 1;
}
""" + start + handlers + r"""
static result_t *reply(unsigned type, unsigned gen) {
    result_t *r = calloc(1, sizeof(*r));
    assert(r); r->type = type; r->generation = gen; r->ok = true; return r;
}
int main(void) {
    handle_key(51, true);
    assert(screen == IDLE && !memory_light && !strcmp(shown, "NO RADIO STATION"));
    playing = true;
    handle_key(51, true);
    assert(screen == STORE && memory_light && !strcmp(snapshot.name, "On Air"));
    handle_key(5, false);
    assert(screen == CHECK_SLOT && memory_light && queued.slot == 1);
    result_t *slot_result = reply(JOB_CHECK_SLOT, request_generation);
    slot_result->slot = 1;
    handle_result(slot_result); free(slot_result);
    assert(screen == SAVING && !memory_light && queued.slot == 1 &&
           !strcmp(queued.station.url, "https://example.test/stream"));
    slot_result = reply(JOB_SAVE, request_generation);
    handle_result(slot_result); free(slot_result);
    assert(screen == IDLE && !memory_light && !strcmp(shown, "STORED"));
    handle_key(27, false);
    assert(screen == LOADING_BROWSE && browse_light && !memory_light);
    handle_key(52, false);
    assert(screen == IDLE && !browse_light && !memory_light);
    (void)events;
    handle_key(49, false);
    assert(screen == LOADING_COUNTRIES && queued.type == JOB_COUNTRIES && !tunes);
    result_t *r = reply(JOB_COUNTRIES, request_generation);
    r->count = 2;
    strcpy(r->data.countries[0].name, "Netherlands"); strcpy(r->data.countries[0].code, "NL");
    strcpy(r->data.countries[1].name, "Spain"); strcpy(r->data.countries[1].code, "ES");
    handle_result(r); free(r);
    assert(screen == COUNTRIES && !applied_country[0]);
    handle_key(22, false); handle_key(51, false);
    assert(screen == LOADING_TAGS && !strcmp(applied_country, "NL"));
    r = reply(JOB_TAGS, request_generation); r->count = 1;
    strcpy(r->data.tags[0].name, "rock");
    handle_result(r); free(r);
    assert(screen == TAGS && !strcmp(shown, "ALL GENRES"));
    handle_key(51, false);
    assert(screen == LOADING_BROWSE && queued.type == JOB_BROWSE && !queued.tag[0]);
    unsigned stale = request_generation;
    handle_key(49, false);
    r = reply(JOB_BROWSE, stale); r->count = 1;
    handle_result(r); free(r);
    assert(screen == LOADING_COUNTRIES && !tunes);
    r = reply(JOB_COUNTRIES, request_generation); r->count = 2;
    strcpy(r->data.countries[0].name, "Netherlands"); strcpy(r->data.countries[0].code, "NL");
    strcpy(r->data.countries[1].name, "Spain"); strcpy(r->data.countries[1].code, "ES");
    handle_result(r); free(r);
    handle_key(22, false);
    assert(!strcmp(applied_country, "NL"));
    handle_key(52, false);
    assert(!strcmp(applied_country, "NL") && !tunes);
    handle_key(46, false);
    assert(screen == LOADING_TOP && !strcmp(queued.tag, "pop") &&
           !strcmp(queued.country, "NL"));
    r = reply(JOB_TOP, request_generation); r->count = 1;
    strcpy(r->data.stations[0].name, "SLAM");
    strcpy(r->data.stations[0].url, "https://example.com/stream");
    r->data.stations[0].generation = 1;
    handle_result(r); free(r);
    assert(screen == TOP && !tunes);
    handle_key(51, true);
    assert(screen == STORE && !strcmp(snapshot.name, "SLAM"));
    handle_key(46, false);
    assert(screen == STORE && queued.type == JOB_TOP);
    handle_key(6, false);
    assert(screen == CHECK_SLOT && queued.type == JOB_CHECK_SLOT && queued.slot == 2);
    r = reply(JOB_CHECK_SLOT, request_generation); r->slot = 2; r->exists = true;
    handle_result(r); free(r);
    assert(screen == REPLACE);
    handle_key(49, false);
    assert(screen == REPLACE && queued.type == JOB_CHECK_SLOT);
    handle_key(52, false);
    assert(screen == TOP && !tunes && !recalls);
    handle_key(51, true); handle_key(6, false);
    r = reply(JOB_CHECK_SLOT, request_generation); r->slot = 2; r->exists = true;
    handle_result(r); free(r);
    handle_key(51, false);
    assert(screen == SAVING && queued.type == JOB_SAVE &&
           queued.slot == 2 && !strcmp(queued.station.name, "SLAM") && !tunes);
    r = reply(JOB_SAVE, request_generation); handle_result(r); free(r);
    assert(screen == TOP && !strcmp(shown, "STORED") && !tunes);
    handle_key(51, false);
    assert(screen == IDLE && tunes == 1 && !strcmp(shown, "AUDIO UNCONFIRMED"));
    handle_key(5, false);
    assert(recalls == 1);
    screen = TOP; current_index = 1; station_count = 2;
    strcpy(stations[1].name, "SLAM"); stations[1].generation = 1;
    strcpy(stations[1].url, "https://example.com/stream");
    handle_key(51, true); handle_key(6, false);
    r = reply(JOB_CHECK_SLOT, request_generation); r->slot = 2;
    handle_result(r); free(r);
    r = reply(JOB_SAVE, request_generation); handle_result(r); free(r);
    assert(screen == TOP && current_index == 1);
    partial = true; display();
    assert(shown[0] == '~');
    handle_key(25, false);
    assert(screen == CHECKING && check_return == TOP);
    r = reply(JOB_CHECK, request_generation); r->wifi_ok = true;
    handle_result(r); free(r);
    assert(screen == TOP && current_index == 1);
    screen = LOADING_BROWSE;
    handle_key(49, false);
    handle_key(52, false);
    assert(screen == IDLE);
    screen = BROWSE; partial = true; prefix[0] = 0; active_letter = 0;
    station_count = 1; strcpy(stations[0].name, "SLAM");
    find_candidate(1); display();
    assert(candidate == 0 && shown[0] == '~' && tunes == 1);
    handle_key(51, false);
    assert(!strcmp(prefix, "S") && active_letter == 11 && tunes == 1);
    sony_search_entry_t entries[] = {
        {.name = "Alpha"}, {.name = "bbc radio 1"}, {.name = "BBC Radio 2"},
        {.name = "Zulu"}, {.name = "1 Station"}, {.name = "!Bang"}
    };
    for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
        memset(entries[i].uuid, (int)i + 1, sizeof(entries[i].uuid));
    qsort(entries, sizeof(entries) / sizeof(entries[0]), sizeof(entries[0]), entry_compare);
    search_index.entries = entries; search_index.count = sizeof(entries) / sizeof(entries[0]);
    screen = IDLE; handle_key(26, false);
    assert(screen == LOADING_INDEX && queued.type == JOB_SEARCH_INDEX);
    r = reply(JOB_SEARCH_INDEX, request_generation); handle_result(r); free(r);
    assert(screen == SEARCH_EDIT && search_alphabet[search_cursor] == 'A');
    now = 0; display(); assert(!strcmp(shown, "A"));
    now = 500000; display(); assert(!strcmp(shown, ""));
    handle_key(22, false);
    assert(search_alphabet[search_cursor] == 'B');
    handle_key(51, false);
    assert(!strcmp(prefix, "B") && search_alphabet[search_cursor] == 'B');
    handle_key(51, false);
    assert(!strcmp(prefix, "BB") && search_alphabet[search_cursor] == 'C');
    handle_key(51, false);
    assert(!strcmp(prefix, "BBC") && search_alphabet[search_cursor] == ' ');
    handle_key(27, false);
    assert(screen == SEARCH_STATIONS && search_count == 2 && tunes == 1);
    assert(!strcasecmp(search_index.entries[search_first + search_position].name, "bbc radio 1"));
    handle_key(22, false);
    assert(!strcasecmp(search_index.entries[search_first + search_position].name, "bbc radio 2"));
    handle_key(26, true);
    assert(screen == LOADING_RESOLVE && queued.type == JOB_SEARCH_RESOLVE && !queued.slot);
    r = reply(JOB_SEARCH_RESOLVE, request_generation); r->count = 1;
    strcpy(r->data.stations[0].name, "SLAM");
    handle_result(r); free(r);
    assert(screen == IDLE && tunes == 2);
    handle_key(26, false);
    unsigned stale_search = request_generation;
    unsigned old_epoch = atomic_load(&search_epoch);
    handle_key(52, false);
    assert(screen == IDLE && atomic_load(&search_epoch) != old_epoch);
    r = reply(JOB_SEARCH_INDEX, stale_search); handle_result(r); free(r);
    assert(screen == IDLE);
    screen = SEARCH_EDIT; prefix[0] = 0; search_refresh_chars(0);
    handle_key(23, false); assert(search_alphabet[search_cursor] == '!');
    handle_key(23, false); assert(search_alphabet[search_cursor] == '1');
    handle_key(23, false); assert(search_alphabet[search_cursor] == 'Z');
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "menu.c"
            path.write_text(harness)
            binary = Path(temp) / "menu"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            "-I", str(ROOT / "main"), str(path), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)
