import gzip
import importlib.util
import json
from pathlib import Path
import struct
import unittest
import ctypes
import re
import subprocess
import tempfile
from bisect import bisect_left


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "prepare_screen_replay", ROOT / "scripts/prepare_screen_replay.py"
)
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def decode(encoded):
    samples, frames = struct.unpack_from("<II", encoded, 4)
    payload = b"".join(bytes([state]) * count
                       for count, state in struct.iter_unpack("<HB", encoded[17:]))
    if len(payload) != samples:
        raise ValueError("Run lengths do not match sample count")
    return payload, frames


class TextState(ctypes.Structure):
    _fields_ = [("glyphs", ctypes.c_uint64 * 256), ("length", ctypes.c_size_t),
                ("started_us", ctypes.c_int64), ("active", ctypes.c_bool),
                ("scroll", ctypes.c_bool)]


class ScreenPinProfileTests(unittest.TestCase):
    def test_idle_only_switch_releases_old_pins_and_rolls_back_failure(self):
        source = (ROOT / "main/sony_screen.c").read_text()
        profiles = source[source.index("static const sony_pin_profile_t pin_profiles[]"):
                          source.index("static const uint64_t frequency[]")]
        switch = source[source.index("static esp_err_t release_profile("):
                        source.index("esp_err_t sony_screen_init(")]
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "driver").mkdir()
            gpio = """
#pragma once
#include <stdint.h>
typedef int gpio_num_t;
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en; } gpio_config_t;
enum { GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE };
"""
            for pin in (1, 2, 4, 5, 6, 7, 15, 16, 21, 38, 39, 40):
                gpio += f"#define GPIO_NUM_{pin} {pin}\n"
            (directory / "driver/gpio.h").write_text(gpio)
            harness = r"""
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include "sony_pins.h"
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE };
static atomic_bool running;
static int output[49], level[49], writes, fail_new;
"""
            harness += profiles + r"""
static esp_err_t hold_idle_outputs(void) {
    ++writes;
    for (unsigned i = 0; i < 6; ++i) {
        int pin = sony_pins_get()->pins[i];
        output[pin] = 1;
        level[pin] = i == 1 || i == 5;
        if (fail_new && !strcmp(sony_pins_get()->id, "ahct-v2") && i == 3) {
            fail_new = 0;
            return 7;
        }
    }
    return ESP_OK;
}
static void restore_idle_outputs(void) { assert(hold_idle_outputs() == ESP_OK); }
static esp_err_t gpio_config(const gpio_config_t *config) {
    ++writes;
    assert(config->mode == GPIO_MODE_INPUT);
    assert(config->pull_up_en == GPIO_PULLUP_DISABLE);
    assert(config->pull_down_en == GPIO_PULLDOWN_DISABLE);
    for (int i = 0; i < 49; ++i)
        if (config->pin_bit_mask & (1ULL << i)) output[i] = 0;
    return ESP_OK;
}
"""
            harness += switch + r"""
int main(void) {
    const int old[] = {4,5,6,15,16,7}, next[] = {2,1,40,38,21,39};
    assert(!strcmp(sony_pins_get()->id, "ahct-v2"));
    assert(!atomic_load(&running));
    assert(hold_idle_outputs() == ESP_OK);
    for (int i = 0; i < 6; ++i) {
        assert(!output[old[i]] && output[next[i]]);
        assert(level[next[i]] == (i == 1 || i == 5));
    }
    assert(select_pin_profile("original") == ESP_OK);
    int before = writes;
    assert(select_pin_profile("missing") == ESP_ERR_INVALID_ARG);
    atomic_store(&running, true);
    assert(select_pin_profile("ahct-v2") == ESP_ERR_INVALID_STATE);
    assert(writes == before && !strcmp(sony_pins_get()->id, "original"));
    atomic_store(&running, false);
    fail_new = 1;
    assert(select_pin_profile("ahct-v2") == 7);
    assert(!strcmp(sony_pins_get()->id, "original"));
    for (int i = 0; i < 6; ++i)
        assert(output[old[i]] && !output[next[i]]);
    assert(select_pin_profile("ahct-v2") == ESP_OK);
    assert(!atomic_load(&running));
    assert(SONY_GRID_DATA == 2 && SONY_GRID_CLOCK == 1 && SONY_CLEAR == 40);
    assert(SONY_STROBE == 38 && SONY_SEGMENT_DATA == 21 && SONY_SEGMENT_CLOCK == 39);
    for (int i = 0; i < 6; ++i) {
        assert(!output[old[i]] && output[next[i]]);
        assert(level[next[i]] == (i == 1 || i == 5));
    }
    before = writes;
    assert(select_pin_profile("ahct-v2") == ESP_OK && writes == before);
    assert(select_pin_profile("original") == ESP_OK);
    for (int i = 0; i < 6; ++i) {
        assert(output[old[i]] && !output[next[i]]);
        assert(level[old[i]] == (i == 1 || i == 5));
    }
    return 0;
}
"""
            path = directory / "pins.c"
            path.write_text(harness)
            binary = directory / "pins"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            "-I", str(directory), "-I", str(ROOT / "main"),
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class ScreenTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "text.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "main/sony_text.c"), "-o", str(library)], check=True)
        cls.library = ctypes.CDLL(str(library))
        cls.parse = cls.library.sony_text_parse
        cls.parse.argtypes = [ctypes.POINTER(TextState), ctypes.c_char_p, ctypes.c_int64]
        cls.parse.restype = ctypes.c_bool
        cls.frame = cls.library.sony_text_frame
        cls.frame.argtypes = [ctypes.POINTER(TextState), ctypes.c_int64,
                             ctypes.POINTER(ctypes.c_uint64)]
        cls.frame_width = cls.library.sony_text_frame_width
        cls.frame_width.argtypes = cls.frame.argtypes + [ctypes.c_uint]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def text(self, value):
        text = TextState()
        self.assertTrue(self.parse(ctypes.byref(text), value.encode("utf8"), 500))
        return text

    def window(self, text, elapsed):
        words = (ctypes.c_uint64 * 26)(*range(26))
        self.frame(ctypes.byref(text), 500 + elapsed, words)
        for slot in list(range(14)) + [23, 24, 25]:
            self.assertEqual(words[slot], slot, "Scrolling must preserve other fields")
        return list(words)[14:23][::-1]

    def test_static_short_empty_and_scroll_disabled(self):
        for value in ("", "No DSR", "123456789"):
            text = self.text(value)
            expected = list(text.glyphs)[:text.length] + [0] * (9 - text.length)
            self.assertEqual(self.window(text, 0), expected)
            self.assertEqual(self.window(text, 100000000), expected)
        text = self.text("Now Playing: blabla")
        text.scroll = False
        self.assertEqual(self.window(text, 9999999), list(text.glyphs)[:9])

    def test_pause_step_four_space_gap_and_loop(self):
        text = self.text("ABCDEFGHIJKL")
        glyphs = list(text.glyphs)[:12]
        self.assertEqual(self.window(text, 0), glyphs[:9])
        self.assertEqual(self.window(text, 999999), glyphs[:9])
        self.assertEqual(self.window(text, 1000000), glyphs[1:10])
        self.assertEqual(self.window(text, 1299999), glyphs[1:10])
        self.assertEqual(self.window(text, 1300000), glyphs[2:11])
        self.assertEqual(self.window(text, 4300000), [0] * 4 + glyphs[:5])
        self.assertEqual(self.window(text, 5500000), glyphs[:9])
        self.assertEqual(self.window(text, 6499999), glyphs[:9])
        self.assertEqual(self.window(text, 6500000), glyphs[1:10])

    def test_full_width_radio_scroll_preserves_fixed_indicators(self):
        text = self.text("ABCDEFGHIJKLMNOPQRSTUVXYZ")
        words = (ctypes.c_uint64 * 26)(*range(26))
        self.frame_width(ctypes.byref(text), 500, words, 22)
        self.assertEqual(list(words)[1:23][::-1], list(text.glyphs)[:22])
        for slot in (0, 23, 24, 25):
            self.assertEqual(words[slot], slot)
        self.frame_width(ctypes.byref(text), 1000500, words, 22)
        self.assertEqual(list(words)[1:23][::-1], list(text.glyphs)[1:23])
        short = self.text("Short song")
        self.frame_width(ctypes.byref(short), 9999999, words, 22)
        self.assertEqual(list(words)[1:23][::-1],
                         list(short.glyphs)[:short.length] + [0]*(22-short.length))
        before = list(words)
        for width in (0, 23):
            self.frame_width(ctypes.byref(short), 500, words, width)
            self.assertEqual(list(words), before)

    def test_unicode_mapping_and_grouped_fallback(self):
        composed = self.text("Björk é ♪ 漢 👩‍🎤 🇳🇱")
        decomposed = self.text("Bjo\u0308rk e\u0301 ♪ 漢 👩‍🎤 🇳🇱")
        self.assertEqual(composed.length, decomposed.length)
        self.assertEqual(list(composed.glyphs), list(decomposed.glyphs))
        star = self.text("*").glyphs[0]
        self.assertEqual(list(composed.glyphs)[:composed.length][-5:],
                         [star, 0, star, 0, star])
        self.assertEqual(self.text("♪").glyphs[0], 0x00CE6A5184)
        self.assertEqual(self.text("ḿ").glyphs[0], self.text("m").glyphs[0])
        self.assertEqual(self.text("H\u0301").glyphs[0], 0x04631FC631)
        self.assertEqual(self.text("♥\ufe0f").length, 1)

    def test_native_font_matches_editor_catalogue(self):
        records = json.loads(subprocess.check_output([
            "node", "-e",
            "const d=require('./data/www/display-editor.js');"
            "const chars=new Set([...Object.keys(d.unicodeFont),"
            "...'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-?!:/+']);"
            "console.log(JSON.stringify([...chars].map(c=>[c,d.hex(d.glyph(c))])));"
        ], cwd=ROOT, text=True))
        for character, word in records:
            with self.subTest(character=character):
                text = self.text(character)
                self.assertEqual(text.length, 1)
                self.assertEqual(text.glyphs[0], int(word, 16))

    def test_invalid_utf8_and_capacity(self):
        for invalid in (b"\xc0\xaf", b"\xed\xa0\x80", b"\xf4\x90\x80\x80",
                        b"\xe2\x82", b"\x80", b"a" * 257,
                        ("a" + "\u0301" * 600).encode("utf8")):
            text = TextState()
            self.assertFalse(self.parse(ctypes.byref(text), invalid, 0))
        self.assertEqual(self.text("a" * 256).length, 256)


