import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RadioPresetTests(unittest.TestCase):
    def test_preset_storage_and_selection(self):
        source = (ROOT / "main/audio/radio.c").read_text()
        functions = source[source.index("static bool valid_url("):
                           source.index("static esp_err_t presets_get_handler(")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define URL_SIZE 512
#define CONFIG_SONY_SCREEN_BENCH
typedef int esp_err_t;
typedef int nvs_handle_t;
enum { ESP_OK = 0, ESP_ERR_INVALID_ARG = 1, ESP_ERR_NVS_NOT_FOUND = 2,
       ESP_ERR_INVALID_RESPONSE = 3, ESP_ERR_NOT_FOUND = 4,
       NVS_READONLY = 0, portMAX_DELAY = 0 };
static unsigned generation, decoding, metadata_interval, radio_rate;
static int radio_channels, selected;
static int lock;
static char preset_name[64], preset_url[URL_SIZE], custom_url[URL_SIZE] = "custom";
static char error_text[20], radio_title[20], stream_name[20], stream_content_type[20];
static char stream_bitrate[20], stream_genre[20], stream_url[URL_SIZE];
static char blobs[16][577];
static size_t lengths[16];
static unsigned announced_number, announced_generation;
static bool announced_assigned;
static void sony_vfd_preset_start(unsigned number, uint32_t gen, bool assigned) {
    announced_number = number; announced_generation = gen;
    announced_assigned = assigned;
}
static int nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    (void)ns; (void)mode; *h = 1; return ESP_OK;
}
static void nvs_close(nvs_handle_t h) { (void)h; }
static int nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size) {
    (void)h;
    unsigned n = 0;
    assert(sscanf(key, "p%u", &n) == 1 && n >= 1 && n <= 16);
    if (!lengths[n - 1]) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < lengths[n - 1]) return ESP_ERR_INVALID_RESPONSE;
    *size = lengths[n - 1]; memcpy(out, blobs[n - 1], *size); return ESP_OK;
}
static void xSemaphoreTake(int l, int t) { (void)l; (void)t; }
static void xSemaphoreGive(int l) { (void)l; }
#define atomic_store(p, v) (*(p) = (v))
#define atomic_load(p) (*(p))
#define atomic_fetch_add(p, v) (*(p) += (v))
""" + functions + r"""
int main(void) {
    radio_preset_t p;
    assert(read_preset(0, &p) == ESP_ERR_INVALID_ARG);
    assert(read_preset(17, &p) == ESP_ERR_INVALID_ARG);
    assert(read_preset(1, &p) == ESP_OK && !p.name[0] && !p.url[0]);
    assert(radio_select_preset(1) == ESP_ERR_NOT_FOUND && generation == 0);
    assert(announced_number == 1 && announced_generation == 0 && !announced_assigned);
    const char first[] = "First\0https://example.com/first";
    const char last[] = "Last\0http://example.com/last";
    memcpy(blobs[0], first, sizeof(first)); lengths[0] = sizeof(first);
    memcpy(blobs[15], last, sizeof(last)); lengths[15] = sizeof(last);
    assert(radio_select_preset(1) == ESP_OK);
    assert(selected == 5 && generation == 1);
    assert(announced_number == 1 && announced_generation == 1 && announced_assigned);
    assert(!strcmp(preset_name, "First") && !strcmp(preset_url, "https://example.com/first"));
    assert(!strcmp(custom_url, "custom"));
    assert(radio_select_preset(16) == ESP_OK && generation == 2);
    assert(announced_number == 16 && announced_generation == 2 && announced_assigned);
    assert(!strcmp(preset_name, "Last") && !strcmp(preset_url, "http://example.com/last"));
    blobs[15][lengths[15] - 1] = 'x';
    assert(radio_select_preset(16) == ESP_ERR_INVALID_RESPONSE && generation == 2);
    assert(valid_url("https://example.com/audio") && !valid_url("https://example.com/#fragment"));
    assert(!valid_url("http://user@example.com") && !valid_url("https://example.com/a b"));
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "presets.c"
            path.write_text(harness)
            binary = Path(temp) / "presets"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
