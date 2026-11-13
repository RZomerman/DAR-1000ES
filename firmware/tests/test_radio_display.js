const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const html = fs.readFileSync("data/www/radio.html", "utf8");
class Element {
  constructor() {
    this.events = {};
    this.value = "0.5";
    this.checked = false;
    this.disabled = false;
  }
  addEventListener(name, callback) { this.events[name] = callback; }
  setAttribute() {}
}
const elements = {};
const requests = [];
const state = {pin_profile:"ahct-v2", pin_profile_name:"AHCT v2",
  running:false, now_playing:false, timing_grid_duty:0.005};
const document = {
  getElementById(id) {
    if (!elements[id]) {
      elements[id] = new Element();
      if (id === "vfd-mode") elements[id].value = "now_playing";
    }
    return elements[id];
  },
  querySelectorAll() { return []; }
};
const context = vm.createContext({document, console, AbortSignal,
  setTimeout() {},
  fetch: async (url, options) => {
    requests.push({url, options});
    return {ok:true, json:async () => url === "/api/screen" ? {...state} :
      {source:"Radio", station:"Station", airplay_available:false}};
  }
});
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)];
vm.runInContext(scripts.at(-1)[1], context);
(async () => {
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(elements["vfd-start"].disabled, true);
  assert.equal(requests.filter(r => r.options?.method === "POST").length, 0,
    "Loading the webpage must never start outputs");
  assert.equal(elements["airplay-help"].hidden, true);
  for (const percent of [0.25, 0.5, 1]) {
    const timing = vm.runInContext(`screenTiming(${percent})`, context);
    const transfer = timing.blank_before_ns + 41 * (
      timing.setup_ns + timing.clock_low_ns + timing.clock_high_ns) +
      timing.strobe_ns + timing.latch_guard_ns + timing.blank_after_ns;
    const slot = transfer + timing.on_ns + timing.off_ns;
    assert.equal(slot, 300000);
    assert.equal(timing.on_ns / (26 * slot), percent / 100);
  }
  for (const invalid of [-1, 0, 3.75, NaN, Infinity])
    assert.throws(() => vm.runInContext(`screenTiming(${invalid})`, context));
  await vm.runInContext("screenCommand(false)", context);
  assert.equal(requests.filter(r => r.options?.method === "POST").length, 0);
  elements["vfd-confirm"].checked = true;
  elements["vfd-confirm"].events.change();
  assert.equal(elements["vfd-start"].disabled, false);
  await vm.runInContext("screenCommand(false)", context);
  const command = JSON.parse(requests.at(-1).options.body);
  assert.equal(command.now_playing, true);
  assert.equal(command.visualization, "now_playing");
  assert.equal(command.live_confirmed, true);
  assert.equal(command.timing.on_ns, 39000);
  assert.equal(command.words.length, 26);
  elements["vfd-mode"].value = "spectrum";
  assert.equal(requests.filter(r => r.options?.method === "POST").length, 1,
    "Selecting a mode must not start output");
  await vm.runInContext("screenCommand(false)", context);
  assert.equal(JSON.parse(requests.at(-1).options.body).visualization, "spectrum");
  vm.runInContext('renderScreen({...screenState,running:true,now_playing:true,visualization:"spectrum"})', context);
  assert.match(elements["vfd-status"].textContent, /11 LEFT \+ 11 RIGHT/);
  elements["vfd-mode"].value = "invalid";
  const beforeInvalid = requests.length;
  await vm.runInContext("screenCommand(false)", context);
  assert.equal(requests.length, beforeInvalid);
  elements["vfd-confirm"].checked = false;
  await vm.runInContext("screenCommand(true)", context);
  assert.equal(JSON.parse(requests.at(-1).options.body).sample, "stop");
  elements["vfd-mode"].value = "spectrum";
  elements["vfd-confirm"].checked = true;
  let releaseStart;
  const normalFetch = context.fetch;
  context.fetch = async (url, options) => {
    if (options?.method === "POST" && JSON.parse(options.body).sample === "Timing")
      await new Promise(resolve => { releaseStart = resolve; });
    return normalFetch(url, options);
  };
  const pendingStart = vm.runInContext("screenCommand(false)", context);
  await vm.runInContext("screenCommand(true)", context);
  releaseStart();
  await pendingStart;
  assert.deepEqual(requests.slice(-2).map(r => JSON.parse(r.options.body).sample),
    ["Timing", "stop"], "Stop during a pending start must not be discarded");
  vm.runInContext('renderScreen({...screenState,pin_profile:"original"})', context);
  assert.equal(elements["vfd-confirm"].checked, false);
  assert.equal(elements["vfd-start"].disabled, true);
  console.log("Radio display UI checks passed");
})().catch(error => { console.error(error); process.exitCode = 1; });
