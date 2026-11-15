#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SONY_UART_FRAME_SIZE 14
enum { SONY_UART_PING = 1, SONY_UART_PONG, SONY_UART_MAP, SONY_UART_ACK };

typedef struct {
  uint8_t type;
  uint32_t sequence;
  uint32_t value;
} sony_uart_message_t;

typedef struct {
  uint8_t bytes[SONY_UART_FRAME_SIZE];
  size_t used;
  uint32_t bad_frames;
} sony_uart_parser_t;

void sony_uart_encode(const sony_uart_message_t *message,
                      uint8_t output[SONY_UART_FRAME_SIZE]);
bool sony_uart_feed(sony_uart_parser_t *parser, uint8_t byte,
                    sony_uart_message_t *message);
