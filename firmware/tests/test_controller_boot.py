import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ControllerBootTests(unittest.TestCase):
    def test_normal_display_starts_and_failure_does_not_block_control(self):
        source = (ROOT / "main/sony_controller_main.c").read_text()
        app = source[source.index("void app_main(void)"):]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_ERR_NO_MEM = 1, pdPASS = 1 };
typedef struct {
  const char *base_path, *partition_label;
  unsigned max_files;
  bool format_if_mount_failed;
} esp_vfs_spiffs_conf_t;
static int sequence, screen_init_order, display_order, keypad_order, http_order;
static int display_error, log_count;
#define ESP_ERROR_CHECK(call) assert((call) == ESP_OK)
#define ESP_LOGE(tag, fmt, ...) (++log_count)
static int nvs_flash_init(void) { return 0; }
static int settings_init(void) { return 0; }
static int sony_uart_link_init(void) { return 0; }
static int sony_screen_init(void) { screen_init_order = ++sequence; return 0; }
static int radio_init(void) { return 0; }
static int sony_controller_menu_init(void) { return 0; }
static void audio_display_task(void *arg) { (void)arg; }
static int xTaskCreatePinnedToCore(void (*task)(void *), const char *name,
                                   int stack, void *arg, int priority,
                                   void *handle, int core) {
  (void)task; (void)name; (void)stack; (void)arg; (void)priority;
  (void)handle; (void)core; return pdPASS;
}
static int sony_screen_display_button_press(bool held) {
  assert(!held); display_order = ++sequence; return display_error;
}
static int sony_keypad_init(void) { keypad_order = ++sequence; return 0; }
static int esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *config) {
  assert(!config->format_if_mount_failed); return 0;
}
static void wifi_init_apsta(void *a, void *b) { (void)a; (void)b; }
static void start_http(void) { http_order = ++sequence; }
""" + app + r"""
int main(void) {
  app_main();
  assert(screen_init_order < display_order && display_order < keypad_order);
  assert(keypad_order < http_order && log_count == 0);
  sequence = 0; display_error = 1;
  app_main();
  assert(log_count == 1 && display_order < http_order);
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "boot.c"
            path.write_text(harness)
            binary = Path(temp) / "boot"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
