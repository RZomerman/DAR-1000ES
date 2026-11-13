/* Slot words are 40-bit values: use BigInt, not JavaScript's 32-bit operators. */
const SonyDisplay = (() => {
  const sonyFont = {
    " ": 0n, ".": 0x0080000000n,
    F: 0x004217843fn, N: 0x04639ace31n, D: 0x03e5294a4fn,
    S: 0x03a307062en, R: 0x045257c62fn, M: 0x04631ad771n,
    H: 0x04631fc631n,
    r: 0x0084236800n, e: 0x0783f8b800n, q: 0x043d18f800n,
    u: 0x05b318c400n, n: 0x046319b400n, c: 0x078210f800n,
    y: 0x03a1e8c400n, o: 0x03a318b800n, z: 0x07c4447c00n,
    "0": 0x03a318c62en, "1": 0x02108421c8n, "2": 0x07c226422en,
    "3": 0x03a306422en, "7": 0x010844221fn, "8": 0x03a317462en
  };
  const font = {
    A: [126,17,17,17,126], B: [127,73,73,73,54], C: [62,65,65,65,34],
    D: [127,65,65,34,28], E: [127,73,73,73,65], F: [127,9,9,9,1],
    G: [62,65,73,73,122], H: [127,8,8,8,127], I: [0,65,127,65,0],
    J: [32,64,65,63,1], K: [127,8,20,34,65], L: [127,64,64,64,64],
    M: [127,2,12,2,127], N: [127,4,8,16,127], O: [62,65,65,65,62],
    P: [127,9,9,9,6], Q: [62,65,81,33,94], R: [127,9,25,41,70],
    S: [70,73,73,73,49], T: [1,1,127,1,1], U: [63,64,64,64,63],
    V: [31,32,64,32,31], W: [63,64,56,64,63], X: [99,20,8,20,99],
    Y: [7,8,112,8,7], Z: [97,81,73,69,67],
    "0": [62,81,73,69,62], "1": [0,66,127,64,0], "2": [66,97,81,73,70],
    "3": [33,65,69,75,49], "4": [24,20,18,127,16], "5": [39,69,69,69,57],
    "6": [60,74,73,73,48], "7": [1,113,9,5,3], "8": [54,73,73,73,54],
    "9": [6,73,73,41,30], " ": [0,0,0,0,0], "-": [8,8,8,8,8],
    "?": [2,1,81,9,6], "!": [0,0,95,0,0], ":": [0,54,54,0,0],
    "/": [32,16,8,4,2], "+": [8,8,62,8,8],
    a: [32,84,84,84,120], b: [127,72,68,68,56], c: [56,68,68,68,32],
    d: [56,68,68,72,127], e: [56,84,84,84,24], f: [8,126,9,1,2],
    g: [12,82,82,82,62], h: [127,8,4,4,120], i: [0,68,125,64,0],
    j: [32,64,68,61,0], k: [127,16,40,68,0], l: [0,65,127,64,0],
    m: [124,4,24,4,120], n: [124,8,4,4,120], o: [56,68,68,68,56],
    p: [124,20,20,20,8], q: [8,20,20,24,124], r: [124,8,4,4,8],
    s: [72,84,84,84,32], t: [4,63,68,64,32], u: [60,64,64,32,124],
    v: [28,32,64,32,28], w: [60,64,48,64,60], x: [68,40,16,40,68],
    y: [12,80,80,80,60], z: [68,100,84,76,68]
  };
  const red = {"": 0n, A: 0x478c00n, B: 0xa61c00n, C: 0xc08400n, D: 0xa41c00n};
  const indicators = [
    ["MEMORY",25,0x02n,"confirmed"],
    ["Left MONO label",24,0x10n,"candidate"],
    ["Left MONO 1",24,0x08n,"confirmed"],
    ["Left MONO 2",24,0x04n,"confirmed"],
    ["Right MONO",24,0x800n,"confirmed"],
    ["PROGRAM label",24,0x100n,"candidate"],
    ["PROGRAM 1",24,0x80n,"confirmed"], ["PROGRAM 2",24,0x40n,"confirmed"],
    ["PROGRAM 3",24,0x20n,"confirmed"],
    ["CABLE",23,0x01n,"confirmed"], ["SATELLITE",23,0x02n,"confirmed"],
    ["LNC label",23,0x04n,"candidate"], ["LNC OFF",23,0x08n,"confirmed"],
    ["LNC A",23,0x10n,"confirmed"], ["LNC B",23,0x20n,"confirmed"]
  ];
  const mapped = slot => slot >= 3 && slot <= 10 || slot >= 14 && slot <= 22;
  const hex = word => word.toString(16).toUpperCase().padStart(10, "0");
  const unicodeFont = {};
  function bitmapWord(rows) {
    let word = 0n;
    rows.forEach((row, r) => Array.from(row).forEach((pixel, c) => {
      if (pixel === "#") word |= 1n << BigInt(r * 5 + c);
    }));
    return word;
  }
  function addUnicode(character, name, rows) {
    unicodeFont[character] = {name, rows, word: bitmapWord(rows),
      source: "Generated approximation"};
  }
  const accents = [
    ["ÀÈÌÒÙàèìòù", "AEIOUaeiou", "GRAVE", ".#..."],
    ["ÁÉÍÓÚÝáéíóúý", "AEIOUYaeiouy", "ACUTE", "...#."],
    ["ÂÊÎÔÛâêîôû", "AEIOUaeiou", "CIRCUMFLEX", ".###."],
    ["ÃÑÕãñõ", "ANOano", "TILDE", "##..#"],
    ["ÄËÏÖÜŸäëïöüÿ", "AEIOUYaeiouy", "DIAERESIS", ".#.#."]
  ];
  for (const [characters, bases, accent, top] of accents) {
    Array.from(characters).forEach((character, index) => {
      const base = bases[index];
      const word = glyph(base);
      const rows = [top, ".....", ...[0,1,3,5,6].map(row =>
        Array.from({length:5}, (_, column) =>
          word & (1n << BigInt(row * 5 + column)) ? "#" : ".").join(""))];
      addUnicode(character,
        `LATIN ${base === base.toUpperCase() ? "CAPITAL" : "SMALL"} LETTER ${base.toUpperCase()} WITH ${accent}`, rows);
    });
  }
  const extraGlyphs = [
    ["\u{1F508}", "SPEAKER", ["...#.","..##.","####.","####.","####.","..##.","...#."]],
    ["\uE000", "PRIVATE USE: AIRPLAY-STYLE ICON", [".###.","#...#","..#..",".#.#.","..#..",".###.","#####"]],
    ["*", "ASTERISK", [".....","#.#.#",".###.","#####",".###.","#.#.#","....."]],
    ["Å", "LATIN CAPITAL LETTER A WITH RING ABOVE", [".###.",".#.#.",".###.","#...#","#####","#...#","#...#"]],
    ["å", "LATIN SMALL LETTER A WITH RING ABOVE", ["..#..",".#.#.","..#..",".###.","....#",".####",".####"]],
    ["Ç", "LATIN CAPITAL LETTER C WITH CEDILLA", [".####","#....","#....","#....",".####","..#..",".#..."]],
    ["ç", "LATIN SMALL LETTER C WITH CEDILLA", [".....",".....",".###.","#....",".###.","..#..",".#..."]],
    ["Ø", "LATIN CAPITAL LETTER O WITH STROKE", [".####","#..##","#.#.#","#.#.#","##..#","##..#","####."]],
    ["ø", "LATIN SMALL LETTER O WITH STROKE", [".....",".....",".####","#..##","#.#.#","##..#","####."]],
    ["Ł", "LATIN CAPITAL LETTER L WITH STROKE", ["#....","#....","#.#..","##...","#....","#....","#####"]],
    ["ł", "LATIN SMALL LETTER L WITH STROKE", [".##..","..#..","..##.",".##..","..#..","..#..",".###."]],
    ["ß", "LATIN SMALL LETTER SHARP S", [".##..","#..#.","#..#.","#.##.","#...#","#...#","#.##."]],
    ["ẞ", "LATIN CAPITAL LETTER SHARP S", ["####.","#...#","#..#.","#.##.","#...#","#...#","#.##."]],
    ["Æ", "LATIN CAPITAL LETTER AE", [".####","#.#..","#.#..","#####","#.#..","#.#..","#.###"]],
    ["æ", "LATIN SMALL LETTER AE", [".....",".....","##.#.","..#.#",".####","#.#..",".####"]],
    ["Œ", "LATIN CAPITAL LIGATURE OE", [".####","#.#..","#.#..","#.###","#.#..","#.#..",".####"]],
    ["œ", "LATIN SMALL LIGATURE OE", [".....",".....",".#.#.","#.#.#","#.###","#.#..",".####"]],
    ["'", "APOSTROPHE", ["..#..","..#..",".#...",".....",".....",".....","....."]],
    ['"', "QUOTATION MARK", [".#.#.",".#.#.",".....",".....",".....",".....","....."]],
    ["‘", "LEFT SINGLE QUOTATION MARK", ["..#..",".#...",".#...",".....",".....",".....","....."]],
    ["’", "RIGHT SINGLE QUOTATION MARK", [".#...",".#...","#....",".....",".....",".....","....."]],
    ["“", "LEFT DOUBLE QUOTATION MARK", ["..#.#",".#.#.",".#.#.",".....",".....",".....","....."]],
    ["”", "RIGHT DOUBLE QUOTATION MARK", [".#.#.",".#.#.","#.#..",".....",".....",".....","....."]],
    ["„", "DOUBLE LOW-9 QUOTATION MARK", [".....",".....",".....",".....",".#.#.",".#.#.","#.#.."]],
    ["–", "EN DASH", [".....",".....",".....",".###.",".....",".....","....."]],
    ["—", "EM DASH", [".....",".....",".....","#####",".....",".....","....."]],
    ["…", "HORIZONTAL ELLIPSIS", [".....",".....",".....",".....",".....",".....","#.#.#"]],
    [",", "COMMA", [".....",".....",".....",".....","..#..","..#..",".#..."]],
    [";", "SEMICOLON", [".....","..#..",".....",".....","..#..","..#..",".#..."]],
    ["&", "AMPERSAND", [".##..","#..#.","#.#..",".#...","#.#.#","#..#.",".##.#"]],
    ["(", "LEFT PARENTHESIS", ["...#.","..#..",".#...",".#...",".#...","..#..","...#."]],
    [")", "RIGHT PARENTHESIS", [".#...","..#..","...#.","...#.","...#.","..#..",".#..."]],
    ["[", "LEFT SQUARE BRACKET", [".###.",".#...",".#...",".#...",".#...",".#...",".###."]],
    ["]", "RIGHT SQUARE BRACKET", [".###.","...#.","...#.","...#.","...#.","...#.",".###."]],
    ["°", "DEGREE SIGN", [".##..","#..#.",".##..",".....",".....",".....","....."]],
    ["©", "COPYRIGHT SIGN", [".###.","#...#","#.###","#.#.#","#.###","#...#",".###."]],
    ["®", "REGISTERED SIGN", [".###.","#...#","#.###","#.###","#.#.#","#...#",".###."]],
    ["™", "TRADE MARK SIGN", ["###.#",".#.##",".#.#.",".....",".....",".....","....."]],
    ["€", "EURO SIGN", ["..###",".#...","####.",".#...","####.",".#...","..###"]],
    ["£", "POUND SIGN", ["..##.",".#..#",".#...","####.",".#...",".#...","#####"]],
    ["¥", "YEN SIGN", ["#...#",".#.#.","..#..","#####","..#..","#####","..#.."]],
    ["♥", "BLACK HEART SUIT", [".#.#.","#####","#####","#####",".###.","..#..","....."]],
    ["★", "BLACK STAR", ["..#..","..#..","#####",".###.",".###.","#...#","....."]],
    ["←", "LEFTWARDS ARROW", [".....","..#..",".#...","#####",".#...","..#..","....."]],
    ["→", "RIGHTWARDS ARROW", [".....","..#..","...#.","#####","...#.","..#..","....."]],
    ["♫", "BEAMED EIGHTH NOTES", [".####",".#..#",".#..#",".#..#","##.##","##.##","....."]],
    ["♪", "EIGHTH NOTE", ["..#..","..##.","..#.#","..#.#",".##..","###..","##..."]]
  ];
  for (const [character, name, rows] of extraGlyphs) addUnicode(character, name, rows);
  unicodeFont["♪"].source = "Photo-derived approximation";
  unicodeFont["\u{1F508}"].source = "User-supplied bitmap";
  unicodeFont["\uE000"].source = "User-supplied bitmap";
  unicodeFont[" "] = {name: "NO-BREAK SPACE", rows: Array(7).fill("....."),
    word: 0n, source: "Generated space"};
  const segmenter = new Intl.Segmenter("und", {granularity: "grapheme"});
  function glyph(character) {
    character = character.normalize("NFC");
    if (Object.hasOwn(sonyFont, character)) return sonyFont[character];
    if (Object.hasOwn(unicodeFont, character)) return unicodeFont[character].word;
    const symbol = character.replace(/[\uFE0E\uFE0F]$/, "");
    if (Object.hasOwn(unicodeFont, symbol)) return unicodeFont[symbol].word;
    if (/^\p{Script=Latin}\p{Mark}+$/u.test(character.normalize("NFD")))
      return glyph(Array.from(character.normalize("NFD"))[0]);
    const columns = font[character];
    if (!columns) return unicodeFont["*"].word;
    let word = 0n;
    for (let row = 0; row < 7; ++row)
      for (let column = 0; column < 5; ++column)
        if (columns[column] & (1 << row)) word |= 1n << BigInt(row * 5 + column);
    return word;
  }
  const characters = text => Array.from(segmenter.segment(text.normalize("NFC")),
    part => part.segment);
  function place(words, text, leftSlot, length) {
    const chars = characters(text).slice(0, length);
    while (chars.length < length) chars.push(" ");
    chars.forEach((character, index) => words[leftSlot - index] = glyph(character));
  }
  function barGlyph(level) {
    if (!Number.isInteger(level) || level < 1 || level > 7)
      throw new RangeError("Bar level must be an integer from 1 to 7");
    return ((1n << BigInt(level * 5)) - 1n) << BigInt((7 - level) * 5);
  }
  return {glyph, place, hex, red, indicators, mapped, sonyFont, unicodeFont, barGlyph, characters};
})();
if (typeof module !== "undefined") module.exports = SonyDisplay;