class ScreenTimedCacheTests(unittest.TestCase):
    def test_completed_buffers_reuse_only_identical_continuations(self):
        source = (ROOT / "main/sony_capture_replay.c").read_text()
        model = source[source.index("typedef struct {"):
                       source.index("extern const uint8_t replay_start")]
        submit = source[source.index("esp_err_t sony_scan_submit_timed("):
                        source.index("esp_err_t sony_scan_submit(")]
        harness = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "sony_timing.h"
typedef int esp_err_t;
typedef void *esp_lcd_i80_bus_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef struct sony_scan_engine sony_scan_engine_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_SIZE };
"""
        harness += model + r"""
static unsigned generated;
static size_t last_size;
static uint8_t *last_wave;
static esp_err_t wait_for_buffer(sony_scan_engine_t *engine) {
    (void)engine; return ESP_OK;
}
static esp_err_t held_preload(sony_scan_engine_t *engine, const uint64_t words[26],
                              const sony_timing_t *timing) {
    (void)engine; (void)words; (void)timing; return ESP_OK;
}
static esp_err_t esp_lcd_panel_io_tx_color(void *io, int command, uint8_t *wave,
                                          size_t bytes) {
    (void)io; (void)command; last_size = bytes; last_wave = wave; return ESP_OK;
}
static size_t counted_generate(uint8_t *wave, size_t capacity,
                               const sony_timing_t *timing,
                               const uint64_t words[26], bool initialize) {
    ++generated;
    return sony_timing_generate(wave, capacity, timing, words, initialize);
}
#define sony_timing_generate counted_generate
"""
        harness += submit + r"""
#undef sony_timing_generate
static void check_patches(void) {
    uint8_t *wave = malloc(SONY_TIMING_CAPACITY);
    uint8_t *expected = malloc(SONY_TIMING_CAPACITY);
    assert(wave && expected);
    uint64_t before[26] = {0}, after[26];
    for (unsigned i = 0; i < 26; ++i) after[i] = 0xFFFFFFFFFFULL ^ (1ULL << i);
    for (unsigned pair = 0; pair < 2; ++pair) {
        sony_timing_t timing = sony_timing_defaults();
        if (pair) {
            timing.pair = 1; timing.slot = 22;
            timing.setup_ns = timing.clock_low_ns = timing.clock_high_ns = 1000;
            timing.on_ns = 30000; timing.off_ns = 130000;
        }
        for (unsigned initialize = 0; initialize < 2; ++initialize) {
            size_t bytes = sony_timing_generate(wave, SONY_TIMING_CAPACITY,
                                                &timing, before, initialize);
            assert(bytes);
            assert(sony_timing_generate(expected, SONY_TIMING_CAPACITY,
                                        &timing, after, initialize) == bytes);
            assert(!sony_timing_patch_data(wave, bytes - 1, &timing,
                                           before, after, initialize));
            assert(sony_timing_patch_data(wave, bytes, &timing,
                                          before, after, initialize));
            assert(!memcmp(wave, expected, bytes));
            assert(sony_timing_patch_data(wave, bytes, &timing,
                                          after, before, initialize));
            assert(sony_timing_generate(expected, SONY_TIMING_CAPACITY,
                                        &timing, before, initialize) == bytes);
            assert(!memcmp(wave, expected, bytes));
        }
    }
    free(wave); free(expected);
}
static void check(sony_scan_engine_t *engine, uint64_t words[26],
                   sony_timing_t *timing, bool initialize) {
    assert(sony_scan_submit_timed(engine, words, timing) == ESP_OK);
    uint8_t *expected = malloc(SONY_TIMING_CAPACITY);
    size_t size = engine->held ?
        sony_timing_generate_clear(expected, SONY_TIMING_CAPACITY, timing) :
        sony_timing_generate(expected, SONY_TIMING_CAPACITY, timing, words, initialize);
    assert(size && last_size == size);
    if (!engine->held)
        for (size_t i = 0; i < size; ++i) expected[i] ^= 0x21;
    assert(!memcmp(expected, last_wave, size));
    free(expected);
}
int main(void) {
    check_patches();
    sony_scan_engine_t engine = {.timed = true, .bytes = SONY_TIMING_CAPACITY};
    for (unsigned i = 0; i < 2; ++i) {
        engine.buffers[i] = malloc(engine.bytes);
        assert(engine.buffers[i]);
    }
    sony_timing_t timing = sony_timing_defaults();
    uint64_t words[26] = {0};
    words[22] = 0x010842109F;
    check(&engine, words, &timing, true);
    for (unsigned i = 0; i < 4; ++i) check(&engine, words, &timing, false);
    assert(generated == 3);
    words[25] = 1;
    check(&engine, words, &timing, false);
    check(&engine, words, &timing, false);
    check(&engine, words, &timing, false);
    assert(generated == 3);
    for (unsigned frame = 0; frame < 32; ++frame) {
        for (unsigned slot = 0; slot < 26; ++slot)
            words[slot] = (0x123456789AULL * (frame + 1)) ^ (1ULL << slot);
        check(&engine, words, &timing, false);
        check(&engine, words, &timing, false);
    }
    assert(generated == 3);
    timing.on_ns += 125;
    timing.off_ns -= 125;
    check(&engine, words, &timing, true);
    check(&engine, words, &timing, false);
    check(&engine, words, &timing, false);
    unsigned before = generated;
    check(&engine, words, &timing, false);
    check(&engine, words, &timing, false);
    assert(generated == before);
    engine.held = true;
    engine.submitted = 0;
    timing.hold = 1;
    timing.slot = 22;
    timing.on_ns = 5000;
    timing.off_ns = 995000;
    check(&engine, words, &timing, true);
    check(&engine, words, &timing, false);
    for (unsigned i = 0; i < 2; ++i) free(engine.buffers[i]);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cache.c"
            path.write_text(harness)
            binary = Path(temp) / "cache"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "main"), str(path),
                            str(ROOT / "main/sony_timing.c"), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


