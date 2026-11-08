# Complete 5x7 display bitmap catalogue

Every character below includes its complete bitmap and 40-bit hexadecimal
word. No additional reference font is needed to use these patterns.
**Captured Sony** means the word was extracted from an original display
capture. **Generated fallback** means an application-designed glyph, not a
claimed original Sony shape. Red letters/digits and fixed indicators are
separate fields and must not use these text patterns.

## Encoding and full control

Rows run top to bottom; columns run left to right. `#` is lit and `.` is
unlit. With zero-based coordinates, `bit = row * 5 + column`:

```text
 0  1  2  3  4
 5  6  7  8  9
10 11 12 13 14
15 16 17 18 19
20 21 22 23 24
25 26 27 28 29
30 31 32 33 34
```

Set or clear each bit independently. Bits 35-39 are zero in these glyphs;
they are not established ON/OFF flags. Transmit all 40 bits, **bit 39 first**.
The output latch holds the previous pattern during normal shifting; STB
and grid CLEAR timing expose the completed glyph under the selected grid.
Continuous 26-slot scanning is still required.

Mapped text slots are message 14-22, frequency 6-10 and suffix 3-5.
Message positions left to right correspond to slots 22 down to 14.
Individual live pixel orientation remains to be validated.

Case matters. Missing captured characters use the generated shapes listed
here. Identical generated shapes, such as O and 0, are font-design choices.

## Visual character grid

Each cell shows the character, its exact 5x7 rendering, complete 40-bit HEX
word and source. Cyan pixels are lit; dark pixels are unlit. This chart uses
our current glyphs, not a reproduction of the reference website's font.
The music note is photo-derived; additional Unicode glyphs remain planned.

![Character grid with 5x7 bitmaps, HEX values and provenance](media/display-bitmap-chart.svg)

[Open the full-size character chart](media/display-bitmap-chart.svg).

## Character HEX index

| Character | 40-bit HEX | Source |
|-----------|------------|--------|
| A | `0x0463F8C62E` | Generated fallback |
| B | `0x03E317C62F` | Generated fallback |
| C | `0x03A210862E` | Generated fallback |
| D | `0x03E5294A4F` | Captured Sony |
| E | `0x07C217843F` | Generated fallback |
| F | `0x004217843F` | Captured Sony |
| G | `0x07A31E862E` | Generated fallback |
| H | `0x04631FC631` | Captured Sony |
| I | `0x038842108E` | Generated fallback |
| J | `0x019284211C` | Generated fallback |
| K | `0x0452519531` | Generated fallback |
| L | `0x07C2108421` | Generated fallback |
| M | `0x04631AD771` | Captured Sony |
| N | `0x04639ACE31` | Captured Sony |
| O | `0x03A318C62E` | Generated fallback |
| P | `0x004217C62F` | Generated fallback |
| Q | `0x059358C62E` | Generated fallback |
| R | `0x045257C62F` | Captured Sony |
| S | `0x03A307062E` | Captured Sony |
| T | `0x010842109F` | Generated fallback |
| U | `0x03A318C631` | Generated fallback |
| V | `0x011518C631` | Generated fallback |
| W | `0x02AB5AC631` | Generated fallback |
| X | `0x0462A22A31` | Generated fallback |
| Y | `0x0108454631` | Generated fallback |
| Z | `0x07C222221F` | Generated fallback |
| a | `0x07A3E83800` | Generated fallback |
| b | `0x03E319B421` | Generated fallback |
| c | `0x078210F800` | Captured Sony |
| d | `0x07A31CDA10` | Generated fallback |
| e | `0x0783F8B800` | Captured Sony |
| f | `0x0084238A4C` | Generated fallback |
| g | `0x03A1E8C7C0` | Generated fallback |
| h | `0x046319B421` | Generated fallback |
| i | `0x0388421804` | Generated fallback |
| j | `0x0192843008` | Generated fallback |
| k | `0x024A32A421` | Generated fallback |
| l | `0x0388421086` | Generated fallback |
| m | `0x04635AAC00` | Generated fallback |
| n | `0x046319B400` | Captured Sony |
| o | `0x03A318B800` | Captured Sony |
| p | `0x0042F8BC00` | Generated fallback |
| q | `0x043D18F800` | Captured Sony |
| r | `0x0084236800` | Captured Sony |
| s | `0x03E0E0B800` | Generated fallback |
| t | `0x0324211C42` | Generated fallback |
| u | `0x05B318C400` | Captured Sony |
| v | `0x011518C400` | Generated fallback |
| w | `0x02AB58C400` | Generated fallback |
| x | `0x0454454400` | Generated fallback |
| y | `0x03A1E8C400` | Captured Sony |
| z | `0x07C4447C00` | Captured Sony |
| 0 | `0x03A318C62E` | Captured Sony |
| 1 | `0x02108421C8` | Captured Sony |
| 2 | `0x07C226422E` | Captured Sony |
| 3 | `0x03A306422E` | Captured Sony |
| 4 | `0x0211F4A988` | Generated fallback |
| 5 | `0x03A3083C3F` | Generated fallback |
| 6 | `0x03A317844C` | Generated fallback |
| 7 | `0x010844221F` | Captured Sony |
| 8 | `0x03A317462E` | Captured Sony |
| 9 | `0x01910F462E` | Generated fallback |
| SPACE | `0x0000000000` | Captured Sony |
| . | `0x0080000000` | Captured Sony |
| - | `0x00000F8000` | Generated fallback |
| ? | `0x010044422E` | Generated fallback |
| ! | `0x0100421084` | Generated fallback |
| : | `0x000C6018C0` | Generated fallback |
| / | `0x0002222200` | Generated fallback |
| + | `0x00084F9080` | Generated fallback |

