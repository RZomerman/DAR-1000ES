#include "sony_uart_protocol.h"
#include <string.h>

static uint16_t checksum(const uint8_t *data, size_t size) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < size; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
  }
  return crc;
}

void sony_uart_encode(const sony_uart_message_t *message,
                      uint8_t output[SONY_UART_FRAME_SIZE]) {
  output[0] = 0xa5;
  output[1] = 0x5a;
  output[2] = 1;
  output[3] = message->type;
  for (unsigned i = 0; i < 4; ++i) {
    output[4 + i] = (uint8_t)(message->sequence >> (8 * i));
    output[8 + i] = (uint8_t)(message->value >> (8 * i));
  }
  uint16_t crc = checksum(output, 12);
  output[12] = (uint8_t)crc;
  output[13] = (uint8_t)(crc >> 8);
}

bool sony_uart_feed(sony_uart_parser_t *parser, uint8_t byte,
                    sony_uart_message_t *message) {
  parser->bytes[parser->used++] = byte;
  while (parser->used && (parser->bytes[0] != 0xa5 ||
         (parser->used > 1 && parser->bytes[1] != 0x5a))) {
    memmove(parser->bytes, parser->bytes + 1, --parser->used);
  }
  if (parser->used < SONY_UART_FRAME_SIZE)
    return false;
  const uint8_t *data = parser->bytes;
  uint16_t received = (uint16_t)data[12] | (uint16_t)data[13] << 8;
  if (data[2] != 1 || data[3] < SONY_UART_PING || data[3] > SONY_UART_ACK ||
      checksum(data, 12) != received) {
    ++parser->bad_frames;
    memmove(parser->bytes, parser->bytes + 1, --parser->used);
    return false;
  }
  *message = (sony_uart_message_t){.type = data[3]};
  for (unsigned i = 0; i < 4; ++i) {
    message->sequence |= (uint32_t)data[4 + i] << (8 * i);
    message->value |= (uint32_t)data[8 + i] << (8 * i);
  }
  parser->used = 0;
  return true;
}