if (typeof document !== "undefined") {
  const words = Array(26).fill(0n);
  const entries = [];
  const buttons = [];
  const characterContainer = document.getElementById("characters");
  const codes = document.getElementById("display-codes");
  const status = document.getElementById("status");
  const info = document.getElementById("element-info");
  const inspect = text => info.textContent = text;
  const testSlot = document.getElementById("test-slot");
  for (let slot = 22; slot >= 3; --slot) {
    if (!SonyDisplay.mapped(slot)) continue;
    const option = document.createElement("option");
    option.value = String(slot);
    option.textContent = "P" + String(23 - slot).padStart(2,"0") + " / slot " + slot;
    testSlot.appendChild(option);
  }
  testSlot.value = "22";
  const regions = {};
  function hotspot(button, x, y, width) {
    button.style.left = (x * 2) + "px";
    button.style.top = (y * 2) + "px";
    button.style.width = (width * 2) + "px";
  }
  function group(id, parent) {
    const element = document.createElement("div");
    element.className = "glass-group";
    element.style.left = "0";
    element.style.top = "0";
    if (id === "left") {
      element.style.width = "130px";
      element.style.whiteSpace = "normal";
      element.style.flexWrap = "wrap";
    }
    document.getElementById(parent).appendChild(element);
    regions[id] = element;
    return element;
  }
  group("left", "glass-top");
  group("left-mono", "glass-top");
  group("left-audio", "glass-top");
  group("tuned", "glass-top");
  group("right-mono", "glass-top");
  group("right-audio", "glass-top");
  group("program", "glass-top");
  group("source", "glass-bottom");
  group("lnc", "glass-bottom");
  group("markers", "glass-bottom");
  const groups = slot => slot >= 14 ? "Message" : slot >= 11 ? "Unmapped" :
    slot >= 6 ? "Frequency" : slot >= 3 ? "MHz" : "Unmapped";
  for (let slot = 22; slot >= 1; --slot) {
    const block = document.createElement("div");
    block.className = "character" + (SonyDisplay.mapped(slot) ? "" : " unmapped");
    const label = document.createElement("div");
    label.textContent = "P" + String(23 - slot).padStart(2, "0") + " / slot " + slot;
    const field = document.createElement("input");
    field.disabled = !SonyDisplay.mapped(slot);
    field.setAttribute("aria-label", label.textContent + " character");
    field.addEventListener("input", () => {
      SonyDisplay.place(words, field.value, slot, 1);
      render();
    });
    const pixels = document.createElement("div");
    pixels.className = "pixels";
    const dots = [];
    for (let bit = 0; bit < 35; ++bit) {
      const dot = document.createElement("button");
      dot.type = "button";
      if (!SonyDisplay.mapped(slot)) dot.className = "unknown";
      dot.title = label.textContent + ", row " + (Math.floor(bit / 5) + 1) +
        ", column " + (bit % 5 + 1) + ", bit " + bit;
      dot.setAttribute("aria-label", dot.title);
      dot.addEventListener("click", () => {
        inspect(dot.title + (SonyDisplay.mapped(slot) ? " (generated pixel mapping)." :
          " is unmapped. No word changed."));
        if (!SonyDisplay.mapped(slot)) return;
        words[slot] ^= 1n << BigInt(bit);
        field.value = "";
        render();
      });
      dots.push(dot);
      pixels.appendChild(dot);
    }
    block.append(label, field, pixels, document.createTextNode(groups(slot)));
    characterContainer.appendChild(block);
    entries.push({slot, dots, field});
  }
  const regionFor = name => name === "MEMORY" ? "left" :
    name.startsWith("Left MONO") ? "left-mono" :
    name === "Right MONO" ? "right-mono" :
    name.startsWith("PROGRAM") ? "program" :
    name.startsWith("LNC") ? "lnc" : "source";
  for (const [name, slot, mask, confidence] of SonyDisplay.indicators) {
    const button = document.createElement("button");
    button.className = "indicator" + (confidence === "candidate" ? " candidate" : "");
    const captions = {"Left MONO label":"MONO", "Left MONO 1":"1",
      "Left MONO 2":"2", "Right MONO":"MONO", "PROGRAM label":"PROGRAM",
      "PROGRAM 1":"1","PROGRAM 2":"2","PROGRAM 3":"3",
      "LNC label":"LNC","LNC OFF":"OFF","LNC A":"A","LNC B":"B"};
    button.textContent = (captions[name] || name) + (confidence === "candidate" ? " ?" : "");
    button.title = "Slot " + slot + ", mask 0x" + mask.toString(16) + ", " + confidence;
    button.setAttribute("aria-label", name + ": " + button.title);
    button.addEventListener("click", () => {
      inspect(name + ": " + button.title + ". Local edit; Apply to send.");
      words[slot] ^= mask;
      render();
    });
    regions[regionFor(name)].appendChild(button);
    const locations = {
      MEMORY:[170,66,43], "Left MONO label":[240,55,26],
      "Left MONO 1":[282,55,9], "Left MONO 2":[303,55,9],
      "Right MONO":[708,58,29], "PROGRAM label":[960,60,48],
      "PROGRAM 1":[1020,60,9], "PROGRAM 2":[1036,60,9],
      "PROGRAM 3":[1051,60,9], CABLE:[239,118,36],
      SATELLITE:[287,118,65], "LNC label":[371,118,25],
      "LNC OFF":[403,118,23], "LNC A":[440,118,12], "LNC B":[469,118,12]
    };
    hotspot(button, ...locations[name]);
    buttons.push({button, slot, mask});
  }
  for (const [name, region] of [["STEREO","left"],["MUSIC (left)","left-audio"],
    ["SPEECH (left)","left-audio"],["< TUNED","tuned"],["TUNED","tuned"],
    ["TUNED >","tuned"],["MUSIC (right)","right-audio"],["SPEECH (right)","right-audio"],
    ["Right MONO index 2","right-mono"],["M symbol","left"],["N symbol","left"]]) {
    const button = document.createElement("button");
    button.className = "indicator unknown";
    button.textContent = name.replace(" (left)","").replace(" (right)","")
      .replace("Right MONO index 2","2").replace("< TUNED","<").replace("TUNED >",">");
    button.addEventListener("click", () => inspect(name +
      ": physical element shown in the drawing; slot/mask not isolated. No word changed."));
    regions[region].appendChild(button);
    const locations = {
      STEREO:[170,55,41], "MUSIC (left)":[339,55,36],
      "SPEECH (left)":[414,55,43], "< TUNED":[544,58,18],
      TUNED:[564,58,32], "TUNED >":[598,58,21], "MUSIC (right)":[808,59,39],
      "SPEECH (right)":[860,59,43], "Right MONO index 2":[742,58,12],
      "M symbol":[170,87,10], "N symbol":[170,101,10]
    };
    hotspot(button, ...locations[name]);
    button.title = name + " (unmapped)";
    if (name === "STEREO") {
      button.style.order = "-1";
    }
    if (["M symbol","N symbol"].includes(name)) {
      button.style.position = "absolute";
      button.style.left = "340px";
      button.style.top = name === "M symbol" ? "174px" : "202px";
      button.textContent = name[0];
    }
  }
  for (let number = 1; number <= 16; ++number) {
    const button = document.createElement("button");
    button.className = "indicator unknown";
    button.textContent = String(number);
    button.style.width = "86px";
    button.style.textAlign = "center";
    button.addEventListener("click", () => inspect("Lower marker " + number +
      ": mapping unknown. No word changed."));
    regions.markers.appendChild(button);
    hotspot(button, 543 + (number-1)*26.8, 122, 14);
    button.title = "Lower marker " + number + " (unmapped)";
  }
  const red = document.getElementById("red-letter");
  red.addEventListener("change", () => {
    words[25] = (words[25] & 0x02n) | SonyDisplay.red[red.value];
    inspect("Red " + (red.value || "blank") + ": captured slot-25 pattern; MEMORY preserved.");
    render();
  });
  document.getElementById("red-inspect").addEventListener("click", () =>
    inspect("Red field: slot 25. Letter presets are captured; bit controls are numbered " +
      "rather than placed on guessed physical segments."));
  const redBits = [];
  const matrixDots = [];
  for (let bit = 0; bit < 35; ++bit) {
    const dot = document.createElement("button");
    dot.setAttribute("aria-label", "Inspect red matrix row " +
      (Math.floor(bit/5)+1) + ", column " + (bit%5+1));
    dot.addEventListener("click", () => inspect(
      "Small red letter matrix pixel: physical bit mapping not validated. " +
      "Use numbered slot-25 bit tests to identify it. No word changed."));
    document.getElementById("red-matrix").appendChild(dot);
    matrixDots.push(dot);
  }
  for (const [id, name] of [["red-tens","tens"],["red-units","units"]]) {
    for (const segment of ["a","b","c","d","e","f","g"]) {
      const button = document.createElement("button");
      button.className = segment;
      button.setAttribute("aria-label", "Inspect red " + name + " segment " + segment);
      button.addEventListener("click", () => inspect("Red " + name + " digit, segment " +
        segment + ": physical bit mapping incomplete. No word changed."));
      document.getElementById(id).appendChild(button);
    }
  }
  for (const bit of [10,11,12,15,16,17,18,21,22,23]) {
    const button = document.createElement("button");
    const mask = 1n << BigInt(bit);
    button.textContent = "b" + bit;
    button.title = "Slot 25, bit " + bit + ", mask 0x" + mask.toString(16);
    button.addEventListener("click", () => {
      words[25] ^= mask;
      red.value = "";
      inspect(button.title + ": known red-letter bit, physical segment position unverified.");
      render();
    });
    document.getElementById("red-bits").appendChild(button);
    redBits.push({button, mask});
  }
  const meter = document.getElementById("meter-preview");
  for (let i = 0; i < 15; ++i) {
    const bar = document.createElement("button");
    bar.style.height = (35 + i * 2.9) + "px";
    bar.setAttribute("aria-label", "Inspect signal bar " + (i + 1));
    bar.addEventListener("click", () => inspect("Signal bar " + (i + 1) +
      ": individual assignment unknown. Full group uses slot 0 = 0x7FFF. No word changed."));
    meter.appendChild(bar);
    const dot = document.createElement("button");
    dot.setAttribute("aria-label", "Inspect lower signal dot " + (i + 1));
    dot.addEventListener("click", () => inspect("Lower signal dot " + (i + 1) +
      ": individual assignment unknown. Startup/dotted state captured as 0x4000. No word changed."));
    document.getElementById("meter-dots").appendChild(dot);
  }
  for (const [label, value] of [["Signal off",0n], ["Startup / dots",0x4000n], ["Signal full",0x7fffn]]) {
    const button = document.createElement("button");
    button.className = "indicator";
    button.textContent = label;
    button.title = "Slot 0, word 0x" + value.toString(16);
    button.addEventListener("click", () => {
      words[0] = value;
      inspect(label + ": slot 0 = 0x" + SonyDisplay.hex(value) +
        ". Preview is schematic; individual bars/dots are not mapped.");
      render();
    });
    document.getElementById("signal-controls").appendChild(button);
  }
  function render() {
    for (const {slot, dots} of entries)
      dots.forEach((dot, bit) => {
        const on = Boolean(words[slot] & (1n << BigInt(bit)));
        dot.classList.toggle("on", on);
        dot.setAttribute("aria-pressed", String(on));
      });
    for (const {button, slot, mask} of buttons) {
      const on = Boolean(words[slot] & mask);
      button.classList.toggle("on", on);
      button.setAttribute("aria-pressed", String(on));
    }
    function fitDisplay() {
      const glass = document.querySelector(".glass");
      const container = document.querySelector(".display-preview");
      if (glass && container)
        glass.style.zoom = Math.min(1, Math.max(0.1, (container.clientWidth - 24) / 2220));
    }
    if (typeof window.addEventListener === "function") {
      window.addEventListener("resize", fitDisplay);
      fitDisplay();
    }
    meter.classList.toggle("full", words[0] === 0x7fffn);
    document.getElementById("meter-dots").classList.toggle("lit", words[0] !== 0n);
    const letter = Object.entries(SonyDisplay.red).find(([, value]) =>
      value === (words[25] & ~0x02n));
    document.getElementById("red-preview").textContent = letter ? letter[0] || " " : "?";
    const preview = letter ? SonyDisplay.glyph(letter[0] || " ") : 0n;
    matrixDots.forEach((dot,bit) =>
      dot.classList.toggle("on", Boolean(preview & (1n << BigInt(bit)))));
    for (const {button, mask} of redBits) {
      const on = Boolean(words[25] & mask);
      button.classList.toggle("on", on);
      button.setAttribute("aria-pressed", String(on));
    }
    document.getElementById("red-code").textContent = "Slot 25: 0x" + SonyDisplay.hex(words[25]);
    codes.textContent = words.map((word, slot) => "Slot " + String(slot).padStart(2, "0") +
      ": 0x" + SonyDisplay.hex(word)).join("\n");
  }
  function setText(id, leftSlot, length) {
    const text = document.getElementById(id).value;
    SonyDisplay.place(words, text, leftSlot, length);
    const chars = SonyDisplay.characters(text);
    entries.forEach(entry => {
      const index = leftSlot - entry.slot;
      if (index >= 0 && index < length) entry.field.value = chars[index] || " ";
    });
    render();
  }
  document.getElementById("set-message").addEventListener("click", () => setText("message",22,9));
  document.getElementById("set-frequency").addEventListener("click", () => {
    setText("frequency",10,5);
    SonyDisplay.place(words,"MHz",5,3);
    entries.forEach(entry => {
      if (entry.slot >= 3 && entry.slot <= 5) entry.field.value = "MHz"[5-entry.slot];
    });
    render();
  });
  document.getElementById("clear-editor").addEventListener("click", () => {
    words.fill(0n);
    entries.forEach(entry => entry.field.value = "");
    red.value = "";
    render();
  });
  async function transmit(frame) {
    return window.screenRequest({method:"POST", headers:{"Content-Type":"application/json"},
      body:JSON.stringify({sample:"Display",
        ...(window.screenTimingRequest ? window.screenTimingRequest() : {}),
        words:frame.map(SonyDisplay.hex),
        bench_confirmed:document.getElementById("confirmed").checked,
        live_confirmed:document.getElementById("live-confirmed").checked})});
  }
  let testTask = null;
  let testGeneration = 0;
  let testIndex = 0;
  const testStatus = document.getElementById("test-status");
  const groupSelector = document.getElementById("test-group");
  groupSelector.value = "indicators";
  function testItems() {
    switch (groupSelector.value) {
      case "red": return redBits.map(({mask}, index) =>
        ["Red bit " + [10,11,12,15,16,17,18,21,22,23][index],25,mask,"position unverified"]);
      case "letters": return Object.entries(SonyDisplay.red).filter(([name]) => name)
        .map(([name, mask]) => ["Red " + name,25,mask,"captured preset"]);
      case "meter": return [["Signal off",0,0n,"captured"],
        ["Startup/dots",0,0x4000n,"captured group"],["Signal full",0,0x7fffn,"captured group"]];
      case "pixels": {
        const slot = Number(testSlot.value);
        if (!SonyDisplay.mapped(slot)) throw new Error("Choose a mapped character slot");
        return Array.from({length:35},(_,bit) => [
          "Pixel row " + (Math.floor(bit/5)+1) + ", column " + (bit%5+1),
          slot,1n << BigInt(bit),"generated pixel"]);
      }
      default: return SonyDisplay.indicators;
    }
  }
  function testBusy(busy) {
    for (const id of ["test-next","test-cycle","test-group","test-slot","apply-display"])
      document.getElementById(id).disabled = busy;
    document.querySelectorAll("[data-sample]").forEach(button => {
      if (!["stop","blank"].includes(button.dataset.sample)) button.disabled = busy;
    });
  }
  async function testOne(item, index, total) {
    const [name, slot, mask, confidence] = item;
    const frame = Array(26).fill(0n);
    frame[slot] = mask;
    await transmit(frame);
    words.splice(0,26,...frame);
    entries.forEach(entry => entry.field.value = "");
    red.value = "";
    render();
    testStatus.textContent = (index+1) + "/" + total + " — " + name +
      "; slot " + slot + ", code 0x" + SonyDisplay.hex(mask) + "; " + confidence +
      ". Observe and record which actual element lights.";
  }
  async function stopTest() {
    ++testGeneration;
    if (testTask) await testTask;
    await window.screenRequest({method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({sample:"blank"})});
    testStatus.textContent = "Test stopped; grid blanking requested.";
  }
  groupSelector.addEventListener("change", () => testIndex = 0);
  testSlot.addEventListener("change", () => testIndex = 0);
  function launchTest(cycle) {
    if (testTask) return;
    const token = ++testGeneration;
    testBusy(true);
    testTask = (async () => {
      try {
        const items = testItems();
        const count = cycle ? items.length : 1;
        for (let step = 0; step < count && token === testGeneration; ++step) {
          const index = cycle ? step : testIndex % items.length;
          await testOne(items[index], index, items.length);
          testIndex = (index+1) % items.length;
          if (cycle) {
            await new Promise(resolve => setTimeout(resolve,1000));
          }
        }
        if (cycle && token === testGeneration) {
          await window.screenRequest({method:"POST",headers:{"Content-Type":"application/json"},
            body:JSON.stringify({sample:"blank"})});
          testStatus.textContent += " Cycle complete; grid blanking requested.";
        }
      } catch (error) {
        testStatus.textContent = error.message;
        try {
          await window.screenRequest({method:"POST",headers:{"Content-Type":"application/json"},
            body:JSON.stringify({sample:"blank"})});
        } catch (blankError) {
          testStatus.textContent += " Blanking request failed: " + blankError.message;
        }
      } finally {
        testBusy(false);
      }
    })();
    testTask.finally(() => testTask = null);
  }
  document.getElementById("test-next").addEventListener("click", () => launchTest(false));
  document.getElementById("test-cycle").addEventListener("click", () => launchTest(true));
  document.getElementById("test-stop").addEventListener("click", () => {
    stopTest().catch(error => testStatus.textContent = error.message);
  });
  document.querySelectorAll("[data-sample]").forEach(button => {
    button.addEventListener("click", () => {
      if (testTask) stopTest().catch(error => testStatus.textContent = error.message);
    });
  });
  document.getElementById("apply-display").addEventListener("click", async () => {
    try {
      await transmit(words);
    } catch (error) { status.textContent = error.message; }
  });
  const memoryOnly = document.getElementById("timing-memory");
  if (memoryOnly) memoryOnly.addEventListener("click", () => {
    words.fill(0n);
    words[25] = 2n;
    entries.forEach(entry => entry.field.value = "");
    render();
  });
  render();
}