## Uppercase A-Z

### A

**HEX:** `0x0463F8C62E` — Generated fallback

```text
.###.
#...#
#...#
#...#
#####
#...#
#...#
```

### B

**HEX:** `0x03E317C62F` — Generated fallback

```text
####.
#...#
#...#
####.
#...#
#...#
####.
```

### C

**HEX:** `0x03A210862E` — Generated fallback

```text
.###.
#...#
#....
#....
#....
#...#
.###.
```

### D

**HEX:** `0x03E5294A4F` — Captured Sony

```text
####.
.#..#
.#..#
.#..#
.#..#
.#..#
####.
```

### E

**HEX:** `0x07C217843F` — Generated fallback

```text
#####
#....
#....
####.
#....
#....
#####
```

### F

**HEX:** `0x004217843F` — Captured Sony

```text
#####
#....
#....
####.
#....
#....
#....
```

### G

**HEX:** `0x07A31E862E` — Generated fallback

```text
.###.
#...#
#....
#.###
#...#
#...#
.####
```

### H

**HEX:** `0x04631FC631` — Captured Sony

```text
#...#
#...#
#...#
#####
#...#
#...#
#...#
```

### I

**HEX:** `0x038842108E` — Generated fallback

```text
.###.
..#..
..#..
..#..
..#..
..#..
.###.
```

### J

**HEX:** `0x019284211C` — Generated fallback

```text
..###
...#.
...#.
...#.
...#.
#..#.
.##..
```

### K

**HEX:** `0x0452519531` — Generated fallback

```text
#...#
#..#.
#.#..
##...
#.#..
#..#.
#...#
```

### L

**HEX:** `0x07C2108421` — Generated fallback

```text
#....
#....
#....
#....
#....
#....
#####
```

### M

**HEX:** `0x04631AD771` — Captured Sony

```text
#...#
##.##
#.#.#
#.#.#
#...#
#...#
#...#
```

### N

**HEX:** `0x04639ACE31` — Captured Sony

```text
#...#
#...#
##..#
#.#.#
#..##
#...#
#...#
```

### O

**HEX:** `0x03A318C62E` — Generated fallback

```text
.###.
#...#
#...#
#...#
#...#
#...#
.###.
```

### P

**HEX:** `0x004217C62F` — Generated fallback

```text
####.
#...#
#...#
####.
#....
#....
#....
```

### Q

**HEX:** `0x059358C62E` — Generated fallback

```text
.###.
#...#
#...#
#...#
#.#.#
#..#.
.##.#
```

### R

**HEX:** `0x045257C62F` — Captured Sony

```text
####.
#...#
#...#
####.
#.#..
#..#.
#...#
```

### S

**HEX:** `0x03A307062E` — Captured Sony

```text
.###.
#...#
#....
.###.
....#
#...#
.###.
```

### T

**HEX:** `0x010842109F` — Generated fallback

```text
#####
..#..
..#..
..#..
..#..
..#..
..#..
```

### U

**HEX:** `0x03A318C631` — Generated fallback

```text
#...#
#...#
#...#
#...#
#...#
#...#
.###.
```

### V

**HEX:** `0x011518C631` — Generated fallback

```text
#...#
#...#
#...#
#...#
#...#
.#.#.
..#..
```

### W

**HEX:** `0x02AB5AC631` — Generated fallback

```text
#...#
#...#
#...#
#.#.#
#.#.#
#.#.#
.#.#.
```

### X

**HEX:** `0x0462A22A31` — Generated fallback

```text
#...#
#...#
.#.#.
..#..
.#.#.
#...#
#...#
```

