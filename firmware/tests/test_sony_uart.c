#include <assert.h>
#include <string.h>
#include "sony_uart_protocol.h"

static unsigned feed(sony_uart_parser_t *parser, const uint8_t *bytes,
                     size_t size, sony_uart_message_t *message) {
  unsigned count = 0;
  for (size_t i = 0; i < size; ++i)
    count += sony_uart_feed(parser, bytes[i], message);
  return count;
}

static void check_metadata(void) {
  sony_uart_metadata_t sent = {
      .codec = "AAC", .bitrate = "128", .genre = "Pop", .ip = "192.0.2.59",
      .url = "https://example.test/"};
  memset(sent.url + strlen(sent.url), 'x', sizeof(sent.url) - strlen(sent.url) - 1);
  sony_uart_metadata_t staging = {0};
  unsigned next = SONY_UART_METADATA_PARTS;
  sony_uart_parser_t parser = {0};
  for (unsigned pass = 0; pass < 3; ++pass) {
    if (pass == 2) sent.url[sizeof(sent.url) - 1] = 'x';
    for (unsigned part = 0; part < SONY_UART_METADATA_PARTS; ++part) {
      if (pass == 0 && part == 4) continue;
      sony_uart_message_t message = {.type = SONY_UART_METADATA0 + part, .sequence = 123};
      for (unsigned i = 0; i < 4; ++i)
        message.value |= (uint32_t)((const unsigned char *)&sent)[part * 4 + i] << (8 * i);
      uint8_t frame[SONY_UART_FRAME_SIZE];
      sony_uart_message_t output;
      sony_uart_encode(&message, frame);
      assert(feed(&parser, frame, sizeof(frame), &output) == 1);
      assert(output.sequence == 123);
      bool complete = sony_uart_metadata_feed(&staging, &next, &output);
      assert(complete == (pass == 1 && part + 1 == SONY_UART_METADATA_PARTS));
      if (complete) assert(!memcmp(&sent, &staging, sizeof(sent)));
    }
  }
}

int main(void) {
  check_metadata();
  sony_uart_parser_t parser = {0};
  sony_uart_message_t output;
  for (uint32_t cue = SONY_STARTUP_SOUND_CANCEL; cue <= SONY_STARTUP_SOUND_PLAY; cue++) {
    uint8_t startup_frame[SONY_UART_FRAME_SIZE];
    sony_uart_message_t startup = {.type = SONY_UART_STARTUP, .value = cue};
    sony_uart_encode(&startup, startup_frame);
    assert(feed(&parser, startup_frame, sizeof(startup_frame), &output) == 1);
    assert(output.type == SONY_UART_STARTUP && !output.sequence && output.value == cue);
  }
  sony_uart_message_t ping = {
      .type = SONY_UART_PING, .sequence = 0x78563412, .value = 0x434f4e54};
  uint8_t frame[SONY_UART_FRAME_SIZE];
  sony_uart_encode(&ping, frame);
  assert(frame[4] == 0x12 && frame[7] == 0x78);
  const uint8_t noise[] = {0, 0xff, 0xa5, 0xa5, 0, 0x5a, 1};
  assert(!feed(&parser, noise, sizeof(noise), &output));
  assert(!feed(&parser, frame, 7, &output));
  assert(feed(&parser, frame + 7, sizeof(frame) - 7, &output) == 1);
  assert(output.type == ping.type && output.sequence == ping.sequence &&
         output.value == ping.value);

  frame[8] ^= 1;
  assert(!feed(&parser, frame, sizeof(frame), &output));
  assert(parser.bad_frames == 1);
  frame[8] ^= 1;
  assert(feed(&parser, frame, sizeof(frame), &output) == 1);

  assert(!feed(&parser, frame, 8, &output));
  assert(feed(&parser, frame, sizeof(frame), &output) == 1);
  assert(parser.bad_frames == 2);
  assert(feed(&parser, frame, sizeof(frame), &output) == 1);
  assert(feed(&parser, frame, sizeof(frame), &output) == 1);

  sony_uart_message_t ack = {
      .type = SONY_UART_ACK, .sequence = UINT32_MAX, .value = 15};
  sony_uart_encode(&ack, frame);
  assert(feed(&parser, frame, sizeof(frame), &output) == 1);
  assert(output.sequence == UINT32_MAX && output.value == 15 &&
         output.type == SONY_UART_ACK);
  for (unsigned i = 0; i < 10000; ++i)
    sony_uart_feed(&parser, (uint8_t)(i * 73), &output);
  assert(feed(&parser, frame, sizeof(frame), &output) == 1);
  return 0;
}
