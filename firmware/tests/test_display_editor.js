const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const display = require("../data/www/display-editor.js");
const words = Array(26).fill(0n);
for (let level = 1; level <= 7; ++level) {
  const word = display.barGlyph(level);
  for (let row = 0; row < 7; ++row)
    for (let column = 0; column < 5; ++column)
      assert.equal(Boolean(word & (1n << BigInt(row * 5 + column))),
        row >= 7 - level);
  assert.ok(word < (1n << 35n));
}
for (const invalid of [0,8,-1,1.5,NaN,"1"])
  assert.throws(() => display.barGlyph(invalid), RangeError);
display.place(words, "Frequency", 22, 9);
assert.deepEqual(words.slice(14,23).map(display.hex), [
  "03A1E8C400","078210F800","046319B400","0783F8B800","05B318C400",
  "043D18F800","0783F8B800","0084236800","004217843F"
]);
assert.equal(display.glyph("N"), 0x04639ace31n);
assert.equal(display.glyph("o"), 0x03a318b800n);
assert.equal(display.glyph("é"), display.glyph("e\u0301"));
assert.notEqual(display.glyph("é"), display.glyph("e"));
assert.equal(display.glyph("♪"), 0x00ce6a5184n);
assert.deepEqual(display.unicodeFont["\u{1F508}"].rows,
  ["...#.","..##.","####.","####.","####.","..##.","...#."]);
assert.equal(display.glyph("\u{1F508}\uFE0F"), display.glyph("\u{1F508}"));
assert.deepEqual(display.unicodeFont["\uE000"].rows,
  [".###.","#...#","..#..",".#.#.","..#..",".###.","#####"]);
