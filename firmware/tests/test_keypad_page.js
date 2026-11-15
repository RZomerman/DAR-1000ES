const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const html = fs.readFileSync("data/www/keypad.html", "utf8");
const elements = {};
const requests = [];
const state = {present:true, scanning:false, hardware_scan_active:false,
  rows:0, columns:0, errors:0, overflows:0, last_error:"ESP_OK", events:[]};
const document = {getElementById(id) {
  if (!elements[id]) elements[id] = {value:"1", checked:false,
    addEventListener(name, callback) { this[name] = callback; }};
  return elements[id];
}};
const context = vm.createContext({document, setTimeout() {},
  fetch: async (url, options) => {
    requests.push({url, options});
    return {ok:true, json:async () => state};
  }});
vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context);
(async () => {
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(requests.filter(r => r.options.method === "POST").length, 0);
  await vm.runInContext("command(true)", context);
  assert.match(elements.error.textContent, /Confirm/);
  assert.equal(requests.filter(r => r.options.method === "POST").length, 0);
  elements.confirm.checked = true;
  document.getElementById("rows").value = "3";
  document.getElementById("columns").value = "4";
  await vm.runInContext("command(true)", context);
  assert.deepEqual(JSON.parse(requests.at(-1).options.body),
    {enable:true, rows:3, columns:4, isolated_matrix_confirmed:true});
  elements.remember.checked = true;
  elements.rows.value = "6";
  elements.columns.value = "9";
  await vm.runInContext("command(true)", context);
  assert.deepEqual(JSON.parse(requests.at(-1).options.body),
    {enable:true, rows:6, columns:9, isolated_matrix_confirmed:true, remember:true});
  await vm.runInContext("command(false)", context);
  assert.deepEqual(JSON.parse(requests.at(-1).options.body), {enable:false});
  vm.runInContext(`render({present:true,scanning:true,hardware_scan_active:true,
    rows:3,columns:4,errors:0,overflows:0,last_error:"ESP_OK",
    events:[{sequence:1,pressed:true,row:2,column:3,code:24},
            {sequence:2,pressed:false,row:2,column:3,code:24}]})`, context);
  assert.match(elements.events.textContent, /PRESS  ROW2 COL3/);
  assert.match(elements.events.textContent, /RELEASE  ROW2 COL3/);
  console.log("Keypad UI safety and event checks passed");
})().catch(error => { console.error(error); process.exitCode = 1; });
