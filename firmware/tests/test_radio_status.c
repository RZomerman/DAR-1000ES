#include "../main/sony_radio_status.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

int main(void) {
  sony_radio_status_t status = sony_radio_status(true, 2, "192", "Pop");
  assert(status.meter == 0xFF && status.audio == 0x2002 && status.stereo == 1);
  status = sony_radio_status(true, 1, "128", "Talk");
  assert(status.meter == 0x3F && status.audio == 0x2011 && status.stereo == 0);
  status = sony_radio_status(false, 2, "320", "Music");
  assert(status.meter == 0x4000 && !status.audio && !status.stereo);
  status = sony_radio_status(true, 0, "192", "Pop");
  assert(status.meter == 0x4000 && !status.audio && !status.stereo);

  const char *invalid[] = {NULL,
                           "",
                           "0",
                           "-1",
                           "192x",
                           "128/256",
                           "999999999999999999999999999999999999999"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    status = sony_radio_status(true, 2, invalid[i], "");
    assert(status.meter == 0x4000 && status.audio == 0x2000 &&
           status.stereo == 1);
  }
  status = sony_radio_status(true, 2, " 64 ", "Top 40");
  assert(status.meter == 7 && status.audio == 0x2002);
  status = sony_radio_status(true, 2, "1", "NEWS / SPORTS");
  assert(status.meter == 1 && status.audio == 0x2001);
  status = sony_radio_status(true, 2, "256", "jAzZ, SOUL");
  assert(status.meter == 0x7FF && status.audio == 0x2002);
  status = sony_radio_status(true, 2, "1000", "R&B");
  assert(status.meter == 0x3FFF && status.audio == 0x2002);
  const char *unknown[] = {NULL,         "",          "Mixed",
                           "Pop / Talk", "newspaper", "variety"};
  for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
    status = sony_radio_status(true, 2, "320", unknown[i]);
    assert(status.audio == 0x2000);
  }
  char long_genre[200];
  memset(long_genre, 'x', sizeof(long_genre));
  memcpy(long_genre + sizeof(long_genre) - 6, " Talk", 6);
  assert(sony_radio_status(true, 2, "192", long_genre).audio == 0x2001);

  uint64_t words[26], before[26];
  for (unsigned i = 0; i < 26; ++i)
    words[i] = before[i] = 0xFFFFFFFFFFULL;
  status = sony_radio_status(true, 1, "192", "Talk");
  sony_radio_status_apply(words, &status);
  assert(words[0] == ((before[0] & ~0x7FFFULL) | 0xFF));
  assert(words[24] == ((before[24] & ~0x2013ULL) | 0x2011));
  assert(words[25] == (before[25] & ~1ULL));
  for (unsigned i = 1; i < 24; ++i)
    assert(words[i] == before[i]);
  status = sony_radio_status(false, 1, "192", "Talk");
  sony_radio_status_apply(words, &status);
  assert((words[0] & 0x7FFF) == 0x4000);
  assert(!(words[24] & 0x2013) && !(words[25] & 1));
  return 0;
}