### Y

**HEX:** `0x0108454631` — Generated fallback

```text
#...#
#...#
#...#
.#.#.
..#..
..#..
..#..
```

### Z

**HEX:** `0x07C222221F` — Generated fallback

```text
#####
....#
...#.
..#..
.#...
#....
#####
```

## Lowercase a-z

### a

**HEX:** `0x07A3E83800` — Generated fallback

```text
.....
.....
.###.
....#
.####
#...#
.####
```

### b

**HEX:** `0x03E319B421` — Generated fallback

```text
#....
#....
#.##.
##..#
#...#
#...#
####.
```

### c

**HEX:** `0x078210F800` — Captured Sony

```text
.....
.....
.####
#....
#....
#....
.####
```

### d

**HEX:** `0x07A31CDA10` — Generated fallback

```text
....#
....#
.##.#
#..##
#...#
#...#
.####
```

### e

**HEX:** `0x0783F8B800` — Captured Sony

```text
.....
.....
.###.
#...#
#####
#....
.####
```

### f

**HEX:** `0x0084238A4C` — Generated fallback

```text
..##.
.#..#
.#...
###..
.#...
.#...
.#...
```

### g

**HEX:** `0x03A1E8C7C0` — Generated fallback

```text
.....
.####
#...#
#...#
.####
....#
.###.
```

### h

**HEX:** `0x046319B421` — Generated fallback

```text
#....
#....
#.##.
##..#
#...#
#...#
#...#
```

### i

**HEX:** `0x0388421804` — Generated fallback

```text
..#..
.....
.##..
..#..
..#..
..#..
.###.
```

### j

**HEX:** `0x0192843008` — Generated fallback

```text
...#.
.....
..##.
...#.
...#.
#..#.
.##..
```

### k

**HEX:** `0x024A32A421` — Generated fallback

```text
#....
#....
#..#.
#.#..
##...
#.#..
#..#.
```

### l

**HEX:** `0x0388421086` — Generated fallback

```text
.##..
..#..
..#..
..#..
..#..
..#..
.###.
```

### m

**HEX:** `0x04635AAC00` — Generated fallback

```text
.....
.....
##.#.
#.#.#
#.#.#
#...#
#...#
```

### n

**HEX:** `0x046319B400` — Captured Sony

```text
.....
.....
#.##.
##..#
#...#
#...#
#...#
```

### o

**HEX:** `0x03A318B800` — Captured Sony

```text
.....
.....
.###.
#...#
#...#
#...#
.###.
```

### p

**HEX:** `0x0042F8BC00` — Generated fallback

```text
.....
.....
####.
#...#
####.
#....
#....
```

### q

**HEX:** `0x043D18F800` — Captured Sony

```text
.....
.....
.####
#...#
#...#
.####
....#
```

### r

**HEX:** `0x0084236800` — Captured Sony

```text
.....
.....
.#.##
.##..
.#...
.#...
.#...
```

### s

**HEX:** `0x03E0E0B800` — Generated fallback

```text
.....
.....
.###.
#....
.###.
....#
####.
```

### t

**HEX:** `0x0324211C42` — Generated fallback

```text
.#...
.#...
###..
.#...
.#...
.#..#
..##.
```

### u

**HEX:** `0x05B318C400` — Captured Sony

```text
.....
.....
#...#
#...#
#...#
#..##
.##.#
```

### v

**HEX:** `0x011518C400` — Generated fallback

```text
.....
.....
#...#
#...#
#...#
.#.#.
..#..
```

### w

**HEX:** `0x02AB58C400` — Generated fallback

```text
.....
.....
#...#
#...#
#.#.#
#.#.#
.#.#.
```

### x

**HEX:** `0x0454454400` — Generated fallback

```text
.....
.....
#...#
.#.#.
..#..
.#.#.
#...#
```

### y

**HEX:** `0x03A1E8C400` — Captured Sony

```text
.....
.....
#...#
#...#
.####
....#
.###.
```

### z

**HEX:** `0x07C4447C00` — Captured Sony

```text
.....
.....
#####
...#.
..#..
.#...
#####
```

## Digits 0-9

### 0

**HEX:** `0x03A318C62E` — Captured Sony

```text
.###.
#...#
#...#
#...#
#...#
#...#
.###.
```

### 1

**HEX:** `0x02108421C8` — Captured Sony

```text
...#.
.###.
...#.
...#.
...#.
...#.
...#.
```

### 2

**HEX:** `0x07C226422E` — Captured Sony

```text
.###.
#...#
....#
..##.
.#...
#....
#####
```

### 3

**HEX:** `0x03A306422E` — Captured Sony