class ScreenSpectrumTests(unittest.TestCase):
    def test_real_stereo_frequencies_levels_decay_and_grid_mapping(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "spectrum"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/test_sony_spectrum.c"),
                            str(ROOT / "main/sony_spectrum_dsp.c"),
                            "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class ScreenRadioStatusTests(unittest.TestCase):
    def test_channels_bitrate_genre_and_preserved_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "radio-status"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/test_radio_status.c"),
                            str(ROOT / "main/sony_radio_status.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class ScreenDisplayModeTests(unittest.TestCase):
    def test_metadata_views_fallbacks_and_quantized_dim_cycle(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "display-modes"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined",
                            str(ROOT / "tests/test_sony_display_modes.c"),
                            str(ROOT / "main/sony_display_modes.c"),
                            str(ROOT / "main/sony_timing.c"),
                            str(ROOT / "main/sony_text.c"),
                            str(ROOT / "main/audio/radio_display_text.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_first_spectrum_selection_rejects_reentrant_initialization(self):
        source = (ROOT / "main/sony_spectrum.c").read_text()
        initializer = source[source.index("esp_err_t sony_spectrum_init(void)"):
                             source.index("void sony_spectrum_enable(")]
        harness = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "sony_spectrum_dsp.h"
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM };
enum { MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, pdPASS = 1 };
#define INPUT_BYTES (SONY_SPECTRUM_SIZE * 8)
static atomic_bool initialized;
static atomic_flag initializing = ATOMIC_FLAG_INIT;
static sony_spectrum_dsp_t *dsp;
static int16_t *history;
static uint8_t *storage;
static int input_control;
static void *input;
static bool fail_allocations, reenter;
static unsigned allocations, tasks;
esp_err_t sony_spectrum_init(void);
static void *heap_caps_malloc(size_t bytes, int caps) {
    (void)caps;
    ++allocations;
    if (reenter) {
        reenter = false;
        assert(sony_spectrum_init() == ESP_ERR_INVALID_STATE);
        assert(allocations == 4);
    }
    return fail_allocations ? NULL : malloc(bytes);
}
static void *xStreamBufferCreateStatic(size_t bytes, unsigned trigger,
                                       void *buffer, int *control) {
    (void)bytes; (void)trigger; (void)control;
    return buffer;
}
static void vStreamBufferDelete(void *buffer) { (void)buffer; }
static void spectrum_task(void *unused) { (void)unused; }
static int xTaskCreatePinnedToCore(void (*task)(void *), const char *name,
                                   int stack, void *arg, int priority,
                                   void *handle, int core) {
    (void)task; (void)name; (void)stack; (void)arg;
    (void)priority; (void)handle; (void)core;
    ++tasks;
    return pdPASS;
}
"""
        harness += initializer + r"""
int main(void) {
    fail_allocations = true;
    assert(sony_spectrum_init() == ESP_ERR_NO_MEM);
    assert(!atomic_load(&initialized) && !dsp && !history && !storage);
    fail_allocations = false;
    reenter = true;
    assert(sony_spectrum_init() == ESP_OK);
    assert(allocations == 6 && tasks == 1 && atomic_load(&initialized));
    assert(sony_spectrum_init() == ESP_OK && allocations == 6 && tasks == 1);
    free(dsp); free(history); free(storage);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "init.c"
            path.write_text(harness)
            binary = Path(temp) / "init"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "main"), str(path),
                            str(ROOT / "main/sony_spectrum_dsp.c"),
                            "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class ScreenCheckStartTests(unittest.TestCase):
    def test_startup_artwork_fill_title_star_order_and_blank(self):
        with tempfile.TemporaryDirectory() as temp:
            library = Path(temp) / "startup.so"
            subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "main/sony_startup.c"),
                            str(ROOT / "main/sony_text.c"), "-o", str(library)], check=True)
            driver = ctypes.CDLL(str(library))
            driver.sony_text_ascii_word.argtypes = [ctypes.c_ubyte]
            driver.sony_text_ascii_word.restype = ctypes.c_uint64
            driver.sony_startup_frame.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_uint64)]
            driver.sony_startup_frame.restype = None

            def frame(ms):
                words = (ctypes.c_uint64 * 26)()
                driver.sony_startup_frame(ms, words)
                self.assertEqual([words[i] for i in (0, 23, 24, 25)], [0] * 4)
                return list(words)[1:23][::-1]

            def glyphs(text):
                return [driver.sony_text_ascii_word(ch) for ch in text]

            self.assertLess(sum(word.bit_count() for word in frame(300)), 770)
            self.assertEqual(frame(1200), [(1 << 35) - 1] * 22)
            self.assertEqual(frame(2150), glyphs(b"   SONY DAR-1000ES    "))
            early = frame(3200)
            for cell, ch in ((1, b"E"), (10, b"S")):
                mask = glyphs(ch)[0]
                self.assertEqual(early[cell] & mask, mask)
            self.assertTrue(all(word.bit_count() <= 1 for i, word in enumerate(early)
                                if i not in (1, 10)))
            phrase = glyphs(b" Elevated Standard    ")
            self.assertEqual(frame(4250), phrase)
            self.assertNotEqual(frame(5750), phrase)
            self.assertEqual(frame(6250), [0] * 22)

    def test_check_start_is_guarded_and_uses_standard_radio_scan(self):
        source = (ROOT / "main/sony_screen.c").read_text()
        ready = source[source.index("static bool radio_controls_ready("):
                       source.index("void sony_vfd_radio_search(")]
        function = source[source.index("static esp_err_t start_radio_display_locked("):
                          source.index("bool sony_screen_is_running(")]
        harness = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "sony_display_modes.h"
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_ERR_INVALID_STATE = 1, ESP_ERR_INVALID_ARG = 2,
       ESP_ERR_NO_MEM = 3, pdPASS = 1, portMAX_DELAY = 0 };
