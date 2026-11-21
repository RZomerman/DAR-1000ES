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
  assert(status.meter == 0x4000 && status.audio == 2 && !status.stereo);

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
  const char *unknown[] = {NULL, "", "newspaper", "variety"};
  for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
    status = sony_radio_status(true, 2, "320", unknown[i]);
    assert(status.audio == 0x2000);
  }
  char long_genre[200];
  memset(long_genre, 'x', sizeof(long_genre));
  memcpy(long_genre + sizeof(long_genre) - 6, " Talk", 6);
  assert(sony_radio_status(true, 2, "192", long_genre).audio == 0x2001);

  uint64_t words[26], before[26];
  memset(words, 0, sizeof(words));
  status = sony_radio_status(false, 0, "", "");
  sony_radio_controller_indicators(words, &status, false, false, false, false, false);
  assert(words[23] == 0x08);
  sony_radio_controller_indicators(words, &status, false, false, true, false, false);
  assert(words[23] == 0x09);
  sony_radio_controller_indicators(words, &status, false, false, true, true, false);
  assert(words[23] == 0x0B);
  status = sony_radio_status(true, 2, "128", "Pop / Talk");
  assert((status.audio & 3) == 3);
  assert(sony_radio_genre_labels("Mixed") == 3);
  status.preset = 16;
  words[24] = 0x600;
  words[25] = 0x101;
  sony_radio_controller_indicators(words, &status, true, true, true, true, false);
  assert(words[23] == 0x200007 && words[24] == 3 && words[25] == 0x103);
  status.preset = 1;
  sony_radio_controller_indicators(words, &status, false, false, true, false, false);
  assert(words[23] == 0x41 && words[25] == 0x101);
  memcpy(before, words, sizeof(words));
  words[25] |= 0x800220;
  sony_radio_controller_indicators(words, &status, false, false, true, true, true);
  assert(words[23] == 0x03 && words[24] == 0x5003 && words[25] == 0x1DD);
  assert(status.preset == 1);
  memcpy(words, before, sizeof(words));  // Next scan starts from the base frame.
  sony_radio_controller_indicators(words, &status, false, false, true, true, false);
  assert(words[23] == 0x43 && words[24] == 3 && words[25] == 0x101);

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
  for (unsigned source = 0; source < 3; ++source) {
    status = sony_radio_status(source != 2, source == 1 ? 1 : 2, "192", "Pop");
    memcpy(words, before, sizeof(words));
    sony_radio_status_apply_view(words, &status, true);
    assert(!(words[24] & 0x80C));
    assert((words[24] & 0x2013) == status.audio);
    assert((words[0] & 0x7FFF) == (status.meter & 0x4000));
    assert((words[25] & 1) == status.stereo);
    assert((words[24] & ~0x281FULL) == (before[24] & ~0x281FULL));
    for (unsigned i = 1; i < 24; ++i)
      assert(words[i] == before[i]);
    sony_radio_status_apply_view(words, &status, false);
    assert(!(words[24] & 0x80C));
    assert((words[0] & 0x7FFF) == status.meter);
    assert((words[24] & 0x2013) == status.audio);
    assert((words[25] & 1) == status.stereo);
  }
  return 0;
}
