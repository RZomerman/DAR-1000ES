#pragma once

#include "esp_err.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SONY_SEARCH_DEFAULT_COUNTRY "NL"

typedef struct {
  char name[64];
  uint8_t uuid[16];
} sony_search_entry_t;

typedef struct {
  sony_search_entry_t *entries;
  size_t count;
  char country[3];
} sony_search_index_t;

typedef void (*sony_search_progress_t)(size_t count, void *context);

esp_err_t sony_search_index_load(sony_search_index_t *index, const char *country,
                                const atomic_uint *epoch, unsigned expected_epoch,
                                sony_search_progress_t progress, void *context);
void sony_search_index_uuid(const sony_search_entry_t *entry, char uuid[37]);
void sony_search_index_range(const sony_search_index_t *index, const char *prefix,
                             size_t *first, size_t *count);
void sony_search_index_chars(const sony_search_index_t *index, const char *prefix,
                             bool available[128]);