```text
.###.
#...#
....#
..##.
....#
#...#
.###.
```

### 4

**HEX:** `0x0211F4A988` — Generated fallback

```text
...#.
..##.
.#.#.
#..#.
#####
...#.
...#.
```

### 5

**HEX:** `0x03A3083C3F` — Generated fallback

```text
#####
#....
####.
....#
....#
#...#
.###.
```

### 6

**HEX:** `0x03A317844C` — Generated fallback

```text
..##.
.#...
#....
####.
#...#
#...#
.###.
```

### 7

**HEX:** `0x010844221F` — Captured Sony

```text
#####
....#
...#.
...#.
..#..
..#..
..#..
```

### 8

**HEX:** `0x03A317462E` — Captured Sony

```text
.###.
#...#
#...#
.###.
#...#
#...#
.###.
```

### 9

**HEX:** `0x01910F462E` — Generated fallback

```text
.###.
#...#
#...#
.####
....#
...#.
.##..
```

## Space and punctuation

### SPACE

**HEX:** `0x0000000000` — Captured Sony

```text
.....
.....
.....
.....
.....
.....
.....
```

### .

**HEX:** `0x0080000000` — Captured Sony

```text
.....
.....
.....
.....
.....
.....
.#...
```

### -

**HEX:** `0x00000F8000` — Generated fallback

```text
.....
.....
.....
#####
.....
.....
.....
```

### ?

**HEX:** `0x010044422E` — Generated fallback

```text
.###.
#...#
....#
...#.
..#..
.....
..#..
```

### !

**HEX:** `0x0100421084` — Generated fallback

```text
..#..
..#..
..#..
..#..
..#..
.....
..#..
```

### :

**HEX:** `0x000C6018C0` — Generated fallback

```text
.....
.##..
.##..
.....
.##..
.##..
.....
```

### /

**HEX:** `0x0002222200` — Generated fallback

```text
.....
....#
...#.
..#..
.#...
#....
.....
```

### +

**HEX:** `0x00084F9080` — Generated fallback

```text
.....
..#..
..#..
#####
..#..
..#..
.....
```

## Music-note symbol

Photo-derived symbol: fourteen lit pixels counted from the close-up;
not extracted from a Sony bus capture. Not yet added as a text-input character.

### Music note

**HEX:** `0x00CE6A5184` — Photo-derived symbol

```text
..#..
..##.
..#.#
..#.#
.##..
###..
##...
```

## Seven vertical bar characters

Each level fills five complete columns from the bottom upward.
Minimum level 1 always lights the bottom row. The same set works for LEFT
and RIGHT; physical channel allocation and audio analysis are separate.

### Bar level 1

**HEX:** `0x07C0000000` — Generated graphic

```text
.....
.....
.....
.....
.....
.....
#####
```

### Bar level 2

**HEX:** `0x07FE000000` — Generated graphic

```text
.....
.....
.....
.....
.....
#####
#####
```

### Bar level 3

**HEX:** `0x07FFF00000` — Generated graphic

```text
.....
.....
.....
.....
#####
#####
#####
```

### Bar level 4

**HEX:** `0x07FFFF8000` — Generated graphic

```text
.....
.....
.....
#####
#####
#####
#####
```

### Bar level 5

**HEX:** `0x07FFFFFC00` — Generated graphic

```text
.....
.....
#####
#####
#####
#####
#####
```

### Bar level 6

**HEX:** `0x07FFFFFFE0` — Generated graphic

```text
.....
#####
#####
#####
#####
#####
#####
```

### Bar level 7

**HEX:** `0x07FFFFFFFF` — Generated graphic

```text
#####
#####
#####
#####
#####
#####
#####
```

## Future special characters and Unicode

Song titles and artist names require more than ASCII: accented letters,
umlauts, cedillas, slashed letters, sharp s, typographic punctuation and
special symbols should be added as individual glyph records. Each must
include a Unicode code point/name, bitmap, HEX word and provenance.
These additions are planned, not implemented by the current catalogue.

Normalize equivalent Unicode forms before lookup and define transliteration
or a visible replacement for unsupported characters. Multi-code-point
symbols require an explicit single-cell approximation or fallback.

## Reproduction and evidence

Generated from the current editor dictionaries with:

```sh
cd firmware
node scripts/document_display_font.js ../10-display-bitmap-catalogue.md
```

Captured glyph sources include Frequency, No DSR, MHz, 132.0, 118.0 and
1227. Memory OK was captured on the older parallel controller bus and
does not provide additional original 40-bit pixel words.

See the [driver specification](08-esp-display-driver-specification.md)
and [music-note reference](09-music-note-bitmap.md).
