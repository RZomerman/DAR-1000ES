import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class UartProtocolTests(unittest.TestCase):
    def test_controller_telemetry_generation_and_stale_meters(self):
        source = (ROOT / "main/sony_uart_link.c").read_text()
        state = source[source.index("static sony_audio_link_status_t audio_status;"):
                       source.index("uint32_t sony_uart_link_request_check(void)")]
        header = (ROOT / "main/sony_uart_link.h").read_text()
        status_type = header[header.index("typedef struct {"):
                             header.index("esp_err_t sony_uart_link_select_station")]
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sony_uart_protocol.h"
#include "sony_station_wire.h"
static int state_lock;
static int64_t now;
static int64_t esp_timer_get_time(void) { return now; }
#define portENTER_CRITICAL(lock) (void)(lock)
#define portEXIT_CRITICAL(lock) (void)(lock)
""" + status_type + state + r"""
int main(void) {
    sony_audio_link_status_t out;
    sony_station_command_t saving;
    sony_uart_link_audio_status(&out);
    assert(!out.connected);
    now = last_pong_us = 100000;
    sony_uart_link_audio_status(&out);
    assert(out.connected && !out.generation && !out.playing);
    sony_uart_message_t msg = {.type=SONY_UART_ACK, .sequence=41, .value=0x53544100};
    audio_status.generation = 42;
    current_station = (sony_station_command_t){.generation = 42, .name = "On Air"};
    accept_audio_message(&msg);
    assert(!audio_status.acknowledged);
    assert(!sony_uart_link_current_station(&saving));
    msg.sequence = 42;
    accept_audio_message(&msg);
    assert(audio_status.acknowledged);
    now = 100000;
    msg.type = SONY_UART_AUDIO_STATE; msg.value = 1 | (2 << 8) | (2 << 16);
    accept_audio_message(&msg);
    msg.type = SONY_UART_EQ0; msg.value = 0x12345670;
    accept_audio_message(&msg);
    msg.type = SONY_UART_RMS; msg.value = (2000U << 16) | 1000;
    accept_audio_message(&msg);
    sony_uart_link_audio_status(&out);
    assert(out.fresh && out.playing && out.channels == 2 && out.sample_rate == 44100);
    assert(sony_uart_link_current_station(&saving) && !strcmp(saving.name, "On Air"));
    assert(out.levels[1] == 7 && out.rms[0] == 1000 && out.rms[1] == 2000);
    for (unsigned i = 0; i < 16; ++i) {
      msg.type = SONY_UART_TITLE0 + i;
      msg.value = i ? 0 : 0x74736554;
      accept_audio_message(&msg);
    }
    sony_uart_link_audio_status(&out);
    assert(!strcmp(out.title, "Test"));
    now = 1200000;
    sony_uart_link_audio_status(&out);
    assert(out.fresh && out.playing && !out.levels[1] && !out.rms[0]);
    now = 2200000;
    sony_uart_link_audio_status(&out);
    assert(!out.fresh && !out.playing && out.connected);
    assert(!sony_uart_link_current_station(&saving));
    now = 3100000;
    sony_uart_link_audio_status(&out);
    assert(!out.connected);
    audio_status.generation = 0;
    msg = (sony_uart_message_t){.type=SONY_UART_AUDIO_STATE, .value=4};
    accept_audio_message(&msg);
    sony_uart_link_audio_status(&out);
    assert(out.airplay_active && !out.generation && !out.fresh);
    msg.value = 0;
    accept_audio_message(&msg);
    sony_uart_link_audio_status(&out);
    assert(!out.airplay_active);
    msg.value = 4;
    accept_audio_message(&msg);
    now += 3000000;
    sony_uart_link_audio_status(&out);
    assert(!out.airplay_active);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "telemetry.c"
            path.write_text(harness)
            binary = Path(temporary) / "telemetry"
            subprocess.run(
                ["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-I", str(ROOT / "main"), str(path),
                 str(ROOT / "main/sony_uart_protocol.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_station_command_roundtrip_and_malformed_rejection(self):
        source = r"""
#include "sony_station_wire.h"
#include <assert.h>
#include <string.h>

int main(void) {
    sony_station_command_t in = {
        .generation = 0x12345678,
        .id = "01234567-89ab-cdef-0123-456789abcdef",
        .name = "Radio Example",
        .url = "https://example.com/live.mp3"
    }, out;
    uint8_t bytes[SONY_STATION_WIRE_MAX];
    size_t n = sony_station_encode(&in, bytes, sizeof(bytes));
    assert(n && !sony_station_encode(&in, bytes, n - 1));
    assert(sony_station_decode(bytes, n, &out));
    assert(out.generation == in.generation && !strcmp(in.id, out.id) &&
           !strcmp(in.name, out.name) && !strcmp(in.url, out.url));
    sony_station_parser_t parser = {0};
    assert(!sony_station_feed(&parser, 0x11, &out));
    assert(!sony_station_feed(&parser, 0xa5, &out));
    for (size_t i = 0; i < n; ++i)
        assert(sony_station_feed(&parser, bytes[i], &out) == (i == n - 1));
    assert(!strcmp(out.name, in.name));
    bytes[n - 1] ^= 1;
    for (size_t i = 0; i < n; ++i)
        assert(!sony_station_feed(&parser, bytes[i], &out));
    assert(parser.bad_frames);
    bytes[n - 1] ^= 1;
    for (size_t i = 0; i < n; ++i)
        assert(sony_station_feed(&parser, bytes[i], &out) == (i == n - 1));
    assert(!sony_station_decode(bytes, n - 1, &out));
    bytes[n - 3] ^= 1;
    assert(!sony_station_decode(bytes, n, &out));
    bytes[n - 3] ^= 1;
    bytes[2] = 3;
    assert(!sony_station_decode(bytes, n, &out));
    in.id[0] = 0;
    n = sony_station_encode(&in, bytes, sizeof(bytes));
    assert(n && sony_station_decode(bytes, n, &out) && !out.id[0]);
    strcpy(in.url, "http://example.com/stream");
    assert(sony_station_encode(&in, bytes, sizeof(bytes)));
    strcpy(in.url, "https://user@example.com/stream");
    assert(!sony_station_encode(&in, bytes, sizeof(bytes)));
    strcpy(in.url, "https://example.com/a#b");
    assert(!sony_station_encode(&in, bytes, sizeof(bytes)));
    strcpy(in.url, "https://example.com/live");
    in.generation = 0;
    assert(!sony_station_encode(&in, bytes, sizeof(bytes)));
    in.generation = 1;
    strcpy(in.id, "not-a-uuid");
    assert(!sony_station_encode(&in, bytes, sizeof(bytes)));
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "station.c"
            path.write_text(source)
            binary = Path(temporary) / "station"
            subprocess.run(
                ["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-I", str(ROOT / "main"), str(path),
                 str(ROOT / "main/sony_station_wire.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_fragmentation_noise_corruption_and_resynchronization(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "uart"
            subprocess.run(
                ["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-I", str(ROOT / "main"), str(ROOT / "tests/test_sony_uart.c"),
                 str(ROOT / "main/sony_uart_protocol.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
