const assert = require("node:assert/strict");
const {Inputs, draw} = require("../data/www/front-panel.js");

(async () => {
  const requests = [];
  let now = 0;
  const input = new Inputs(async body => {
    requests.push({body, at:now});
    return {virtual_session:7, virtual_enabled:true};
  }, async ms => { now += ms; }, () => now);
  assert.equal(input.down(5), false, "Never inject input before explicit arming");
  await assert.rejects(input.arm(false));
  assert.equal(requests.length, 0, "Missing isolation confirmation sends no request");
  await input.arm(true);
  assert.equal(input.down(5), true);
  assert.equal(input.down(6), false, "No overlapping virtual switch pulses");
  await input.up();
  assert.deepEqual(requests.slice(-2).map(item => item.body), [
    {virtual_key:5, pressed:true, session:7},
    {virtual_key:5, pressed:false, session:7, cancel:false}
  ]);
  assert.ok(requests.at(-1).at - requests.at(-2).at >= 120,
    "Quick browser clicks still pass the real hardware debounce");
  assert.equal(input.down(58), true);
  await input.held.ready;
  now += 1300;
  await input.up();
  assert.deepEqual(requests.at(-1).body,
    {virtual_key:58, pressed:false, session:7, cancel:false},
    "Long presses remain raw switch events, not browser-invented DIM actions");
  input.down(58);
  await input.up(true);
  assert.equal(requests.at(-1).body.cancel, true);
  await input.keepalive();
  assert.deepEqual(requests.at(-1).body, {virtual_keepalive:true, session:7});
  await input.disarm();
  assert.deepEqual(requests.at(-1).body, {virtual_enable:false, session:7});
  assert.equal(input.down(25), false);

  const rectangles = [];
  const ctx = {fillStyle:"", fillRect(...args) {
    rectangles.push({color:this.fillStyle, args});
  }, fillText() {}};
  const words = Array(26).fill(0n);
  words[22] = 1n << 34n;
  draw({width:1380, height:160, getContext:() => ctx}, words, "");
  const lit = rectangles.filter(item => item.color === "#a8ffd1");
  assert.equal(lit.length, 1);
  assert.deepEqual(lit[0].args, [70,108,7,7],
    "Actual output bit 34 is bottom-right of physical leftmost matrix");
  console.log("Hardware-backed front-panel input and pixel checks passed");
})().catch(error => { console.error(error); process.exitCode = 1; });
