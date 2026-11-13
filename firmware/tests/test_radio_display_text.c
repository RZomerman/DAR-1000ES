#include "../main/audio/radio_display_text.h"

#include <assert.h>
#include <string.h>

int main(void) {
  char text[1024];
  radio_display_text(text, sizeof(text), "Artist - Song", "Station", true,
                     false);
  assert(!strcmp(text, "Artist - Song"));
  radio_display_text(text, sizeof(text), "", "Station", true, false);
  assert(!strcmp(text, "Station"));
  radio_display_text(text, sizeof(text), "Stale song", "Station", false, true);
  assert(!strcmp(text, "RECONNECTING - Station"));
  radio_display_text(text, sizeof(text), "", "Station", false, false);
  assert(!strcmp(text, "CONNECTING - Station"));
  radio_display_text(text, sizeof(text), "Bj\xc3\xb6rk - Caf\xc3\xa9", "", true,
                     false);
  assert(!strcmp(text, "Bj\xc3\xb6rk - Caf\xc3\xa9"));
  radio_display_text(text, sizeof(text), "Beyonc\xe9", "", true, false);
  assert(!strcmp(text, "Beyonc\xc3\xa9"));
  radio_display_text(text, sizeof(text), "A\nB\tC", "", true, false);
  assert(!strcmp(text, "A B C"));
  char small[4];
  radio_display_text(small, sizeof(small), "\xc3\xa9\xc3\xa9", "", true, false);
  assert(!strcmp(small, "\xc3\xa9"));
  radio_display_text(small, 1, "Song", "", true, false);
  assert(!small[0]);
  radio_display_text(NULL, 0, "Song", "", true, false);
  char long_title[512];
  memset(long_title, 'A', sizeof(long_title) - 1);
  long_title[sizeof(long_title) - 1] = 0;
  radio_display_text(text, sizeof(text), long_title, "", true, false);
  assert(strlen(text) == 200);
  radio_display_text(text, sizeof(text), "\xf0\x9f", "", true, false);
  assert(!strcmp(text, "\xc3\xb0\xc2\x9f"));
  return 0;
}
