const fs = require("node:fs");
const path = require("node:path");
const display = require("../data/www/display-editor.js");

const output = process.argv[2];
if (!output) throw new Error("Usage: node document_display_font.js <output.md>");
const entries = [];
const lines = [
  "# Complete 5x7 display bitmap catalogue",
  "",
  "Every character below includes its complete bitmap and 40-bit hexadecimal",
  "word. No additional reference font is needed to use these patterns.",
  "**Captured Sony** means the word was extracted from an original display",
  "capture. **Generated fallback** means an application-designed glyph, not a",
  "claimed original Sony shape. Red letters/digits and fixed indicators are",
  "separate fields and must not use these text patterns.",
  "",
  "## Encoding and full control",
  "",
  "Rows run top to bottom; columns run left to right. `#` is lit and `.` is",
  "unlit. With zero-based coordinates, `bit = row * 5 + column`:",
  "",
  "```text",
  " 0  1  2  3  4",
  " 5  6  7  8  9",
  "10 11 12 13 14",
  "15 16 17 18 19",
  "20 21 22 23 24",
  "25 26 27 28 29",
  "30 31 32 33 34",
  "```",
  "",
  "Set or clear each bit independently. Bits 35-39 are zero in these glyphs;",
  "they are not established ON/OFF flags. Transmit all 40 bits, **bit 39 first**.",
  "The output latch holds the previous pattern during normal shifting; STB",
  "and grid CLEAR timing expose the completed glyph under the selected grid.",
  "Continuous 26-slot scanning is still required.",
  "",
  "Mapped text slots are message 14-22, frequency 6-10 and suffix 3-5.",
  "Message positions left to right correspond to slots 22 down to 14.",
  "Individual live pixel orientation remains to be validated.",
  "",
  "Case matters. Missing captured characters use the generated shapes listed",
  "here. Identical generated shapes, such as O and 0, are font-design choices.",
  "",
  "## Character HEX index",
  "",
  "| Character | 40-bit HEX | Source |",
  "|-----------|------------|--------|"
];
const groups = [
  ["Uppercase A-Z", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"],
  ["Lowercase a-z", "abcdefghijklmnopqrstuvwxyz"],
  ["Digits 0-9", "0123456789"],
  ["Space and punctuation", " .-?!:/+"]
];
const label = character => character === " " ? "SPACE" : character;
for (const [, characters] of groups)
  for (const character of characters)
    lines.push(`| ${label(character)} | \`0x${display.hex(display.glyph(character))}\` | ${
      Object.hasOwn(display.sonyFont, character) ? "Captured Sony" : "Generated fallback"} |`);

function bitmap(word) {
  return Array.from({length:7}, (_, row) =>
    Array.from({length:5}, (_, column) =>
      word & (1n << BigInt(row * 5 + column)) ? "#" : ".").join(""));
}
function entry(name, word, source, character, unicodeName) {
  const codePoints = character ? Array.from(character, c =>
    `U+${c.codePointAt(0).toString(16).toUpperCase().padStart(4, "0")}`).join(" ") : "";
  entries.push({name, word, source, codePoints});
  lines.push("", `### ${name}`, "", `**HEX:** \`0x${display.hex(word)}\` — ${source}`,
    ...(character ? ["", `**Unicode:** ${codePoints} — ${unicodeName}`] : []),
    "", "```text", ...bitmap(word), "```");
}
for (const [heading, characters] of groups) {
  lines.push("", `## ${heading}`);
  for (const character of characters)
    entry(label(character), display.glyph(character),
      Object.hasOwn(display.sonyFont, character) ? "Captured Sony" : "Generated fallback");
}
lines.push("", "## Extended Latin, punctuation and symbols",
  "", "These are generated or user-supplied 5x7 approximations, not captured Sony",
  "glyphs. Accents use compact letter bodies to fit the seven-row grid;",
  "fine details and some distinctions are necessarily lost. Hardware appearance",
  "has not yet been validated. Each record below is supported by text lookup.",
  "",
  "The speaker (U+1F508) uses the supplied rows 02, 06, 1E, 1E, 1E, 06, 02",
  "when bit 4 is the leftmost pixel in each source row. Sony instead assigns",
  "bit 0 to the leftmost pixel, so each five-bit row is reversed before packing",
  "with `bit = row * 5 + column`. Generic row bytes are not sent directly.",
  "The supplied AirPlay-style icon uses project-private U+E000, not a standard",
  "Unicode AirPlay character. Its source row bytes are 0E, 11, 04, 0A, 04,",
  "0E, 1F. Applications must deliberately insert U+E000 to select this icon.",
  "", "| Character | Unicode | Name | 40-bit HEX | Provenance |",
  "|-----------|---------|------|------------|------------|");
for (const [character, record] of Object.entries(display.unicodeFont)) {
  if (character === "♪") continue;
  lines.push(`| ${character === "\u00a0" ? "NBSP" : character} | U+${
    character.codePointAt(0).toString(16).toUpperCase().padStart(4, "0")} | ${
    record.name} | \`0x${display.hex(record.word)}\` | ${record.source} |`);
}
for (const [character, record] of Object.entries(display.unicodeFont)) {
  if (character === "♪") continue;
  entry(character === "\u00a0" ? "NBSP" : character, record.word, record.source,
    character, record.name);
}
lines.push("", "## Music-note symbol",
  "", "Photo-derived symbol: fourteen lit pixels counted from the close-up;",
  "not extracted from a Sony bus capture. Text input uses U+266A.");
entry("♪", display.unicodeFont["♪"].word, display.unicodeFont["♪"].source,
  "♪", display.unicodeFont["♪"].name);
lines.push("", "## Seven vertical bar characters",
  "", "Each level fills five complete columns from the bottom upward.",
  "Minimum level 1 always lights the bottom row. The same set works for LEFT",
  "and RIGHT; physical channel allocation and audio analysis are separate.");
for (let level = 1; level <= 7; ++level)
  entry(`Bar level ${level}`, display.barGlyph(level), "Generated graphic");
lines.push("", "## Unicode lookup and unsupported characters",
  "",
"Inbound UTF-8 must first be decoded to Unicode text by the transport.",
"The browser editor operates on decoded JavaScript strings, not font indexes",
"derived from individual UTF-8 bytes. Invalid input must be rejected or",
"decoded with U+FFFD, which renders as the unmatched-character asterisk.",
"",
"Text is normalized to NFC before lookup and split into grapheme clusters,",
"so composed/decomposed accents occupy the same single cell. Captured Sony",
"glyphs take precedence, followed by custom Unicode records and the existing",
"application font. No broad external font replaces these designs.",
  "",
  "For an unlisted Latin letter with decomposable accents, lookup removes the",
  "combining marks and uses the base letter as a one-cell transliteration.",
  "Unlisted letters, scripts, standalone marks and unsupported multi-code-point",
  "symbols (including Chinese characters and emoji ZWJ sequences) use one",
  "visible asterisk (U+002A) per",
  "grapheme, not a blank or one replacement per UTF-16 byte/code unit.",
  "A trailing text/emoji presentation selector on a listed symbol uses its",
  "same monochrome glyph. NBSP renders as a blank space.",
  "This is a finite repertoire, not complete Unicode coverage.",
  "Question marks in the original text retain their own question-mark glyph.",
  "A future broader fallback font may be inserted before the asterisk, but",
  "no Unifont conversion or contextual-script shaping is implemented.",
  "",
  "## Reproduction and evidence",
  "",
  "Generated from the current editor dictionaries with:",
  "",
  "```sh",
  "cd firmware",
  "node scripts/document_display_font.js ../10-display-bitmap-catalogue.md",
  "```",
  "",
  "Captured glyph sources include Frequency, No DSR, MHz, 132.0, 118.0 and",
  "1227. Memory OK was captured on the older parallel controller bus and",
  "does not provide additional original 40-bit pixel words.",
  "",
  "See the [driver specification](08-esp-display-driver-specification.md)",
  "and [music-note reference](09-music-note-bitmap.md).",
  ""
);
const escapeXml = text => text.replace(/[&<>"']/g, character =>
  ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&apos;"})[character]);
const columns = 6;
const tileWidth = 180;
const tileHeight = 175;
const width = columns * tileWidth;
const height = Math.ceil(entries.length / columns) * tileHeight;
const svg = [
  `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">`,
  "<title>DAR-1000ES character bitmap and 40-bit HEX chart</title>",
  "<desc>Each cell shows a character name, its five-column seven-row bitmap, hexadecimal word and evidence source. Cyan pixels are lit; dark pixels are unlit.</desc>",
  `<rect width="${width}" height="${height}" fill="#101820"/>`
];
entries.forEach(({name, word, source, codePoints}, index) => {
  const x = (index % columns) * tileWidth;
  const y = Math.floor(index / columns) * tileHeight;
  svg.push(`<g transform="translate(${x} ${y})">`,
    `<rect x="3" y="3" width="174" height="169" rx="5" fill="#182630" stroke="#40505c"/>`,
    `<text x="90" y="24" text-anchor="middle" fill="#ffffff" font-family="monospace" font-size="16">${escapeXml(name)}${codePoints ? ` (${codePoints})` : ""}</text>`);
  bitmap(word).forEach((row, r) => Array.from(row).forEach((pixel, c) =>
    svg.push(`<rect x="${60 + c * 12}" y="${35 + r * 12}" width="10" height="10" rx="1" fill="${pixel === "#" ? "#66f5ee" : "#293c48"}"/>`)));
  svg.push(`<text x="90" y="140" text-anchor="middle" fill="#ffffff" font-family="monospace" font-size="13">0x${display.hex(word)}</text>`,
    `<text x="90" y="158" text-anchor="middle" fill="#bccbd5" font-family="sans-serif" font-size="11">${escapeXml(source)}</text>`,
    "</g>");
});
svg.push("</svg>", "");
const chartPath = path.resolve(path.dirname(output), "media/display-bitmap-chart.svg");
fs.mkdirSync(path.dirname(chartPath), {recursive: true});
fs.writeFileSync(chartPath, svg.join("\n"));
lines.splice(lines.indexOf("## Character HEX index"), 0,
  "## Visual character grid",
  "",
  "Each cell shows the character, its exact 5x7 rendering, complete 40-bit HEX",
  "word and source. Cyan pixels are lit; dark pixels are unlit. This chart uses",
  "our current glyphs, not a reproduction of the reference website's font.",
  "The music note is photo-derived; extended glyphs are generated approximations.",
  "",
  "![Character grid with 5x7 bitmaps, HEX values and provenance](media/display-bitmap-chart.svg)",
  "",
  "[Open the full-size character chart](media/display-bitmap-chart.svg).",
  "",
);
fs.writeFileSync(path.resolve(output), lines.join("\n"));
