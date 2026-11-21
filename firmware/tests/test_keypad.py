import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class KeypadConfigurationTests(unittest.TestCase):
    def test_bus_recovery_is_bounded_and_leaves_idle_bus_untouched(self):
        source = (ROOT / "main/sony_keypad.c").read_text()
        delay_and_clock = source[source.index("static void bus_delay(void)"):
                                 source.index("static esp_err_t bus_start(void)")]
        recovery = source[source.index("static void bus_stop(void)"):
                          source.index("static esp_err_t bus_send(")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
typedef int esp_err_t;
enum { KEYPAD_SDA = 10, KEYPAD_SCL = 9, ESP_OK = 0, ESP_ERR_TIMEOUT = 1 };
static int scl, sda, pulses, release_after, clock_stuck;
static void esp_rom_delay_us(int us) { (void)us; }
static void gpio_set_level(int pin, int level) {
    if (pin == KEYPAD_SDA) sda = level;
    else { if (!scl && level) ++pulses; scl = level; }
}
static int gpio_get_level(int pin) {
    return pin == KEYPAD_SCL ? scl && !clock_stuck :
           sda && pulses >= release_after;
}
""" + delay_and_clock + recovery + r"""
int main(void) {
    scl = sda = 1; release_after = 0;
    bus_recover();
    assert(pulses == 0);
    release_after = 4;
    bus_recover();
    assert(pulses == 4 && scl && sda);
    pulses = 0; release_after = 100;
    bus_recover();
    assert(pulses == 9 && scl && sda);
    pulses = 0; clock_stuck = 1;
    bus_recover();
    assert(pulses == 1 && sda);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bus_recovery.c"
            path.write_text(harness)
            binary = Path(temp) / "bus_recovery"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_controller_debounce_memory_and_check_holds(self):
        source = (ROOT / "main/sony_keypad.c").read_text()
        decoder = source[source.index("enum { DISPLAY_KEY_CODE"):
                         source.index("static void bus_delay(void)")]
        harness = r"""
#define CONFIG_SONY_CONTROLLER_NODE 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
""" + decoder + r"""
static void press(unsigned code, int64_t down, int64_t up, int expected) {
    display_button_reset(&display_button, 0);
    assert(display_button_poll(&display_button, 200) == -1);
    display_button_event(&display_button, code | 0x80, down);
    assert(display_button_poll(&display_button, down + 60) == -1);
    display_button_event(&display_button, code, up);
    assert(display_button_poll(&display_button, up + 60) == expected);
}
static void press_on_down(unsigned code, int expected) {
    display_button_reset(&display_button, 0);
    assert(display_button_poll(&display_button, 200) == -1);
    display_button_event(&display_button, code | 0x80, 300);
    assert(display_button_poll(&display_button, 319) == -1);
    assert(display_button_poll(&display_button, 320) == expected);
    assert(display_button_poll(&display_button, 1600) == -1);
    display_button_event(&display_button, code, 1700);
    assert(display_button_poll(&display_button, 1720) == -1);
}
int main(void) {
    press_on_down(23, 0x100 | 23);
    press_on_down(22, 0x100 | 22);
    press_on_down(53, 0x100 | 53);
    press_on_down(59, 0x100 | 59);
    press_on_down(33, 0x100 | 33);
    press_on_down(52, 0x100 | 52);
    press_on_down(27, 0x100 | 27);
    press_on_down(5, 2);
    press_on_down(29, BUTTON_MONO_ACTION);
    press(51, 300, 450, 0x100 | 51);
    press(51, 300, 1550, 0x200 | 51);
    press(25, 300, 450, 0x100 | 25);
    press(25, 300, 2350, 0x200 | 25);
    press(26, 300, 450, 0x100 | 26);
    press(26, 300, 1550, 0x200 | 26);
    press(58, 300, 450, 0);
    press(58, 300, 1550, 1);
    display_button_reset(&display_button, 0);
    assert(display_button_poll(&display_button, 200) == -1);
    display_button_event(&display_button, 51 | 0x80, 300);
    assert(display_button_poll(&display_button, 360) == -1);
    assert(display_button_poll(&display_button, 1500) == (0x200 | 51));
    assert(display_button_poll(&display_button, 1700) == -1);
    display_button_event(&display_button, 51, 1800);
    assert(display_button_poll(&display_button, 1860) == -1);
    display_button_event(&display_button, 26 | 0x80, 1900);
    assert(display_button_poll(&display_button, 1960) == -1);
    assert(display_button_poll(&display_button, 3100) == (0x200 | 26));
    display_button_event(&display_button, 26, 3200);
    assert(display_button_poll(&display_button, 3260) == -1);
    display_button_event(&display_button, 23 | 0x80, 3300);
    display_button_event(&display_button, 22 | 0x80, 3320);
    assert(display_button_poll(&display_button, 3380) == -1);
    display_button_event(&display_button, 23, 3400);
    display_button_event(&display_button, 22, 3410);
    assert(display_button_poll(&display_button, 3470) == -1);
    display_button_reset(&display_button, 0);
    assert(display_button_poll(&display_button, 200) == -1);
    display_button_event(&display_button, 53 | 0x80, 300);
    assert(display_button_poll(&display_button, 310) == -1);
    display_button_event(&display_button, 53, 340);
    assert(display_button_poll(&display_button, 359) == -1);
    assert(display_button_poll(&display_button, 360) == (0x100 | 53));
    display_button_event(&display_button, 53 | 0x80, 500);
    display_button_event(&display_button, 53, 510);
    assert(display_button_poll(&display_button, 530) == -1);
    display_button_event(&display_button, 53 | 0x80, 550);
    display_button_event(&display_button, 53, 550);
    assert(display_button_poll(&display_button, 570) == -1);
    display_button_event(&display_button, 53 | 0x80, 600);
    display_button_event(&display_button, 53, 630);
    assert(display_button_poll(&display_button, 650) == (0x100 | 53));
    display_button_event(&display_button, 53 | 0x80, 660);
    display_button_event(&display_button, 53, 690);
    assert(display_button_poll(&display_button, 710) == (0x100 | 53));
    display_button_event(&display_button, 53 | 0x80, 800);
    display_button_event(&display_button, 22 | 0x80, 810);
    display_button_event(&display_button, 53, 840);
    display_button_event(&display_button, 22, 850);
    assert(display_button_poll(&display_button, 870) == -1);
    display_button_event(&display_button, 26 | 0x80, 1000);
    display_button_event(&display_button, 26, 2250);
    assert(display_button_poll(&display_button, 2270) == (0x200 | 26));
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "controller_keys.c"
            path.write_text(harness)
            binary = Path(temp) / "controller_keys"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_virtual_and_physical_events_share_actions_and_lost_lease_cancels(self):
        source = (ROOT / "main/sony_keypad.c").read_text()
        decoder = source[source.index("enum { DISPLAY_KEY_CODE"):
                         source.index("static void bus_delay(void)")]
        dispatch = source[source.index("static void record_key("):
                          source.index("static esp_err_t disable_scan(void)")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
enum { EVENT_CAPACITY = 32 };
typedef struct {
    uint32_t sequence;
    uint8_t raw;
    int64_t time_ms;
    bool virtual_input;
} key_event_t;
static key_event_t events[EVENT_CAPACITY];
static uint32_t sequence;
static bool virtual_enabled;
static int64_t virtual_seen_ms;
static int actions[16], count, stops;
static int sony_screen_start_from_check(void) { actions[count++] = 18; return 0; }
static int sony_screen_startup_press(void) { actions[count++] = 19; return 0; }
static int radio_select_preset(unsigned number) { actions[count++] = number + 1; return 0; }
static int sony_screen_display_button_press(bool long_press) {
    actions[count++] = long_press; return 0;
}
static void sony_screen_stop(void) { ++stops; }
""" + decoder + dispatch + r"""
static void tick(bool virtual_input, int64_t now) {
    if (virtual_input) poll_virtual(now);
    else dispatch_button(display_button_poll(&display_button, now));
}
int main(void) {
    const unsigned measured_codes[] = {23, 22, 26, 27};
    const char *measured_names[] = {"FINE -", "FINE +", "DIRECT", "LINK"};
    for (unsigned n = 0; n < 4; ++n) {
        bool found = false;
        for (unsigned i = 0; i < sizeof(key_bindings) / sizeof(key_bindings[0]); ++i)
            if (!strcmp(key_bindings[i].name, measured_names[n]) &&
                key_bindings[i].code == measured_codes[n])
                found = true;
        assert(found);
    }
    for (unsigned source = 0; source < 2; ++source) {
        count = 0;
        virtual_enabled = source;
        virtual_seen_ms = 0;
        display_button_reset(&display_button, 0);
        tick(source, 200);
        record_key(0xba, 300, source, true);
        tick(source, 360);
        tick(source, 1500);
        record_key(0x3a, 1600, source, true);
        tick(source, 1660);
        assert(count == 1 && actions[0] == 1);
        record_key(0x99, 2500, source, true);
        tick(source, 2560);
        record_key(0x19, 2600, source, true);
        tick(source, 2660);
        assert(count == 2 && actions[1] == 18);
        record_key(0x89, 3500, source, true);
        tick(source, 3560);
        record_key(0x09, 3700, source, true);
        tick(source, 3760);
        assert(count == 3 && actions[2] == 9);
        record_key(0xa3, 4000, source, true);
        tick(source, 4060);
        record_key(0x23, 4200, source, true);
        tick(source, 4260);
        assert(count == 3);
        record_key(0x9d, 4400, source, true);
        tick(source, 4460);
        record_key(0x1d, 4600, source, true);
        tick(source, 4660);
        assert(count == 4 && actions[3] == 19);
        assert(events[(sequence - 1) % EVENT_CAPACITY].virtual_input == (bool)source);
    }
    record_key(0xba, 5000, true, true);
    virtual_seen_ms = 5000;
    virtual_enabled = true;
    poll_virtual(11001);
    assert(!virtual_enabled && stops == 1 && count == 4);
    assert(!display_button.down[0] && !display_button.down[1]);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "virtual.c"
            path.write_text(harness)
            binary = Path(temp) / "virtual"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

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
