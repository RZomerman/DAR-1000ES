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

int main(void) {
  sony_uart_parser_t parser = {0};
  sony_uart_message_t output;
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
