# Music-note bitmap

A photo-derived music-note symbol for a single 5x7 text character on the
DAR-1000ES display. Fourteen lit pixels were visually counted from the
close-up reference. **Not extracted from a Sony bus capture** and not yet
validated on the physical display.

## Bitmap

`#` is lit; `.` is unlit. Rows run top to bottom, columns left to right.

```text
..#..
..##.
..#.#
..#.#
.##..
###..
##...
```

## Encoding

Use the text-matrix convention established from Frequency and No DSR:

```text
bit = row * 5 + column

 0  1  2  3  4
 5  6  7  8  9
10 11 12 13 14
15 16 17 18 19
20 21 22 23 24
25 26 27 28 29
30 31 32 33 34
```

Coordinates are zero-based. The resulting complete word is:

```text
40-bit hexadecimal: 0x00CE6A5184
40-bit binary:      00000000 11001110 01101010 01010001 10000100
Lit bits:           2, 7, 8, 12, 14, 17, 19, 21, 22, 25, 26, 27, 30, 31
```

Bits 35-39 are zero. Transmit bit 39 first, ending with bit 0, using the
normal 40-clock segment transfer. Bit 39 is not an ON/OFF command.
Latch and blanking timing remain the same as for text characters.

## Placement and limitations

Place this word in any **mapped text slot**: message slots 14-22,
frequency slots 6-10 or suffix slots 3-5. Slot 22 is the leftmost message
character. Continuous scanning is required; setting a word once is not
enough to sustain the screen.

This is a pixel glyph, not the fixed MUSIC annunciator or a red slot-25
pattern. The photo's full phrase "Network Audio Player" exceeds the currently
mapped nine-character message field; it does not prove that all character
positions on our hardware are mapped.

The local editor now includes this design as U+266A EIGHTH NOTE.
This Unicode font update has not been deployed to the ESP.
Verify orientation with a safe live pixel test before claiming the pictured
appearance on the actual VFD.

See the [display-driver specification](08-esp-display-driver-specification.md)
for slot mapping, original Sony glyphs, derived font and scan timing.

## Special symbols and song-title characters

The [bitmap catalogue](10-display-bitmap-catalogue.md) now includes individual
records for music notes, quotation marks, ampersands, parentheses, accented
vowels, umlauts, cedillas, slashed letters, sharp s and other title symbols.
Apart from this photo-derived note, the additions are generated approximations,
not original Sony captures. The local editor supports these records.

Each entry records its Unicode code point, name, 5x7 bitmap, 40-bit HEX
word and provenance (captured, photo-derived or generated). Preserve original
Sony glyphs where available. Normalize equivalent composed/decomposed Unicode
forms before lookup; where no glyph exists, use a documented transliteration
or replacement symbol rather than silently dropping the character.
Lookup normalizes NFC and uses one cell per grapheme. Unlisted decomposable
Latin accents transliterate to the base letter; other unsupported graphemes
use a visible asterisk (U+002A). Listed symbols with a presentation selector use
the same monochrome bitmap; unsupported emoji sequences use one asterisk.

The 5x7 grid limits accent and symbol detail. This music-note bitmap is one
stored design, not a verified encoding for every Unicode musical-note symbol.