assert.equal(display.glyph("♥\uFE0F"), display.glyph("♥"));
assert.equal(display.glyph("ḿ"), display.glyph("m"));
assert.equal(display.glyph("👩‍🎤"), display.glyph("*"));
assert.equal(display.glyph("漢"), display.glyph("*"));
assert.equal(display.glyph("\u0301"), display.glyph("*"));
assert.equal(display.glyph("\uFFFD"), display.glyph("*"));
assert.notEqual(display.glyph("*"), display.glyph("?"));
assert.equal(display.glyph("H\u0301"), display.glyph("H"));
assert.equal(display.glyph("é"), display.unicodeFont["é"].word);
for (const {rows, word} of Object.values(display.unicodeFont)) {
  assert.equal(rows.length, 7);
  let decoded = 0n;
  rows.forEach((row, r) => {
    assert.match(row, /^[.#]{5}$/);
    Array.from(row).forEach((pixel, c) => {
      if (pixel === "#") decoded |= 1n << BigInt(r * 5 + c);
    });
  });
  assert.equal(word, decoded);
}
display.place(words, "e\u0301👩‍🎤Ø", 22, 4);
assert.deepEqual(words.slice(19,23), [
  0n, display.glyph("Ø"), display.glyph("*"), display.glyph("é")
]);
display.place(words, Buffer.from("Björk 中文 ♪", "utf8").toString("utf8"), 22, 9);
assert.deepEqual(words.slice(14,23).reverse(), [
  display.glyph("B"), display.glyph("j"), display.glyph("ö"), display.glyph("r"),
  display.glyph("k"), 0n, display.glyph("*"), display.glyph("*"), 0n
]);
display.place(words, "No DSR", 22, 9);
assert.equal(words[22], display.glyph("N"));
assert.equal(words[21], display.glyph("o"));
assert.equal(words[14], 0n);
assert.equal(words[23], 0n);
assert.equal(display.hex(display.glyph(".")), "0080000000");
for (const character of "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") {
  assert.notEqual(display.glyph(character), 0n);
  assert.ok(display.glyph(character) < (1n << 35n));
}
assert.equal(display.hex((1n << 34n) | display.red.C), "0400C08400");
const memory = display.indicators.find(([name]) => name === "MEMORY");
let red = display.red.A | memory[2];
red = (red & memory[2]) | display.red.C;
assert.equal(red, 0xc08402n);
assert.ok(display.mapped(14));
assert.ok(!display.mapped(13));
assert.ok(!display.mapped(2));

class Element {
  constructor() {
    this.children = [];
    this.events = {};
    this.style = {};
    this.value = "";
    this.checked = false;
    this.classList = {toggle() {}};
  }
  append(...items) { this.children.push(...items); }
  appendChild(item) { this.children.push(item); }
  setAttribute() {}
  addEventListener(event, handler) { this.events[event] = handler; }
}
const elements = {};
const document = {
  getElementById(id) { return elements[id] ||= new Element(); },
  createElement() { return new Element(); },
  createTextNode(text) { return {textContent:text}; },
  querySelectorAll() { return []; }
};
const requests = [];
vm.runInNewContext(fs.readFileSync(require.resolve("../data/www/display-editor.js"), "utf8"), {
  document, setTimeout: callback => setImmediate(callback),
  window:{screenRequest: async options => requests.push(JSON.parse(options.body))}
});
assert.equal(requests.length, 0, "Opening the editor must not start output");
assert.equal(elements.characters.children.length, 22);
const first = elements.characters.children[0];
assert.equal(first.children[0].textContent, "P01 / slot 22");
assert.equal(first.children[2].children.length, 35);
assert.equal(elements["red-matrix"].children.length, 35);
assert.equal(elements["red-tens"].children.length, 7);
assert.equal(elements["red-units"].children.length, 7);
const unmapped = elements.characters.children.find(block =>
  block.children[0].textContent.endsWith("slot 13"));
assert.ok(unmapped.children[1].disabled);
document.getElementById("message").value = "e\u0301👩‍🎤Ø";
elements["set-message"].events.click();
assert.equal(first.children[1].value, "é");
assert.equal(elements.characters.children[1].children[1].value, "👩‍🎤");
assert.equal(elements.characters.children[2].children[1].value, "Ø");
first.children[1].value = "e\u0301";
first.children[1].events.input();
assert.match(elements["display-codes"].textContent,
  new RegExp("Slot 22: 0x" + display.hex(display.glyph("é"))));
document.getElementById("message").value = "No DSR";
elements["set-message"].events.click();
first.children[2].children[34].events.click();
document.getElementById("live-confirmed").checked = true;
elements["apply-display"].events.click().then(async () => {
  assert.equal(requests.length, 1);
  assert.equal(requests[0].live_confirmed, true);
  assert.equal(requests[0].words.length, 26);
  assert.equal(requests[0].words[22], display.hex(display.glyph("N") ^ (1n << 34n)));
  assert.equal(requests[0].words[13], "0000000000");
  const unknown = elements["glass-top"].children.flatMap(group => group.children)
    .find(button => button.textContent === "STEREO");
  unknown.events.click();
  assert.match(elements["element-info"].textContent, /No word changed/);
  assert.equal(requests.length, 1);
  elements["test-next"].events.click();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(requests[1].words[25], "0000000002");
  assert.equal(requests[1].words.filter(word => word !== "0000000000").length, 1);
  assert.match(elements["test-status"].textContent, /MEMORY/);
  elements["test-group"].value = "red";
  elements["test-group"].events.change();
  elements["test-next"].events.click();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(requests[2].words[25], "0000000400");
  elements["test-stop"].events.click();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(requests[3].sample, "blank");
  elements["test-group"].value = "letters";
  elements["test-group"].events.change();
  elements["test-cycle"].events.click();
  for (let turn = 0; turn < 12; ++turn)
    await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(requests.slice(4,8).map(request => request.words[25]),
    ["0000478C00","0000A61C00","0000C08400","0000A41C00"]);
  assert.equal(requests[8].sample, "blank", "Finite cycle must blank on completion");
  assert.equal(elements["test-next"].disabled, false);
  const timingElements = {};
  const timingDocument = {
    getElementById(id) { return timingElements[id] ||= new Element(); },
    createElement() { return new Element(); }
  };
  timingDocument.getElementById("timing-mode").value = "scan";
  const saved = new Map();
  const context = {
    document:timingDocument, window:{}, status:{},
    localStorage:{setItem:(k,v) => saved.set(k,v), getItem:k => saved.get(k)}
  };
  const html = fs.readFileSync(require.resolve("../data/www/screen.html"), "utf8");
  vm.runInNewContext(html.slice(html.indexOf("const timingFields ="), html.indexOf("</script>")), context);
  let config = context.window.screenTimingRequest();
  assert.equal(config.sample, "Timing");
  assert.equal(config.timing.off_ns, 190750);
  assert.match(timingElements["timing-estimate"].textContent, /301.000 us/);
  timingElements["timing-fast"].onclick();
  assert.match(timingElements["timing-estimate"].textContent, /154.000 us/);
  timingElements["timing-repeat"].onclick();
  assert.equal(context.window.screenTimingRequest().timing.pulses, 4);
  timingElements["timing-hold"].onclick();
  config = context.window.screenTimingRequest();
  assert.equal(config.timing.hold, 1);
  assert.equal(config.timing.off_ns, 1450000);
  timingElements["timing-save"].onclick();
  timingElements["timing-baseline"].onclick();
  timingElements["timing-load"].onclick();
  assert.equal(context.window.screenTimingRequest().timing.hold, 1);
  timingElements["timing-mode"].value = "legacy";
  assert.equal(Object.keys(context.window.screenTimingRequest()).length, 0);
  // The same editor and validation controls must honor the selected timing driver.
  requests.length = 0;
  const timingWindow = {screenRequest:async options => requests.push(JSON.parse(options.body)),
    screenTimingRequest:() => ({sample:"Timing", timing:{pulses:4}})};
  vm.runInNewContext(fs.readFileSync(require.resolve("../data/www/display-editor.js"), "utf8"),
    {document, window:timingWindow, setTimeout:callback => setImmediate(callback)});
  await elements["apply-display"].events.click();
  assert.equal(requests[0].sample, "Timing");
  assert.equal(requests[0].timing.pulses, 4);
  const profileElements = {};
  class ProfileElement extends Element {
    querySelectorAll() {
      return this.children.flatMap(child => child.buttons || []);
    }
    set innerHTML(html) {
      this.buttons = [...html.matchAll(/data-gpio="(\d+)" data-level="(\d+)"/g)]
        .map(([,gpio,level]) => {
          const button = new ProfileElement();
          button.dataset = {gpio,level};
          return button;
        });
    }
    set textContent(text) { this.text = text; this.children = []; }
    get textContent() { return this.text; }
  }
  const profileDocument = {
    getElementById(id) { return profileElements[id] ||= new ProfileElement(); },
    createElement() { return new ProfileElement(); },
    querySelectorAll(selector) {
      return selector === "[data-gpio]" ?
        this.getElementById("pin-controls").querySelectorAll() : [];
    }
  };
  const profileRequests = [];
  let state = {running:false, pin_profile:"original", pin_profile_name:"Original",
    pins:[4,5,6,15,16,7].map(gpio => ({gpio,level:0}))};
  const profileContext = {
    document:profileDocument, window:{}, AbortSignal,
    setTimeout() {},
    fetch:async (_url, options) => {
      if (options.body) profileRequests.push(JSON.parse(options.body));
      return {ok:true, json:async () => state};
    }
  };
  vm.runInNewContext(html.slice(html.indexOf("const status ="), html.indexOf("const timingFields =")),
    profileContext);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(profileRequests.length, 0, "Opening page must not change profile or start output");
  const selector = profileElements["pin-profile"];
  selector.value = "ahct-v2";
  await profileContext.window.screenRequest();
  assert.equal(selector.value, "ahct-v2", "Polling must preserve an unapplied selection");
  profileDocument.getElementById("confirmed").checked = true;
  await profileElements["pin-profile-apply"].events.click();
  assert.deepEqual(profileRequests[0], {pin_profile:"ahct-v2",bench_confirmed:true});
  profileDocument.getElementById("live-confirmed").checked = true;
  state = {running:false, pin_profile:"ahct-v2", pin_profile_name:"AHCT v2",
    pins:[2,1,40,38,21,39].map(gpio => ({gpio,level:0}))};
  await profileContext.window.screenRequest();
  const buttons = profileElements["pin-controls"].querySelectorAll();
  assert.deepEqual(buttons.map(button => Number(button.dataset.gpio)),
    [2,2,1,1,40,40,38,38,21,21,39,39]);
  assert.equal(profileElements.confirmed.checked, false);
  assert.equal(profileElements["live-confirmed"].checked, false);
  profileElements.confirmed.checked = true;
  await buttons[4].events.click();
  assert.deepEqual(profileRequests[1], {gpio:40,level:0,bench_confirmed:true});
  state = {...state,running:true};
  await profileContext.window.screenRequest();
  assert.equal(selector.disabled, true);
  assert.equal(profileElements["pin-profile-apply"].disabled, true);
  assert.ok(buttons.every(button => button.disabled));
  console.log("Display editor encoding and interaction checks passed");
}).catch(error => { console.error(error); process.exitCode = 1; });
