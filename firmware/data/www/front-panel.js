/* Virtual keys feed the firmware's physical-key decoder; pixels come from DMA frame words. */
const SonyFrontPanel = (() => {
  class Inputs {
    constructor(request, sleep, now) {
      this.request = request;
      this.sleep = sleep;
      this.now = now;
      this.session = 0;
      this.held = null;
    }
    async arm(confirmed) {
      if (!confirmed) throw new Error("Disconnect all Sony signal wires and disable buffers first.");
      const state = await this.request({virtual_enable:true, outputs_disconnected_confirmed:true});
      this.session = state.virtual_session;
      await this.sleep(240);
    }
    async disarm() {
      const session = this.session;
      try {
        if (session) await this.request({virtual_enable:false, session});
      } finally {
        this.session = 0;
        this.held = null;
      }
    }
    async keepalive() {
      if (this.session) await this.request({virtual_keepalive:true, session:this.session});
    }
    down(code) {
      if (!this.session || this.held) return false;
      const held = {code, start:0, releasing:false};
      this.held = held;
      held.ready = this.request({virtual_key:code, pressed:true, session:this.session})
        .then(() => { held.start = this.now(); })
        .catch(error => { if (this.held === held) this.held = null; throw error; });
      return true;
    }
    async up(cancel = false) {
      const held = this.held;
      if (!held || held.releasing) return;
      held.releasing = true;
      try {
        await held.ready;
        if (!cancel) await this.sleep(Math.max(0, 120 - (this.now() - held.start)));
        await this.request({virtual_key:held.code, pressed:false, session:this.session, cancel});
        await this.sleep(100);
      } finally {
        if (this.held === held) this.held = null;
      }
    }
  }

  function draw(canvas, words, labels) {
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const width = canvas.width, height = canvas.height;
    ctx.fillStyle = "#071713";
    ctx.fillRect(0, 0, width, height);
    const step = width / 138, top = height * .3;
    for (let cell = 0; cell < 22; ++cell) {
      const word = BigInt(words[22 - cell] || 0);
      for (let row = 0; row < 7; ++row) {
        for (let column = 0; column < 5; ++column) {
          ctx.fillStyle = word & (1n << BigInt(row * 5 + column)) ? "#a8ffd1" : "#16372a";
          ctx.fillRect((3 + cell * 6 + column) * step, top + row * step, step * .7, step * .7);
        }
      }
    }
    ctx.fillStyle = "#91d8b3";
    ctx.font = "16px monospace";
    ctx.fillText(labels, 24, height - 22);
  }

  function mount(document, window) {
    const $ = id => document.getElementById(id);
    const sleep = ms => new Promise(resolve => window.setTimeout(resolve, ms));
    let keypad = {}, screen = {}, changing = false, lastKeepalive = 0;
    let requestTail = Promise.resolve();
    const fail = error => { $("demo-notice").textContent = error.message; };
    const request = body => {
      const run = async () => {
        const response = await window.fetch("/api/keypad", {
          cache:"no-store", signal:AbortSignal.timeout(5000),
          ...(body ? {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(body)} : {})
        });
        if (!response.ok) throw new Error(await response.text());
        keypad = await response.json();
        return keypad;
      };
      const pending = requestTail.then(run);
      requestTail = pending.catch(() => {});
      return pending;
    };
    const inputs = new Inputs(request, sleep, () => window.performance.now());
    const groups = {
      power:["POWER", "PROGRAM", "CHECK"], display:["DISPLAY MODE"],
      category:["NEWS", "AFFAIRS", "INFO", "SPORT", "EDUCATE", "DRAMA",
        "CULTURE", "SCIENCE", "VARIED", "POP M", "ROCK M", "M.O.R. M",
        "MUSIC", "SPEECH", "LIGHT M", "CLASSICS", "OTHER M", "UNDEFINED"],
      block:["MEMORY", "BLOCK -", "BLOCK +"],
      presets:[...Array.from({length:16}, (_, i) => String(i + 1)), "MHz/CLEAR", "MONO MODE"],
      link:["DIRECT", "LINK"], fine:["FINE -", "FINE +"]
    };
    const buttons = [];
    const binding = name => keypad.bindings?.find(item => item.name === name);
    for (const [group, keys] of Object.entries(groups)) {
      for (const key of keys) {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = key;
        button.dataset.key = key;
        button.disabled = true;
        const down = () => {
          const item = binding(key);
          if (!item || !inputs.down(item.code)) return false;
          button.classList.add("pressed");
          inputs.held.ready.catch(fail);
          return true;
        };
        const up = cancel => {
          if (inputs.held?.code !== binding(key)?.code) return;
          button.classList.remove("pressed");
          inputs.up(cancel).catch(fail);
        };
        button.addEventListener("pointerdown", event => {
          if (event.button !== 0) return;
          event.preventDefault();
          if (down()) button.setPointerCapture(event.pointerId);
        });
        button.addEventListener("pointerup", () => up(false));
        button.addEventListener("pointercancel", () => up(true));
        button.addEventListener("lostpointercapture", () => {
          if (!inputs.held?.releasing) up(true);
        });
        button.addEventListener("keydown", event => {
          if (!["Enter"," "].includes(event.key)) return;
          event.preventDefault();
          if (!event.repeat) down();
        });
        button.addEventListener("keyup", event => {
          if (["Enter"," "].includes(event.key)) { event.preventDefault(); up(false); }
        });
        button.addEventListener("click", event => {
          if (event.detail === 0 && down()) up(false);
        });
        $("demo-" + group).appendChild(button);
        buttons.push(button);
      }
    }
    const controls = () => {
      $("demo-enabled").checked = inputs.session !== 0;
      $("demo-enabled").disabled = changing || !Array.isArray(keypad.bindings);
      $("hardware-bench").hidden = inputs.session !== 0;
      buttons.forEach(button => {
        const item = binding(button.dataset.key);
        button.disabled = changing || !inputs.session || !item;
        button.title = item ? `TCA8418 code ${item.code}: R${Math.floor((item.code - 1) / 10)} ` +
          `C${(item.code - 1) % 10}; ` + (item.action ? "real firmware action" : "logged; no action assigned") :
          "Physical input mapping not verified; disabled";
      });
      $("demo-pins").textContent = (screen.pins || []).map((pin, i) =>
        ["GDATA","GCLK","CLEAR","STB","SDATA","SCLK"][i] + "=GPIO" + pin.gpio).join(" / ");
      $("demo-state").textContent = (keypad.events || []).slice(-6).map(event =>
        `${event.virtual ? "Virtual" : "Physical"} ${event.pressed ? "PRESS" : "RELEASE"} ` +
        `code ${event.code} (R${event.row} C${event.column})`).join(" | ");
    };
    $("demo-enabled").checked = false;
    $("demo-enabled").addEventListener("change", async () => {
      if (changing) return;
      const enable = $("demo-enabled").checked;
      changing = true;
      $("demo-notice").textContent = enable ? "Arming virtual keypad input..." : "Disarming virtual keypad input...";
      controls();
      try {
        if (enable) await inputs.arm($("demo-confirm").checked);
        else { await inputs.up(true); await inputs.disarm(); }
        $("demo-notice").textContent = inputs.session ?
          "REAL firmware input armed. DISPLAY/CHECK can start real GPIO scanning on disconnected pins." :
          "Read-only output preview; virtual inputs disarmed.";
      } catch (error) { fail(error); }
      finally { changing = false; controls(); }
    });
    $("demo-stop").addEventListener("click", async () => {
      changing = true;
      controls();
      try {
        await requestTail;
        await inputs.up(true);
        if (inputs.session) await inputs.disarm();
        else if (keypad.virtual_enabled)
          await request({virtual_enable:false, session:keypad.virtual_session});
      } catch (error) { fail(error); }
      finally {
        await window.screenRequest({method:"POST", headers:{"Content-Type":"application/json"},
          body:JSON.stringify({sample:"stop"})}).catch(fail);
        changing = false;
        if (!inputs.session)
          $("demo-notice").textContent = "Output stopped; virtual inputs disarmed.";
        controls();
      }
    });
    window.addEventListener("blur", () => {
      buttons.forEach(button => button.classList.remove("pressed"));
      inputs.up(true).catch(fail);
    });
    const refresh = async () => {
      try {
        await request();
        if (inputs.session && (!keypad.virtual_enabled || keypad.virtual_session !== inputs.session)) {
          inputs.session = 0;
          inputs.held = null;
          $("demo-notice").textContent = "Virtual session ended; confirm isolation and arm again.";
        }
        if (inputs.session && window.performance.now() - lastKeepalive >= 2000) {
          await inputs.keepalive();
          lastKeepalive = window.performance.now();
        }
        controls();
      } catch (error) { fail(error); }
      window.setTimeout(refresh, 1000);
    };
    refresh();
    return {
      pollInterval:100,
      observe(state) {
        screen = state;
        const available = Array.isArray(state.output_words) && state.output_words.length === 26;
        const words = available && state.output_frame_valid ?
          state.output_words.map(word => BigInt("0x" + word)) : Array(26).fill(0n);
        draw($("demo-canvas"), words, available ?
          `${state.output_frame_valid ? "REAL OUTPUT" : "OUTPUT OFF"}  ${state.display_mode_label || ""}  ` +
          `grid duty ${(100 * (state.timing_grid_duty || 0)).toFixed(3)}%` : "Update firmware for real output preview");
        $("demo-raw").textContent = words.map((word, i) =>
          "Slot " + i + ": " + word.toString(16).padStart(10,"0")).join("\n");
        controls();
      }
    };
  }
  return {Inputs, draw, mount};
})();
if (typeof module !== "undefined") module.exports = SonyFrontPanel;
if (typeof document !== "undefined" && document.getElementById("travel-demo"))
  window.sonyFrontPanel = SonyFrontPanel.mount(document, window);
