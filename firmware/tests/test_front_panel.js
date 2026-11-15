const assert = require("node:assert/strict");
const {Demo, draw, modes} = require("../data/www/front-panel.js");
const display = require("../data/www/display-editor.js");

{
  const demo = new Demo();
  demo.presets[12] = "Thirteen";
  demo.press("13", 0);
  const frame = demo.frame(500);
  for (let i = 0; i < 6; ++i)
    assert.equal(frame[19 - i], display.glyph("Preset"[i]));
  assert.equal(frame[13], 0n, "Physical cell 10 stays blank");
  assert.equal(frame[12], display.glyph("1"));
  assert.equal(frame[11], display.glyph("3"));
  assert.ok(demo.frame(1500).filter(x => x !== 0n).length === 1, "Search animation");
  const station = Array(26).fill(0n);
  display.place(station, "Thirteen", 22, 22);
  assert.deepEqual(demo.frame(2300), station);
  assert.equal(demo.playing, true);
  demo.frame(4300);
  assert.equal(demo.phase, null, "Returns to selected view after station announcement");
}
{
  const demo = new Demo();
  demo.mode = "off";
  demo.press("1", 0);
  assert.ok(demo.frame(2500).every(x => x === 0n), "Presets cannot start stopped output");
  demo.press("CHECK", 2600);
  assert.equal(demo.mode, "vu_stereo");
  demo.press("2", 3000);
  demo.press("16", 3100);
  assert.equal(demo.station, "BNR Radio", "Unassigned preset leaves selected station intact");
  const expected = Array(26).fill(0n);
  display.place(expected, "Unassigned", 22, 22);
  assert.deepEqual(demo.frame(3200), expected, "Unassigned replaces pending announcement");
}
{
  const demo = new Demo();
  const mode = demo.mode;
  demo.press("DISPLAY MODE", 0, true);
  assert.equal(demo.mode, mode);
  assert.equal(demo.dim, 1, "Hold changes DIM, not view");
  for (let i = 0; i < modes.length - 1; ++i) demo.press("DISPLAY MODE", i);
  assert.equal(demo.mode, "off");
  demo.press("DISPLAY MODE", 100, true);
  assert.equal(demo.dim, 1, "Holding DISPLAY while OFF does not change DIM");
  demo.press("CHECK", 200);
  assert.equal(demo.mode, "vu_stereo");
  demo.press("CHECK", 250);
  assert.equal(demo.mode, "vu_stereo");
  demo.press("POWER", 300);
  demo.press("1", 400);
  assert.equal(demo.phase, null, "Powered-off panel ignores preset keys");
}
{
  const demo = new Demo();
  demo.press("LINK", 0);
  assert.equal(demo.text(), "<<AirPlay>>");
  demo.metadata({source:"AirPlay", title:"Song", artist:"Artist", device:"Demo phone"});
  assert.equal(demo.text(), "Artist - Song");
  demo.mode = "stream_source";
  assert.equal(demo.text(), "Demo phone");
  demo.metadata({source:"AirPlay"});
  assert.equal(demo.text(), "AirPlay", "Unavailable device identity is not invented");
  demo.press("NEWS", 0);
  demo.press("BLOCK +", 2000);
  demo.press("MEMORY", 3000);
  demo.press("16", 3100);
  const assigned = demo.presets[15];
  assert.equal(assigned, demo.station);
  demo.press("1", 5000);
  demo.press("16", 6000);
  assert.equal(demo.station, assigned);
}
{
  const demo = new Demo();
  demo.mode = "vu_stereo";
  const stereo = demo.frame(0);
  assert.ok(stereo.slice(1,23).every(word => (word & (31n << 15n)) === 0n),
    "Stereo VU has separated top/bottom banks, not equalizer bars");
  demo.mode = "vu_center";
  const center = demo.frame(0);
  assert.equal(center[12], display.barGlyph(7));
  assert.equal(center[11], display.barGlyph(7));
  assert.equal(center[22], 0n, "Center VU grows outward from the middle");
  demo.playing = false;
  assert.ok(demo.frame(0).every(word => word === 0n));
}
{
  const rectangles = [];
  const ctx = {globalAlpha:1, fillStyle:"", fillRect(...args) {
    rectangles.push({color:this.fillStyle, args});
  }, fillText() {}};
  const words = Array(26).fill(0n);
  words[22] = 1n << 34n;
  draw({width:1380, height:160, getContext:() => ctx}, words, "");
  const lit = rectangles.filter(x => x.color === "#a8ffd1");
  assert.equal(lit.length, 1);
  assert.deepEqual(lit[0].args, [70,108,7,7], "Bit 34 is bottom-right of physical leftmost cell");
}
console.log("Front-panel demo behavior passed");
