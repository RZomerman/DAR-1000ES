import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class KeypadConfigurationTests(unittest.TestCase):
    def test_matrix_selection_and_failure_cleanup(self):
        source = (ROOT / "main/sony_keypad.c").read_text()
        functions = source[source.index("static esp_err_t disable_scan(void)"):
                           source.index("static void keypad_task(")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK = 0, REG_CFG = 1, REG_KP_ROW = 0x1d,
       REG_KP_COL = 0x1e, REG_KP_COL_HI = 0x1f };
static bool scanning;
static unsigned rows, columns, writes, fail_write;
static uint8_t registers[256];
static int display_button;
static int64_t esp_timer_get_time(void) { return 0; }
static void display_button_reset(int *button, int64_t now) {
    (void)button; (void)now;
}
static esp_err_t read_reg(uint8_t reg, uint8_t *value) {
    *value = registers[reg];
    return ESP_OK;
}
static esp_err_t write_reg(uint8_t reg, uint8_t value) {
    ++writes;
    if (writes == fail_write) return 7;
    registers[reg] = value;
    return ESP_OK;
}
"""
        harness += functions + r"""
int main(void) {
    registers[REG_CFG] = 0x20;
    assert(configure_scan(8, 10) == ESP_OK);
    assert(scanning && rows == 8 && columns == 10);
    assert(registers[REG_KP_ROW] == 255);
    assert(registers[REG_KP_COL] == 255);
    assert(registers[REG_KP_COL_HI] == 3);
    assert(registers[REG_CFG] == 0x29);
    assert(disable_scan() == ESP_OK);
    assert(!scanning && !rows && !columns);
    assert(!registers[REG_KP_ROW] && !registers[REG_KP_COL]
           && !registers[REG_KP_COL_HI]);
    writes = 0; fail_write = 3;
    assert(configure_scan(4, 4) == 7);
    assert(!scanning && !registers[REG_KP_ROW] && !registers[REG_KP_COL]
           && !registers[REG_KP_COL_HI]);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "keypad.c"
            path.write_text(harness)
            binary = Path(temp) / "keypad"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_display_button_debounce_and_ghost_suppression(self):
        source = (ROOT / "main/sony_keypad.c").read_text()
        functions = source[source.index("enum { DISPLAY_KEY_CODE"):
                           source.index("static void bus_delay(void)")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
""" + functions + r"""
int main(void) {
    display_button_t *b = &display_button;
    display_button_reset(b, 0);
    display_button_event(b, 0xba, 10);
    display_button_event(b, 0x3a, 30);
    assert(display_button_poll(b, 229) == -1);
    assert(display_button_poll(b, 230) == -1 && b->armed);
    display_button_event(b, 0xba, 300);
    assert(display_button_poll(b, 360) == -1);
    display_button_event(b, 0x3a, 500);
    assert(display_button_poll(b, 559) == -1);
    assert(display_button_poll(b, 560) == 0);
    assert(display_button_poll(b, 600) == -1);
    display_button_event(b, 0xba, 700);
    assert(display_button_poll(b, 760) == -1);
    assert(display_button_poll(b, 1899) == -1);
    assert(display_button_poll(b, 1900) == 1);
    assert(display_button_poll(b, 1905) == -1);
    display_button_event(b, 0x3a, 1910);
    assert(display_button_poll(b, 1970) == -1);
    display_button_event(b, 0xba, 2000);
    display_button_event(b, 0x3a, 2030);
    assert(display_button_poll(b, 2090) == -1);
    display_button_event(b, 0xb3, 2100);
    display_button_event(b, 0xba, 2100);
    assert(display_button_poll(b, 2160) == -1);
    display_button_event(b, 0xb3 & 0x7f, 2300);
    display_button_event(b, 0x3a, 2300);
    assert(display_button_poll(b, 2360) == -1);
    display_button_event(b, 0xba, 2400);
    assert(display_button_poll(b, 2460) == -1);
    display_button_event(b, 0xb3, 2500);
    display_button_event(b, 0x33, 2550);
    display_button_event(b, 0x3a, 2600);
    display_button_event(b, 0xba, 2620);
    display_button_event(b, 0x3a, 2640);
    assert(display_button_poll(b, 2700) == -1);
    display_button_event(b, 0xba, 2800);
    assert(display_button_poll(b, 2860) == -1);
    display_button_reset(b, 2900);
    display_button_event(b, 0x3a, 3000);
    assert(display_button_poll(b, 3200) == -1);
    static const unsigned codes[] = {5, 6, 7, 8, 1, 2, 3, 9,
                                     15, 16, 17, 18, 11, 12, 13, 19};
    int64_t now = 4000;
    for (unsigned i = 0; i < 16; ++i) {
        display_button_reset(b, now);
        assert(display_button_poll(b, now + 200) == -1);
        display_button_event(b, 0x80 | codes[i], now + 300);
        assert(display_button_poll(b, now + 360) == -1);
        display_button_event(b, codes[i], now + 500);
        assert(display_button_poll(b, now + 560) == (int)i + 2);
        assert(display_button_poll(b, now + 620) == -1);
        now += 1000;
    }
    /* A second key or a full-row ghost prevents selecting any preset. */
    display_button_reset(b, now);
    display_button_poll(b, now + 200);
    for (unsigned code = 1; code <= 9; ++code)
        display_button_event(b, 0x80 | code, now + 300);
    display_button_poll(b, now + 360);
    for (unsigned code = 1; code <= 9; ++code)
        display_button_event(b, code, now + 500);
    assert(display_button_poll(b, now + 560) == -1);
    /* A bounced first key must not block a later, different preset. */
    display_button_event(b, 0x85, now + 700);
    display_button_event(b, 5, now + 710);
    display_button_poll(b, now + 770);
    display_button_event(b, 0x86, now + 800);
    display_button_poll(b, now + 860);
    display_button_event(b, 6, now + 1000);
    assert(display_button_poll(b, now + 1060) == 3);
    /* Reset on overflow/error while held cannot select a preset. */
    display_button_event(b, 0x85, now + 1200);
    display_button_poll(b, now + 1260);
    display_button_reset(b, now + 1300);
    display_button_event(b, 5, now + 1400);
    assert(display_button_poll(b, now + 1600) == -1);
    display_button_reset(b, now + 2000);
    display_button_poll(b, now + 2200);
    display_button_event(b, 0x99, now + 2300);
    display_button_poll(b, now + 2360);
    display_button_event(b, 25, now + 2500);
    assert(display_button_poll(b, now + 2560) == BUTTON_CHECK_ACTION);
    assert(display_button_poll(b, now + 2700) == -1);
    display_button_event(b, 0x99, now + 2800);
    display_button_poll(b, now + 2860);
    display_button_event(b, 0x85, now + 2900);
    display_button_event(b, 5, now + 2950);
    display_button_event(b, 25, now + 3000);
    assert(display_button_poll(b, now + 3060) == -1);
    display_button_reset(b, now + 4000);
    display_button_poll(b, now + 4200);
    display_button_event(b, 0xba, now + 4300);
    display_button_poll(b, now + 4360);
    display_button_event(b, 0x85, now + 4400);
    assert(display_button_poll(b, now + 5500) == -1);
    display_button_event(b, 5, now + 5550);
    display_button_event(b, 58, now + 5600);
    assert(display_button_poll(b, now + 5660) == -1);
    display_button_reset(b, now + 6000);
    display_button_poll(b, now + 6200);
    display_button_event(b, 0xba, now + 6300);
    display_button_poll(b, now + 6360);
    display_button_reset(b, now + 7000);
    assert(display_button_poll(b, now + 7600) == -1);
    display_button_event(b, 58, now + 7700);
    assert(display_button_poll(b, now + 8000) == -1);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "button.c"
            path.write_text(harness)
            binary = Path(temp) / "button"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