static int control_lock, frame_lock, task_result = pdPASS, init_result;
static atomic_bool running, display_requested, timing_requested, stop_requested, startup_active;
static int64_t now_us, startup_arm_until_us;
static int64_t esp_timer_get_time(void) { return now_us; }
static atomic_uint pin_profile_index, timing_revision, completed_frames;
static atomic_uint held_switches, held_last_interval_us;
static atomic_int last_error;
static bool follow_radio, follow_spectrum, display_dim, analyzer_enabled;
static struct { bool active; } preset_overlay;
static struct { bool active; } scrolling_text;
static sony_display_mode_t radio_display_mode;
static sony_timing_t display_timing, remembered_radio_timing;
static bool remembered_radio_timing_valid;
static uint64_t words[26];
static void xSemaphoreTake(int lock, int timeout) { (void)lock; (void)timeout; }
static void xSemaphoreGive(int lock) { (void)lock; }
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static esp_err_t sony_spectrum_init(void) { return init_result; }
static void sony_spectrum_enable(bool on) { analyzer_enabled = on; }
static esp_err_t sony_screen_display_mode_press(bool is_long) {
    (void)is_long; return ESP_ERR_INVALID_STATE;
}
bool sony_display_mode_graphical(sony_display_mode_t mode) {
    return mode == SONY_DISPLAY_VU_STEREO || mode == SONY_DISPLAY_VU_CENTER ||
           mode == SONY_DISPLAY_EQUALIZER;
}
static void scan_task(void *p) { (void)p; }
static int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
    int stack, void *arg, int priority, void *handle, int core) {
    assert(fn == scan_task && !strcmp(name, "screen_display"));
    (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
    return task_result;
}
""" + ready + function + r"""
int main(void) {
    assert(sony_screen_start_from_check() == ESP_ERR_INVALID_STATE);
    control_lock = 1;
    pin_profile_index = 1;
    assert(sony_screen_startup_press() == ESP_ERR_INVALID_STATE);
    assert(arm_startup_locked(false) == ESP_ERR_INVALID_STATE);
    assert(arm_startup_locked(true) == ESP_OK);
    now_us = 30000000;
    assert(sony_screen_startup_press() == ESP_ERR_INVALID_STATE);
    assert(arm_startup_locked(true) == ESP_OK);
    assert(sony_screen_startup_press() == ESP_OK);
    assert(startup_active && running && !follow_radio && !analyzer_enabled);
    assert(arm_startup_locked(true) == ESP_ERR_INVALID_STATE);
    running = startup_active = false;
    assert(sony_screen_startup_press() == ESP_ERR_INVALID_STATE);
    pin_profile_index = 0;
    assert(sony_screen_start_from_check() == ESP_ERR_INVALID_STATE);
    atomic_store(&pin_profile_index, 1);
    atomic_store(&running, true);
    assert(sony_screen_start_from_check() == ESP_ERR_INVALID_STATE);
    atomic_store(&running, false);
    init_result = ESP_ERR_NO_MEM;
    assert(sony_screen_start_from_check() == ESP_ERR_NO_MEM);
    assert(!running && !analyzer_enabled);
    init_result = ESP_OK;
    task_result = 0;
    assert(sony_screen_start_from_check() == ESP_ERR_NO_MEM);
    assert(!running && !display_requested && !timing_requested &&
           stop_requested && !follow_radio && !analyzer_enabled);
    task_result = pdPASS;
    memset(words, 0xff, sizeof(words));
    assert(sony_screen_start_from_check() == ESP_OK);
    assert(running && display_requested && timing_requested && !stop_requested);
    assert(follow_radio && follow_spectrum && analyzer_enabled && display_dim);
    assert(radio_display_mode == SONY_DISPLAY_VU_STEREO);
    assert(display_timing.on_ns == 93625 && display_timing.off_ns == 68875);
    assert(!sony_timing_validate(&display_timing));
    for (unsigned i = 0; i < 26; ++i) assert(!words[i]);
    assert(sony_screen_start_from_check() == ESP_ERR_INVALID_STATE);
    assert(running && analyzer_enabled);
    display_timing.on_ns = 109250;
    display_timing.off_ns = 53250;
    radio_display_mode = SONY_DISPLAY_VU_CENTER;
    assert(sony_screen_display_button_press(false) == ESP_OK);
    assert(stop_requested && remembered_radio_timing_valid);
    running = false;
    assert(sony_screen_start_from_check() == ESP_OK);
    assert(display_timing.on_ns == 109250 && display_timing.off_ns == 53250);
    display_timing.on_ns = 110000;
    display_timing.off_ns = 52500;
    radio_display_mode = SONY_DISPLAY_VU_CENTER;
    assert(sony_screen_display_button_press(false) == ESP_OK);
    running = false;
    assert(sony_screen_display_button_press(false) == ESP_OK);
    assert(radio_display_mode == SONY_DISPLAY_NOW_PLAYING);
    assert(display_timing.on_ns == 110000 && display_timing.off_ns == 52500);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "check.c"
            path.write_text(harness)
            binary = Path(temp) / "check"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "main"), str(path),
                            str(ROOT / "main/sony_timing.c"), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)

    def test_display_button_starts_cycles_off_and_restarts(self):
        source = (ROOT / "main/sony_screen.c").read_text()
        function = source[source.index("static esp_err_t display_button_action_locked("):
                          source.index("bool sony_screen_is_running(")]
        harness = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "sony_display_modes.h"
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_ERR_INVALID_STATE = 1, portMAX_DELAY = 0 };
static int control_lock = 1, frame_lock, long_presses, starts;
static atomic_bool running, stop_requested, startup_active;
static bool follow_radio, follow_spectrum, analyzer_enabled;
static sony_display_mode_t radio_display_mode;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static void xSemaphoreTake(int l, int t) { (void)l; (void)t; }
static void xSemaphoreGive(int l) { (void)l; }
static bool radio_controls_ready(void) {
    return running && follow_radio && !stop_requested;
}
static void remember_radio_timing_locked(void) {}
static void sony_spectrum_enable(bool on) { analyzer_enabled = on; }
static esp_err_t start_radio_display_locked(sony_display_mode_t mode, bool startup) {
    assert(!startup);
    ++starts;
    running = follow_radio = true;
    stop_requested = false;
    radio_display_mode = mode;
    return ESP_OK;
}
static esp_err_t sony_screen_display_mode_press(bool is_long) {
    if (!radio_controls_ready()) return ESP_ERR_INVALID_STATE;
    if (is_long) ++long_presses;
    else ++radio_display_mode;
    return ESP_OK;
}
""" + function + r"""
int main(void) {
    assert(sony_screen_display_button_press(true) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_button_press(false) == ESP_OK);
    assert(starts == 1 && running && radio_display_mode == SONY_DISPLAY_NOW_PLAYING);
    assert(sony_screen_display_button_press(true) == ESP_OK && long_presses == 1);
    for (unsigned i = 1; i < SONY_DISPLAY_MODE_COUNT; ++i) {
        assert(sony_screen_display_button_press(false) == ESP_OK);
        assert(radio_display_mode == i && !stop_requested);
    }
    follow_spectrum = analyzer_enabled = true;
    assert(sony_screen_display_button_press(false) == ESP_OK);
    assert(stop_requested && !follow_radio && !follow_spectrum && !analyzer_enabled);
    assert(sony_screen_display_button_press(false) == ESP_ERR_INVALID_STATE);
    running = false;
    assert(sony_screen_display_button_press(false) == ESP_OK);
    assert(starts == 2 && radio_display_mode == SONY_DISPLAY_NOW_PLAYING);
    follow_radio = false;
    assert(sony_screen_display_button_press(false) == ESP_ERR_INVALID_STATE);
    assert(running && !stop_requested);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "display.c"
            path.write_text(harness)
            binary = Path(temp) / "display"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "main"), str(path),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class ScreenRadioOwnershipTests(unittest.TestCase):
    def test_metadata_cannot_start_output_or_override_manual_control(self):
        source = (ROOT / "main/sony_screen.c").read_text()
        functions = source[source.index("static esp_err_t show_text("):
                           source.index("static esp_err_t set(")]
        harness = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sony_text.h"
#include "sony_radio_status.h"
#include "sony_spectrum_dsp.h"
#include "sony_display_modes.h"
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG };
static atomic_bool running, display_requested, stop_requested, timing_requested;
static sony_timing_t display_timing;
static sony_timing_t remembered_radio_timing;
static bool remembered_radio_timing_valid;
static atomic_uint timing_revision;
static sony_display_mode_t radio_display_mode;
sony_display_mode_t sony_screen_display_mode(void);
static bool follow_radio, follow_spectrum, display_dim, analyzer_enabled;
static sony_radio_status_t last_radio_status;
static struct {
    uint64_t preset[22], result[22];
    int64_t started_us, result_started_us;
    uint32_t generation;
    bool active, assigned, result_ready;
} preset_overlay;
static int64_t search_started_us;
static void sony_spectrum_enable(bool on) { analyzer_enabled = on; }
static esp_err_t sony_spectrum_init(void) { return ESP_OK; }
static unsigned scrolling_width = 9;
static sony_text_t scrolling_text;
static uint64_t words[26];
static int frame_lock;
static int64_t now;
static int64_t esp_timer_get_time(void) { return now; }
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
"""
        harness += functions + r"""
