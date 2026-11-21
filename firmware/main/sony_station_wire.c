#include "sony_station_wire.h"

#include <string.h>

static size_t bounded_length(const char *text, size_t capacity) {
  size_t n = 0;
  while (n < capacity && text[n])
    ++n;
  return n;
}

static uint16_t crc16(const uint8_t *data, size_t size) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < size; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
  }
  return crc;
}

static bool valid_id(const char *id, size_t length) {
  if (!length)
    return true;
  if (length != 36)
    return false;
  for (size_t i = 0; i < length; ++i) {
    char ch = id[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (ch != '-')
        return false;
    } else if (!((ch >= '0' && ch <= '9') ||
                 (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
      return false;
    }
  }
  return true;
}

static bool valid_url(const char *url, size_t length) {
  size_t scheme = length >= 7 && !memcmp(url, "http://", 7) ? 7 :
                  length >= 8 && !memcmp(url, "https://", 8) ? 8 : 0;
  if (!scheme || scheme == length || url[scheme] == '/')
    return false;
  for (size_t i = scheme; i < length; ++i) {
    unsigned char ch = url[i];
    if (ch <= 32 || ch >= 127 || ch == '\\' || ch == '#' || ch == '@')
      return false;
  }
  return true;
}

size_t sony_station_encode(const sony_station_command_t *station,
                           uint8_t *output, size_t capacity) {
  if (!station || !output || !station->generation)
    return 0;
  size_t id_len = bounded_length(station->id, sizeof(station->id));
  size_t name_len = bounded_length(station->name, sizeof(station->name));
  size_t url_len = bounded_length(station->url, sizeof(station->url));
  if (id_len > SONY_STATION_ID_MAX || !name_len || name_len > SONY_STATION_NAME_MAX ||
      url_len > SONY_STATION_URL_MAX || !valid_id(station->id, id_len) ||
      !valid_url(station->url, url_len))
    return 0;
  size_t size = 14 + id_len + name_len + url_len;
  if (capacity < size)
    return 0;
  output[0] = 0xa5;
  output[1] = 0x5a;
  output[2] = 2;
  output[3] = 1;
  for (unsigned i = 0; i < 4; ++i)
    output[4 + i] = (uint8_t)(station->generation >> (8 * i));
  output[8] = (uint8_t)id_len;
  output[9] = (uint8_t)name_len;
  output[10] = (uint8_t)url_len;
  output[11] = (uint8_t)(url_len >> 8);
  size_t offset = 12;
  memcpy(output + offset, station->id, id_len);
  offset += id_len;
  memcpy(output + offset, station->name, name_len);
  offset += name_len;
  memcpy(output + offset, station->url, url_len);
  offset += url_len;
  uint16_t crc = crc16(output, offset);
  output[offset] = (uint8_t)crc;
  output[offset + 1] = (uint8_t)(crc >> 8);
  return size;
}

bool sony_station_decode(const uint8_t *frame, size_t size,
                         sony_station_command_t *station) {
  if (!frame || !station || size < 14 || size > SONY_STATION_WIRE_MAX ||
      frame[0] != 0xa5 || frame[1] != 0x5a || frame[2] != 2 || frame[3] != 1)
    return false;
  size_t id_len = frame[8], name_len = frame[9];
  size_t url_len = (size_t)frame[10] | (size_t)frame[11] << 8;
  if (id_len > SONY_STATION_ID_MAX || !name_len ||
      name_len > SONY_STATION_NAME_MAX || url_len > SONY_STATION_URL_MAX ||
      size != 14 + id_len + name_len + url_len ||
      crc16(frame, size - 2) != ((uint16_t)frame[size - 2] | (uint16_t)frame[size - 1] << 8))
    return false;
  const char *id = (const char *)frame + 12;
  const char *name = id + id_len;
  const char *url = name + name_len;
  if (!valid_id(id, id_len) || memchr(name, 0, name_len) ||
      !valid_url(url, url_len))
    return false;
  uint32_t generation = 0;
  for (unsigned i = 0; i < 4; ++i)
    generation |= (uint32_t)frame[4 + i] << (8 * i);
  if (!generation)
    return false;
  memset(station, 0, sizeof(*station));
  station->generation = generation;
  memcpy(station->id, id, id_len);
  memcpy(station->name, name, name_len);
  memcpy(station->url, url, url_len);
  return true;
}

bool sony_station_feed(sony_station_parser_t *parser, uint8_t byte,
                       sony_station_command_t *station) {
  if (!parser || !station)
    return false;
  if (parser->used >= sizeof(parser->bytes))
    parser->used = 0;
  parser->bytes[parser->used++] = byte;
  while (parser->used) {
    const uint8_t *data = parser->bytes;
    bool invalid = data[0] != 0xa5 ||
        (parser->used > 1 && data[1] != 0x5a) ||
        (parser->used > 2 && data[2] != 2) ||
        (parser->used > 3 && data[3] != 1);
    if (!invalid && parser->used >= 12) {
      size_t id_len = data[8], name_len = data[9];
      size_t url_len = (size_t)data[10] | (size_t)data[11] << 8;
      invalid = id_len > SONY_STATION_ID_MAX || !name_len ||
                name_len > SONY_STATION_NAME_MAX || !url_len ||
                url_len > SONY_STATION_URL_MAX;
      if (!invalid) {
        size_t expected = 14 + id_len + name_len + url_len;
        if (parser->used < expected)
          return false;
        if (sony_station_decode(data, expected, station)) {
          parser->used = 0;
          return true;
        }
        invalid = true;
      }
    } else if (!invalid) {
      return false;
    }
    if (invalid) {
      if (data[0] == 0xa5 && parser->used > 1 && data[1] == 0x5a)
        ++parser->bad_frames;
      memmove(parser->bytes, parser->bytes + 1, --parser->used);
    }
  }
  return false;
}