int main(void) {
    sony_radio_status_t status = sony_radio_status(true, 2, "192", "Pop");
    remember_radio_timing_locked();
    assert(!remembered_radio_timing_valid);
    uint8_t bars[22];
    memset(bars, 3, sizeof(bars));
    assert(sony_vfd_show_spectrum(bars) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(false) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(true) == ESP_ERR_INVALID_STATE);
    assert(sony_vfd_show_radio("Song", &status) == ESP_ERR_INVALID_STATE);
    assert(!atomic_load(&running) && !scrolling_text.active);
    atomic_store(&running, true);
    atomic_store(&display_requested, true);
    atomic_store(&timing_requested, true);
    assert(sony_vfd_show_radio("Song", &status) == ESP_ERR_INVALID_STATE);
    follow_radio = true;
    display_timing.on_ns = 123;
    remember_radio_timing_locked();
    assert(remembered_radio_timing_valid && remembered_radio_timing.on_ns == 123);
    now = 1234;
    assert(sony_vfd_show_radio("A long title that needs to scroll", &status) == ESP_OK);
    assert(scrolling_width == 22 && scrolling_text.started_us == 1234);
    assert(words[0] == 0xFF && words[24] == 0x2002 && words[25] == 1);
    now = 2000;
    status = sony_radio_status(true, 1, "128", "Talk");
    assert(sony_vfd_show_radio("A long title that needs to scroll", &status) == ESP_OK);
    assert(scrolling_text.started_us == 1234);
    assert(words[0] == 0x3F && words[24] == 0x2011 && words[25] == 0);
    follow_spectrum = true;
    analyzer_enabled = true;
    radio_display_mode = SONY_DISPLAY_EQUALIZER;
    assert(sony_vfd_show_spectrum(bars) == ESP_OK);
    uint64_t spectrum_words[26];
    memcpy(spectrum_words, words, sizeof(words));
    assert(sony_vfd_show_radio("New title", &status) == ESP_OK);
    for (unsigned i = 1; i <= 22; ++i)
        assert(spectrum_words[i] == words[i]);
    assert(words[0] == 0 && words[24] == 0x2011 && words[25] == 0);
    status = sony_radio_status(true, 2, "192", "Mixed");
    assert(sony_vfd_show_radio("New title", &status) == ESP_OK);
    assert(words[0] == 0 && words[24] == 0x2003 && words[25] == 1);
    assert(sony_screen_set_display_mode(SONY_DISPLAY_VU_STEREO) == ESP_OK);
    assert(words[0] == 0xff && words[24] == 0x2003 && words[25] == 1);
    assert(sony_screen_set_display_mode(SONY_DISPLAY_EQUALIZER) == ESP_OK);
    assert(words[0] == 0 && words[24] == 0x2003 && words[25] == 1);
    status = sony_radio_status(false, 2, "192", "Mixed");
    assert(sony_vfd_show_radio("New title", &status) == ESP_OK);
    assert(words[0] == 0x4000 && words[24] == 0 && words[25] == 0);
    assert(show_text("New title", true, true, 22, NULL, SONY_DISPLAY_MODE_COUNT) == ESP_OK);
    assert(!follow_spectrum && !analyzer_enabled);
    assert(words[24] == 0 && words[25] == 0);
    assert(memcmp(spectrum_words, words, sizeof(words)));
    assert(sony_vfd_show("Manual", true, true) == ESP_OK);
    assert(!follow_radio && !follow_spectrum && !analyzer_enabled &&
           scrolling_width == 9);
    assert(sony_vfd_show_spectrum(bars) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(false) == ESP_ERR_INVALID_STATE);
    uint64_t before[26];
    memcpy(before, words, sizeof(words));
    assert(sony_vfd_show_radio("New song", &status) == ESP_ERR_INVALID_STATE);
    assert(!memcmp(before, words, sizeof(words)));
    assert(sony_vfd_show_spectrum(bars) == ESP_ERR_INVALID_STATE);
    follow_radio = true;
    follow_spectrum = false;
    display_timing = (sony_timing_t){
        .setup_ns=500, .clock_low_ns=1000, .clock_high_ns=1000,
        .strobe_ns=5000, .latch_guard_ns=10000,
        .blank_before_ns=10000, .blank_after_ns=10000,
        .on_ns=58500, .off_ns=104000, .pulses=1};
    radio_display_mode = SONY_DISPLAY_NOW_PLAYING;
    for (unsigned i = 1; i <= SONY_DISPLAY_MODE_COUNT; ++i) {
        assert(sony_screen_display_mode_press(false) == ESP_OK);
        assert(sony_screen_display_mode() == i % SONY_DISPLAY_MODE_COUNT);
        assert(follow_spectrum == sony_display_mode_graphical(i % SONY_DISPLAY_MODE_COUNT));
        assert(analyzer_enabled == follow_spectrum);
        assert(!scrolling_text.active);
        for (unsigned slot = 1; slot <= 22; ++slot)
            assert(words[slot] == 0);
        assert(display_timing.on_ns == 58500);
    }
    uint32_t peak[] = {23199, 0};
    assert(sony_screen_set_display_mode(SONY_DISPLAY_VU_STEREO) == ESP_OK);
    assert(sony_vfd_show_vu(peak, peak, peak) == ESP_OK);
    assert(words[22] == 0x7fff && (words[24] & 0x1c0) == 0x80);
    assert(sony_vfd_show_spectrum(bars) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_set_display_mode(SONY_DISPLAY_VU_CENTER) == ESP_OK);
    assert(!(words[24] & 0x51c0));
    assert(sony_vfd_show_vu(peak, peak, peak) == ESP_OK);
    assert(words[22] == 0x7ffffffffULL && (words[24] & 0x5000) == 0x4000);
    assert(sony_screen_set_display_mode(SONY_DISPLAY_NOW_PLAYING) == ESP_OK);
    assert(!(words[24] & 0x51c0));
    assert(sony_vfd_show_vu(peak, peak, peak) == ESP_ERR_INVALID_STATE);
    status.searching = true;
    status.generation = 1;
    now = 3000;
    assert(sony_vfd_show_radio("Searching", &status) == ESP_OK);
    assert(search_started_us == 3000);
    now = 4000;
    assert(sony_vfd_show_radio("Searching", &status) == ESP_OK);
    assert(search_started_us == 3000);
    status.generation = 2;
    assert(sony_vfd_show_radio("Searching", &status) == ESP_OK);
    assert(search_started_us == 4000);
    sony_vfd_radio_search(3);
    assert(sony_vfd_show_radio("stale", &status) == ESP_ERR_INVALID_STATE);
    status.generation = 3;
    assert(sony_screen_set_display_mode(SONY_DISPLAY_MODE_COUNT) == ESP_ERR_INVALID_ARG);
    assert(sony_screen_set_display_mode(SONY_DISPLAY_AUDIO_FORMAT) == ESP_OK);
    assert(sony_vfd_show_radio_mode("stale title", &status,
                                  SONY_DISPLAY_NOW_PLAYING) == ESP_ERR_INVALID_STATE);
    assert(sony_vfd_show_radio_mode("MP3 44.1 kHz STEREO", &status,
                                  SONY_DISPLAY_AUDIO_FORMAT) == ESP_OK);
    int64_t started = scrolling_text.started_us;
    assert(sony_screen_set_display_mode(SONY_DISPLAY_AUDIO_FORMAT) == ESP_OK);
    assert(scrolling_text.active && scrolling_text.started_us == started);
    for (unsigned level = 0; level < 5; ++level) {
        assert(sony_screen_display_mode_press(true) == ESP_OK);
        assert(sony_display_dim_level(&display_timing) == (int)level);
        const unsigned on[] = {93625, 109250, 124750, 78000, 58500};
        assert(display_timing.on_ns == on[level]);
        assert(radio_display_mode == SONY_DISPLAY_AUDIO_FORMAT);
    }
    assert(atomic_load(&timing_revision) == 5);
    memcpy(before, words, sizeof(words));
    follow_radio = true;
    display_timing.hold = true;
    assert(sony_vfd_show_radio("Song", &status) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(false) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(true) == ESP_ERR_INVALID_STATE);
    display_timing.hold = false;
    display_timing.pair = true;
    assert(sony_vfd_show_radio("Song", &status) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(false) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(true) == ESP_ERR_INVALID_STATE);
    display_timing.pair = false;
    atomic_store(&stop_requested, true);
    assert(sony_vfd_show_radio("Song", &status) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(false) == ESP_ERR_INVALID_STATE);
    assert(sony_screen_display_mode_press(true) == ESP_ERR_INVALID_STATE);
    assert(!memcmp(before, words, sizeof(words)));
    atomic_store(&stop_requested, false);
    now = 1000000;
    sony_vfd_preset_start(12, 3, true);
    assert(preset_overlay.active && preset_overlay.assigned);
    uint64_t output[26] = {0};
    output[0] = 123;
    output[24] = 456;
    assert(preset_overlay_frame(now, output));
    sony_text_t label;
    assert(sony_text_parse(&label, "   Preset 12", now));
    for (unsigned i = 0; i < 22; ++i)
        assert(output[22 - i] == (i < label.length ? label.glyphs[i] : 0));
    assert(output[14] == label.glyphs[8] && output[12] == label.glyphs[10] &&
           output[11] == label.glyphs[11]);
    assert(output[0] == 123 && output[24] == 456);
    memset(output + 1, 0, 22 * sizeof(*output));
    assert(preset_overlay_frame(now + 1000000, output));
    assert(output[2] == 0x7ffffffffULL);
    assert(preset_overlay_frame(now + 61000000, output));
    assert(preset_overlay.active && !preset_overlay.result_ready);
    sony_vfd_preset_start(5, 4, true);
    sony_vfd_preset_connected(3, "Old station");
    assert(!preset_overlay.result_ready);
    sony_vfd_preset_connected(4, "Example FM");
    assert(preset_overlay.result_ready);
    assert(sony_screen_set_display_mode(SONY_DISPLAY_VU_STEREO) == ESP_OK);
    assert(preset_overlay_frame(now + 1050000, output));
    assert(output[2] == 0x7ffffffffULL);
    assert(preset_overlay_frame(now + 1100000, output));
    assert(sony_text_parse(&label, "Example FM", now));
    for (unsigned i = 0; i < 22; ++i)
        assert(output[22 - i] == (i < label.length ? label.glyphs[i] : 0));
    assert(preset_overlay_frame(now + 3099999, output));
    assert(!preset_overlay_frame(now + 3100000, output));
    assert(!preset_overlay.active && radio_display_mode == SONY_DISPLAY_VU_STEREO);
    now = 9000000;
    sony_vfd_preset_start(2, 4, false);
    assert(preset_overlay.active && !preset_overlay.assigned);
    assert(preset_overlay_frame(now + 1000000, output));
    assert(sony_text_parse(&label, "Unassigned", now));
    for (unsigned i = 0; i < 22; ++i)
        assert(output[22 - i] == (i < label.length ? label.glyphs[i] : 0));
    assert(!preset_overlay_frame(now + 3000000, output));
    sony_vfd_preset_start(1, 4, true);
    assert(preset_overlay.active);
    sony_vfd_radio_search(5);
    assert(!preset_overlay.active);
    sony_vfd_preset_start(1, 4, true);
    assert(!preset_overlay.active);
    follow_radio = false;
    sony_vfd_preset_start(1, 5, true);
    assert(!preset_overlay.active);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "radio.c"
            path.write_text(harness)
            binary = Path(temp) / "radio"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "main"), str(path),
                            str(ROOT / "main/sony_text.c"),
                            str(ROOT / "main/sony_radio_status.c"),
                            str(ROOT / "main/sony_spectrum_dsp.c"),
                            str(ROOT / "main/sony_display_modes.c"),
                            str(ROOT / "main/sony_timing.c"),
                            str(ROOT / "main/audio/radio_display_text.c"),
                            "-lm", "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


class ScreenReplayTests(unittest.TestCase):
    def test_repeated_and_simultaneous_changes(self):
        payload = bytes([0, 0, 63, 63, 33, 41, 33])
        self.assertEqual(decode(replay.encode_runs(payload, 1)), (payload, 1))

    def test_long_run_is_split_without_losing_samples(self):
        payload = bytes([33]) * 65536 + bytes([41])
        encoded = replay.encode_runs(payload, 1)
        self.assertEqual(decode(encoded)[0], payload)
        self.assertEqual(list(struct.iter_unpack("<HB", encoded[17:])),
                         [(65535, 33), (1, 33), (1, 41)])

    def test_initial_segment_word_is_preserved(self):
        encoded = replay.encode_runs(bytes([41]), 1, 0x123456789A)
        self.assertEqual(int.from_bytes(encoded[12:17], "little"), 0x123456789A)

    def test_invalid_waveforms_are_rejected(self):
        for payload in (b"", bytes([64])):
            with self.assertRaises(ValueError):
                replay.encode_runs(payload, 1)

    def test_embedded_capture_matches_original_sample_fixture(self):
        fixture = ROOT / "tests/fixtures/screen"
        for name, frames in (("mono-blink", 130), ("frequency", 128)):
            with self.subTest(name=name):
                original = gzip.decompress((fixture / (name + ".raw.gz")).read_bytes())
                self.assertEqual(decode((fixture / (name + ".rle")).read_bytes()),
                                 (original, frames))

    def test_generated_dim_waveform_preserves_scan_and_replaces_words(self):
        with tempfile.TemporaryDirectory() as temp:
            library = Path(temp) / "display.so"
            subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Werror",
                            str(ROOT / "main/sony_display_frame.c"),
                            "-o", str(library)], check=True)
            generate = ctypes.CDLL(str(library)).sony_display_waveform
            generate.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                 ctypes.POINTER(ctypes.c_uint64)]
            generate.restype = ctypes.c_size_t
            original = gzip.decompress(
                (ROOT / "tests/fixtures/screen/frequency.raw.gz").read_bytes())
            capacity = 8 * 26 * 2500 + 4096
            wave = (ctypes.c_uint8 * capacity).from_buffer_copy(original[:capacity])
            words = (ctypes.c_uint64 * 26)(
                *[(0x123456789A ^ (i << 20)) for i in range(26)])
            bright = ctypes.CDLL(str(library)).sony_display_waveform_brightness
            bright.argtypes = generate.argtypes + [ctypes.c_bool]
            bright.restype = ctypes.c_size_t
            bright_wave = (ctypes.c_uint8 * capacity).from_buffer_copy(original[:capacity])
            bright_length = bright(bright_wave, capacity, words, False)
            self.assertGreater(bright_length, 0)
            length = generate(wave, capacity, words)
            self.assertGreater(length, 0)

            def assert_scan(buffer, scan_length, dim):
                result = bytes(buffer[:scan_length])
                lines = [
                    result.translate(bytes((v >> bit) & 1 for v in range(256)))
                    for bit in range(6)
                ]
                rises = lambda bit: [
                    m.start() + 1 for m in re.finditer(b"\0\1", lines[bit])
                ]
                latches = [0] + rises(3) + [scan_length]
                clocks = rises(0)
                grids = rises(5)
                self.assertEqual(len(latches), 8 * 26 + 1)
                slot_lengths = []
                for index, (start, end) in enumerate(zip(latches, latches[1:])):
                    self.assertEqual(end - start, 1230)
                    slot_lengths.append(end - start)
                    serial = clocks[bisect_left(clocks, start):bisect_left(clocks, end)]
                    self.assertEqual(len(serial), 40)
                    self.assertEqual(
                        len(grids[bisect_left(grids, start):bisect_left(grids, end)]),
                        1,
                    )
                    word = 0
                    for clock in serial:
                        word = (word << 1) | lines[1][clock]
                    self.assertEqual(word, words[(index + 1) % 26])
                    self.assertEqual(lines[2][end - 1], 0)
                    if dim:
                        clear_low = 0
                        while start + clear_low < end and not lines[2][start + clear_low]:
                            clear_low += 1
                        self.assertGreaterEqual(clear_low, 800)
                        self.assertLessEqual(clear_low, 840)
                self.assertGreater(
                    8_000_000 * len(slot_lengths),
                    240 * 26 * sum(slot_lengths),
                )

            assert_scan(bright_wave, bright_length, False)
            assert_scan(wave, length, True)
            short = (ctypes.c_uint8 * 20).from_buffer_copy(original[:20])
            self.assertEqual(generate(short, 20, words), 0)


class Timing(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in (
        "setup_ns", "clock_low_ns", "clock_high_ns", "strobe_ns",
        "latch_guard_ns", "blank_before_ns", "blank_after_ns",
        "on_ns", "off_ns", "pulses", "hold", "slot", "pair", "alternate_ms",
        "alternate_blank_steps")]


class ScreenTimingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "timing.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "main/sony_timing.c"), "-o", str(library)], check=True)
        cls.library = ctypes.CDLL(str(library))
        cls.library.sony_timing_defaults.restype = Timing
        cls.library.sony_timing_validate.argtypes = [ctypes.POINTER(Timing)]
        cls.library.sony_timing_validate.restype = ctypes.c_char_p
        cls.library.sony_timing_generate.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(Timing),
            ctypes.POINTER(ctypes.c_uint64), ctypes.c_bool]
        cls.library.sony_timing_generate.restype = ctypes.c_size_t
        cls.library.sony_timing_generate_clear.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(Timing)]
        cls.library.sony_timing_generate_clear.restype = ctypes.c_size_t
        cls.library.sony_timing_held_slot.argtypes = [
            ctypes.POINTER(Timing), ctypes.c_uint64]
        cls.library.sony_timing_held_slot.restype = ctypes.c_uint

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def generate(self, t, words, initialize=True):
        wave = (ctypes.c_uint8 * 524288)()
        size = self.library.sony_timing_generate(
            wave, len(wave), ctypes.byref(t),
            (ctypes.c_uint64 * 26)(*words), initialize)
        self.assertGreater(size, 0)
        self.assertEqual(size % 64, 0)
        return bytes(wave[:size])

    def visible_words(self, wave):
        segment = grid = 0
        latched_segment = latched_grid = None
        visible = []
        prior = 0x21
        for sample in wave:
            rising = sample & ~prior
            if rising & 1:
                self.assertFalse(sample & 12, "Shifts must be blanked, STB LOW")
                segment = ((segment << 1) | bool(sample & 2)) & ((1 << 40) - 1)
            if rising & 32:
                self.assertFalse(sample & 12)
                grid = ((grid << 1) | bool(sample & 16)) & ((1 << 40) - 1)
            if rising & 8:
                self.assertFalse(sample & 4, "Latch must be blanked")
                latched_segment, latched_grid = segment, grid
            if rising & 4:
                self.assertFalse(sample & 8)
                visible.append((latched_grid, latched_segment))
            prior = sample
        self.assertEqual(wave[-1] & 0x2d, 0x21)
        return visible

    def test_scanning_all_40_bits_grid_order_and_repeated_clear_only(self):
        words = [(0x8123456789 ^ (slot << 16)) for slot in range(26)]
        for pulses in (1, 4, 16):
            with self.subTest(pulses=pulses):
                timing = self.library.sony_timing_defaults()
                timing.pulses = pulses
                timing.on_ns = timing.off_ns = 20000
                wave = self.generate(timing, words)
                visible = self.visible_words(wave)
                self.assertEqual(len(visible) % (26 * pulses), 0)
                for index, (grid, word) in enumerate(visible):
                    slot = (index // pulses) % 26
                    # Original Sony injects every 26 shifts into a 40-bit IC.
                    self.assertEqual(grid & ((1 << 26) - 1), 1 << slot)
                    self.assertEqual(word, words[slot])
                continuation = self.generate(timing, words, False)
                self.assertEqual(self.visible_words(continuation), visible)
                # Each scan slot shifts once, regardless of PWM count.
                rises = sum(bool(b & 1 and not a & 1)
                            for a, b in zip(wave, wave[1:]))
                self.assertEqual(rises, 40 + 40 * len(visible) // pulses)

    def test_full_scan_batches_cover_audio_bursts_without_changing_pulses(self):
        timing = self.library.sony_timing_defaults()
        timing.setup_ns = 500
        timing.clock_low_ns = timing.clock_high_ns = 1000
        timing.strobe_ns = 5000
        timing.latch_guard_ns = timing.blank_before_ns = timing.blank_after_ns = 10000
        timing.on_ns, timing.off_ns = 58500, 104000
        words = [0x7FFFFFFFF] * 26
        wave = self.generate(timing, words)
        self.assertGreater(len(wave) * 125, 50000000)
        self.assertLessEqual(len(wave), 524288)
        visible = self.visible_words(wave)
        self.assertEqual(len(visible), 8 * 26)
        for index, (grid, word) in enumerate(visible):
            self.assertEqual(grid & ((1 << 26) - 1), 1 << (index % 26))
            self.assertEqual(word, words[index % 26])
        clear = bytes((sample >> 2) & 1 for sample in wave)
        highs = list(re.finditer(b"\x01+", clear))
        self.assertEqual(len(highs), 8 * 26)
        self.assertTrue(all(len(high.group()) * 125 == 58500 for high in highs))
        starts = [high.start() for high in highs]
        self.assertEqual({b - a for a, b in zip(starts, starts[1:])}, {2400})

    def test_local_held_alternation_boundaries_and_clear_only_blocks(self):
        timing = self.library.sony_timing_defaults()
        timing.hold, timing.slot, timing.alternate_ms = 1, 22, 50
        timing.on_ns, timing.off_ns = 20000, 1980000
        self.assertIsNone(self.library.sony_timing_validate(ctypes.byref(timing)))
        for elapsed, slot in ((0, 22), (49999, 22), (50000, 21),
                              (99999, 21), (100000, 22), (150000, 21),
                              (5000000000, 22)):
            self.assertEqual(self.library.sony_timing_held_slot(
                ctypes.byref(timing), elapsed), slot)
        wave = (ctypes.c_uint8 * 524288)()
        size = self.library.sony_timing_generate_clear(
            wave, len(wave), ctypes.byref(timing))
        self.assertGreater(size, 0)
        data = bytes(wave[:size])
        self.assertEqual(set(data), {0, 4})
        highs = list(re.finditer(b"\x04+", data))
        self.assertEqual(len(highs), 1)
        self.assertEqual(len(highs[0].group()) * 125, 20000)
        for key, value in (("hold", 0), ("pair", 1),
                           ("alternate_ms", 4), ("alternate_ms", 1001)):
            copy = Timing.from_buffer_copy(timing)
            setattr(copy, key, value)
            self.assertIsNotNone(self.library.sony_timing_validate(ctypes.byref(copy)))
        timing.alternate_ms = 0
        self.assertEqual(self.library.sony_timing_held_slot(
            ctypes.byref(timing), 75000), 22)
        timing.slot, timing.alternate_ms = 1, 50
        self.assertEqual(self.library.sony_timing_held_slot(
            ctypes.byref(timing), 50000), 0)
        timing.slot = 0
        self.assertIsNone(self.library.sony_timing_validate(ctypes.byref(timing)))

    def test_local_held_trailing_blank_steps(self):
        timing = self.library.sony_timing_defaults()
        timing.hold, timing.slot, timing.alternate_ms = 1, 22, 5
        timing.alternate_blank_steps = 3
        timing.on_ns, timing.off_ns = 37500, 962500
        self.assertIsNone(self.library.sony_timing_validate(ctypes.byref(timing)))
        for elapsed, slot in ((0, 22), (4999, 22), (5000, 21), (9999, 21),
                              (10000, 26), (14999, 26), (15000, 26),
                              (20000, 26), (24999, 26), (25000, 22),
                              (30000, 21), (35000, 26), (5000000000, 22)):
            self.assertEqual(self.library.sony_timing_held_slot(
                ctypes.byref(timing), elapsed), slot)
        for key, value in (("alternate_blank_steps", 21), ("alternate_ms", 0),
                           ("hold", 0), ("pair", 1)):
            copy = Timing.from_buffer_copy(timing)
            setattr(copy, key, value)
            self.assertIsNotNone(self.library.sony_timing_validate(ctypes.byref(copy)))
        timing.slot = 1
        self.assertEqual(self.library.sony_timing_held_slot(
            ctypes.byref(timing), 5000), 0)
        self.assertEqual(self.library.sony_timing_held_slot(
            ctypes.byref(timing), 10000), 26)

    def test_full_blank_disconnects_clear_before_draining_and_forces_preload(self):
        source = (ROOT / "main/sony_capture_replay.c").read_text()
        functions = source[source.index("static esp_err_t blank_and_drain("):
                           source.index("esp_err_t sony_scan_close(")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_ERR_INVALID_ARG = 1, ESP_ERR_TIMEOUT = 2,
       SONY_CLEAR = 6, SIG_GPIO_OUT_IDX = 99, GPIO_MODE_OUTPUT = 1,
       pdFALSE = 0 };
#define pdMS_TO_TICKS(x) (x)
typedef struct { bool held; unsigned submitted, in_flight; } sony_scan_engine_t;
static int sequence, level, signal, notifications, fail;
static int gpio_set_level(int pin, int value) {
    assert(pin == SONY_CLEAR && value == 0 && sequence++ == 0);
    level = value;
    return ESP_OK;
}
static void esp_rom_gpio_connect_out_signal(int pin, int sig, bool a, bool b) {
    assert(pin == SONY_CLEAR && !a && !b && sequence++ == 1);
    signal = sig;
}
static int gpio_set_direction(int pin, int mode) {
    assert(pin == SONY_CLEAR && mode == GPIO_MODE_OUTPUT && sequence++ == 2);
    return ESP_OK;
}
static void esp_rom_delay_us(int us) {
    assert(us == 2 && sequence++ == 3);
}
static int ulTaskNotifyTake(int clear, int timeout) {
    assert(!clear && timeout == 1000 && sequence >= 4);
    assert(level == 0 && signal == SIG_GPIO_OUT_IDX);
    ++notifications;
    return !fail;
}
"""
        harness += functions + r"""
int main(void) {
    sony_scan_engine_t engine = {.held = true, .submitted = 42, .in_flight = 2};
    assert(sony_scan_blank(&engine) == ESP_OK);
    assert(engine.in_flight == 0 && engine.submitted == 0);
    assert(sequence == 4 && notifications == 2);
    sequence = notifications = 0;
    engine.submitted = 42; engine.in_flight = 1; fail = 1;
    assert(sony_scan_blank(&engine) == ESP_ERR_TIMEOUT);
    assert(engine.submitted == 42 && engine.in_flight == 1);
    assert(level == 0 && signal == SIG_GPIO_OUT_IDX);
    assert(sony_scan_blank(NULL) == ESP_ERR_INVALID_ARG);
    engine.held = false;
    assert(sony_scan_blank(&engine) == ESP_ERR_INVALID_ARG);
    return 0;
}
"""
        path = Path(self.temp.name) / "blank.c"
        path.write_text(harness)
        binary = path.with_suffix("")
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(path),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_pair_scan_reloads_complete_one_hot_grid_each_slot(self):
        timing = self.library.sony_timing_defaults()
        timing.pair, timing.slot = 1, 22
        timing.setup_ns = 1000
        timing.clock_low_ns = timing.clock_high_ns = 2000
        timing.strobe_ns = timing.latch_guard_ns = 5000
        timing.blank_before_ns = timing.blank_after_ns = 5000
        timing.on_ns, timing.off_ns = 20000, 2060000
        words = [0xFFFFFFFFFF] * 26
        words[22], words[21] = 0x02108421C8, 0x07C226422E
        for initialize in (True, False):
            wave = self.generate(timing, words, initialize)
            visible = self.visible_words(wave)
            self.assertGreaterEqual(len(visible), 2)
            for index, (grid, word) in enumerate(visible):
                slot = 22 - index % 2
                self.assertEqual((grid, word), (1 << slot, words[slot]))
            for bit in (1, 32):
                clocks = sum(bool(b & bit and not a & bit)
                             for a, b in zip(wave, wave[1:]))
                self.assertEqual(clocks, 40 * (len(visible) + initialize))
        timing.slot = 0
        self.assertIsNotNone(self.library.sony_timing_validate(ctypes.byref(timing)))
        timing.slot, timing.hold = 22, 1
        self.assertIsNotNone(self.library.sony_timing_validate(ctypes.byref(timing)))
        timing.hold, timing.on_ns, timing.off_ns = 0, 50000, 2000
        self.assertIn(b"1/26", self.library.sony_timing_validate(ctypes.byref(timing)))
        timing.on_ns, timing.off_ns, timing.clock_low_ns = 20000, 2060000, 500
        self.assertIn(b"whole microseconds",
                      self.library.sony_timing_validate(ctypes.byref(timing)))
        timing.clock_low_ns, timing.on_ns = 2000, 51000
        self.assertIn(b"ON <=", self.library.sony_timing_validate(ctypes.byref(timing)))

    def test_held_grid_loads_once_then_clear_without_clock_or_strobe(self):
        words = [0x8000000001 ^ slot for slot in range(26)]
        for slot in (0, 14, 25):
            timing = self.library.sony_timing_defaults()
            timing.hold, timing.slot, timing.off_ns = 1, slot, 1450000
            initial = self.generate(timing, words)
            self.assertTrue(all(pair == (1 << slot, words[slot])
                                for pair in self.visible_words(initial)))
            continuation = self.generate(timing, words, False)
            self.assertTrue(all(sample & 0x29 == 0x21 for sample in continuation))
            self.assertGreater(sum(bool(b & 4 and not a & 4)
                                   for a, b in zip(continuation, continuation[1:])), 1)

    def test_gpio_pair_executor_blanking_stop_and_write_failure(self):
        source = (ROOT / "main/sony_screen.c").read_text()
        function = source[source.index("static esp_err_t gpio_pair_cycle("):
                          source.index("static void record_output_frame(")]
        harness = r"""
#include <stdatomic.h>
#include "sony_timing.h"
typedef int esp_err_t;
typedef unsigned gpio_num_t;
enum { ESP_OK, ESP_ERR_INVALID_SIZE };
enum { SEGMENT_CLOCK, SEGMENT_DATA, GRID_CLEAR, STROBE, GRID_DATA, GRID_CLOCK };
static atomic_bool stop_requested;
static unsigned calls, fail_at, delays, stop_at;
static uint8_t state, output[1000000], input[SONY_TIMING_CAPACITY];
static size_t length;
#define portENTER_CRITICAL(lock) ((void)0)
#define portEXIT_CRITICAL(lock) ((void)0)
static int set(unsigned pin, int level) {
  if (++calls == fail_at) return 7;
  state = (state & ~(1U << pin)) | ((unsigned)level << pin);
  return ESP_OK;
}
static void esp_rom_delay_us(unsigned us) {
  for (unsigned i = 0; i < us * 8 && length < sizeof(output); ++i)
    output[length++] = state;
  if (++delays == stop_at) atomic_store(&stop_requested, true);
}
"""
        harness += function + r"""
int run(const sony_timing_t *timing, const uint64_t *words,
        unsigned failure, unsigned stop) {
  state = 0x21; length = calls = delays = 0;
  fail_at = failure; stop_at = stop;
  atomic_store(&stop_requested, false);
  return gpio_pair_cycle(input, words, timing);
}
size_t output_length(void) { return length; }
const uint8_t *output_data(void) { return output; }
unsigned final_state(void) { return state; }
"""
        path = Path(self.temp.name) / "gpio_pair.c"
        path.write_text(harness)
        library = Path(self.temp.name) / "gpio_pair.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "main"), str(path),
                        str(ROOT / "main/sony_timing.c"), "-o", str(library)], check=True)
        gpio = ctypes.CDLL(str(library))
        gpio.run.argtypes = [ctypes.POINTER(Timing), ctypes.POINTER(ctypes.c_uint64),
                             ctypes.c_uint, ctypes.c_uint]
        gpio.output_length.restype = ctypes.c_size_t
        gpio.output_data.restype = ctypes.POINTER(ctypes.c_uint8)
        timing = self.library.sony_timing_defaults()
        timing.pair, timing.slot = 1, 22
        timing.setup_ns = 1000
        timing.clock_low_ns = timing.clock_high_ns = 2000
        timing.strobe_ns = timing.latch_guard_ns = 5000
        timing.blank_before_ns = timing.blank_after_ns = 5000
        timing.on_ns, timing.off_ns = 20000, 2060000
        words = (ctypes.c_uint64 * 26)()
        words[22], words[21] = 0x02108421C8, 0x07C226422E
        self.assertEqual(gpio.run(ctypes.byref(timing), words, 0, 0), 0)
        wave = bytes(gpio.output_data()[:gpio.output_length()])
        visible = self.visible_words(wave)
        self.assertGreaterEqual(len(visible), 2)
        for index, pair in enumerate(visible):
            slot = 22 - index % 2
            self.assertEqual(pair, (1 << slot, words[slot]))
        for failure, stop in ((50, 0), (0, 10)):
            self.assertEqual(gpio.run(ctypes.byref(timing), words, failure, stop),
                             7 if failure else 0)
            self.assertFalse(gpio.final_state() & 4)
            self.assertFalse(any(value & 4 for value in
                                 gpio.output_data()[:gpio.output_length()]))

    def test_conservative_edge_setup_hold_and_blanking_guards(self):
        timing = self.library.sony_timing_defaults()
        timing.setup_ns = timing.clock_low_ns = timing.clock_high_ns = 250
        timing.strobe_ns = timing.latch_guard_ns = 250
        wave = self.generate(timing, [0xAAAAAAAAAA] * 26)
        lines = [bytes((sample >> bit) & 1 for sample in wave) for bit in range(6)]
        for clock, data in ((0, 1), (5, 4)):
            edges = [m.start() + 1 for m in re.finditer(b"\0\1", lines[clock])]
            for edge in edges:
                self.assertEqual(len(set(lines[data][edge - 2:edge + 2])), 1)
            for match in re.finditer(b"\0+", lines[clock]):
                self.assertGreaterEqual(len(match.group()), 2)
        self.assertFalse(set(m.start() + 1 for m in re.finditer(b"\0\1", lines[0])) &
                         set(m.start() + 1 for m in re.finditer(b"\0\1", lines[5])),
                         "Preload must not switch both translator clock lanes together")
        for match in re.finditer(b"\1+", lines[3]):
            self.assertGreaterEqual(len(match.group()), 2)
            self.assertNotIn(1, lines[2][match.start():match.end() + 16])
        for edge in (m.start() + 1 for m in re.finditer(b"\0\1", lines[2])):
            self.assertNotIn(1, lines[3][edge - 16:edge])

    def test_held_dma_contains_only_clear_at_requested_duty(self):
        for on, off in ((20000, 1980000), (37500, 1000000), (250, 6250)):
            timing = self.library.sony_timing_defaults()
            timing.hold, timing.on_ns, timing.off_ns = 1, on, off
            buffer = (ctypes.c_uint8 * 524288)()
            size = self.library.sony_timing_generate_clear(
                buffer, len(buffer), ctypes.byref(timing))
            self.assertGreater(size, 0)
            self.assertEqual(size % 64, 0)
            wave = bytes(buffer[:size])
            self.assertEqual(set(wave), {0, 4}, "DMA must not drive DATA/CK/STB")
            highs = list(re.finditer(b"\x04+", wave))
            self.assertGreater(len(highs), 1)
            self.assertTrue(all(len(run.group()) * 125 == on for run in highs))
            self.assertTrue(all((b.start() - a.start()) * 125 == on + off
                                for a, b in zip(highs, highs[1:])))
            self.assertEqual(wave[-1], 0, "Alignment padding must stay blanked")
        small = (ctypes.c_uint8 * 10)(*([123] * 10))
        self.assertEqual(self.library.sony_timing_generate_clear(
            small, len(small), ctypes.byref(timing)), 0)
        self.assertEqual(list(small), [123] * 10)
        timing.hold = 0
        self.assertEqual(self.library.sony_timing_generate_clear(
            small, len(small), ctypes.byref(timing)), 0)

    def test_invalid_limits_and_small_buffer_do_not_generate(self):
        for key, value in (("clock_low_ns", 125), ("setup_ns", 20),
                           ("strobe_ns", 70), ("latch_guard_ns", 50),
                           ("blank_after_ns", 1875), ("clock_high_ns", 300),
                           ("pulses", 0), ("pulses", 17), ("slot", 26),
                           ("on_ns", 280125), ("off_ns", 0), ("hold", 2)):
            timing = self.library.sony_timing_defaults()
            setattr(timing, key, value)
            self.assertIsNotNone(self.library.sony_timing_validate(ctypes.byref(timing)))
        timing = self.library.sony_timing_defaults()
        timing.hold = 1
        self.assertIn(b"1/26", self.library.sony_timing_validate(ctypes.byref(timing)))
        timing.off_ns = 25 * timing.on_ns
        self.assertIsNone(self.library.sony_timing_validate(ctypes.byref(timing)))
        timing.hold, timing.pulses, timing.off_ns = 0, 16, 5000000
        self.assertIn(b"capacity", self.library.sony_timing_validate(ctypes.byref(timing)))
        timing = self.library.sony_timing_defaults()
        short = (ctypes.c_uint8 * 10)(*([123] * 10))
        self.assertEqual(self.library.sony_timing_generate(
            short, 10, ctypes.byref(timing), (ctypes.c_uint64 * 26)(), True), 0)
        self.assertEqual(list(short), [123] * 10)


if __name__ == "__main__":
    unittest.main()
