/* The travel front panel is a browser-only simulator. It never sends commands. */
const SonyFrontPanel = (() => {
  const display = typeof module !== "undefined" ? require("./display-editor.js") : SonyDisplay;
  const modes = ["now_playing", "stream_source", "audio_format", "bitrate",
    "station_genre", "stream_url", "esp_ip", "equalizer", "vu_stereo", "vu_center", "off"];
  const categories = ["NEWS", "AFFAIRS", "INFO", "SPORT", "EDUCATE", "DRAMA",
    "CULTURE", "SCIENCE", "VARIED", "POP M", "ROCK M", "M.O.R. M",
    "LIGHT M", "CLASSICS", "OTHER M"];
  const countries = ["Netherlands", "United Kingdom", "Germany", "France", "Belgium"];
  const defaults = ["NPO Radio 2", "BNR Radio", "Radio 538", "SLAM! FM"];

  class Demo {
    constructor(presets) {
      this.presets = Array.from({length:16}, (_, i) => presets?.[i] || defaults[i] || "");
      this.power = true;
      this.mode = "now_playing";
      this.dim = 0;
      this.country = 0;
      this.category = "";
      this.subcategory = 0;
      this.stationIndex = 0;
      this.station = defaults[0];
      this.title = "Travel demo";
      this.artist = "";
      this.device = "";
      this.airplay = false;
      this.mono = false;
      this.memory = false;
      this.direct = false;
      this.program = false;
      this.playing = true;
      this.phase = null;
      this.lastButton = "";
    }

    press(key, now, long = false) {
      this.lastButton = key + (long ? " (hold)" : "");
      if (key === "POWER") {
        this.power = !this.power;
        this.mode = this.power ? "now_playing" : "off";
        this.phase = null;
        this.memory = false;
        return;
      }
      if (!this.power) return;
      if (key === "DISPLAY MODE") {
        if (long) {
          if (this.mode !== "off") this.dim = (this.dim + 1) % 5;
        } else {
          this.mode = modes[(modes.indexOf(this.mode) + 1) % modes.length];
          if (this.mode === "off") this.phase = null;
        }
        return;
      }
      if (key === "CHECK") {
        if (this.mode === "off") this.mode = "vu_stereo";
        return;
      }
      if (/^(?:[1-9]|1[0-6])$/.test(key)) {
        const number = Number(key);
        if (this.memory) {
          this.presets[number - 1] = this.station;
          this.memory = false;
          this.phase = {kind:"notice", text:"Preset saved", until:now + 1500};
          return;
        }
        const station = this.presets[number - 1];
        if (station) {
          this.station = station;
          this.title = "";
          this.airplay = false;
          this.playing = false;
          this.phase = {kind:"preset", number, start:now};
        } else this.phase = {kind:"notice", text:"Unassigned", until:now + 1500};
        return;
      }
      if (key === "MEMORY") {
        this.memory = !this.memory;
        this.phase = {kind:"notice", text:this.memory ? "Choose preset" : "Memory cancelled", until:now + 1500};
      } else if (key === "MHz/CLEAR") {
        this.memory = false;
        this.phase = null;
      } else if (key === "MONO MODE") {
        this.mono = !this.mono;
      } else if (key === "LINK") {
        this.airplay = !this.airplay;
        this.title = this.artist = "";
        this.phase = null;
      } else if (key === "DIRECT") {
        this.direct = !this.direct;
      } else if (key === "PROGRAM") {
        this.program = !this.program;
      } else if (key === "UNDEFINED") {
        this.country = (this.country + 1) % countries.length;
        this.phase = {kind:"notice", text:countries[this.country], until:now + 1800};
      } else if (categories.includes(key) || key === "MUSIC" || key === "SPEECH") {
        this.subcategory = this.category === key ? (this.subcategory + 1) % 3 : 0;
        this.category = key;
        this.stationIndex = 0;
        this.phase = {kind:"notice", text:key + ["", " / Local", " / International"][this.subcategory], until:now + 1800};
      } else if (["BLOCK -", "BLOCK +", "FINE -", "FINE +"].includes(key)) {
        const direction = key.endsWith("+") ? 1 : -1;
        this.stationIndex = (this.stationIndex + direction * (key.startsWith("BLOCK") ? 10 : 1) + 100) % 100;
        this.station = (this.category || "Demo radio") + " " + (this.stationIndex + 1);
        this.title = "";
        this.airplay = false;
        this.phase = {kind:"notice", text:this.station, until:now + 1800};
      }
    }

    metadata(value) {
      this.station = String(value.station || this.station);
      this.title = String(value.title || "");
      this.artist = String(value.artist || "");
      this.device = String(value.device || "");
      this.airplay = value.source === "AirPlay";
      this.playing = value.playing !== false;
      this.phase = null;
    }

    text() {
      switch (this.mode) {
        case "now_playing": return this.airplay ?
          (this.title ? [this.artist, this.title].filter(Boolean).join(" - ") : "<<AirPlay>>") :
          this.title || this.station;
        case "stream_source": return this.airplay ? this.device || "AirPlay" : this.station;
        case "audio_format": return this.mono ? "48 kHz / MONO" : "48 kHz / STEREO";
        case "bitrate": return this.airplay ? "AirPlay / PCM" : "192 kbps / MP3";
        case "station_genre": return this.category || "Music";
        case "stream_url": return "https://demo.invalid/radio";
        case "esp_ip": return "Browser demo / no IP";
        default: return "";
      }
    }

    frame(now) {
      const words = Array(26).fill(0n);
      if (!this.power || this.mode === "off") return words;
      let text = this.text();
      if (this.phase?.kind === "notice") {
        if (now < this.phase.until) text = this.phase.text;
        else this.phase = null;
      }
      if (this.phase?.kind === "preset") {
        const elapsed = now - this.phase.start;
        if (elapsed < 1000) {
          display.place(words, "Preset", 19, 6);
          display.place(words, String(this.phase.number), 12, 2);
          return words;
        }
        if (elapsed < 2200) {
          const cell = Math.floor((elapsed - 1000) / 90) % 22;
          words[22 - cell] = display.barGlyph(3);
          return words;
        }
        this.playing = true;
        if (elapsed < 4200) text = this.station;
        else this.phase = null;
      }
      if (!this.phase && ["equalizer", "vu_stereo", "vu_center"].includes(this.mode)) {
        const left = this.playing ? Math.floor(12 + 9 * Math.sin(now / 500)) : 0;
        const right = this.playing ? Math.floor(12 + 9 * Math.sin(now / 630 + 1)) : 0;
        for (let i = 0; i < 22; ++i) {
          if (this.mode === "equalizer") {
            const level = this.playing ? 1 + Math.floor((Math.sin(now / 270 + i * .7) + 1) * 3) : 0;
            if (level) words[22 - i] = display.barGlyph(level);
          } else if (this.mode === "vu_center") {
            const count = Math.ceil((i < 11 ? left : right) / 2);
            const distance = i < 11 ? 10 - i : i - 11;
            if (distance < count) words[22 - i] = display.barGlyph(7);
          } else {
            for (let row = 0; row < 3; ++row) {
              if (i < left) words[22 - i] |= 31n << BigInt(row * 5);
              if (i < right) words[22 - i] |= 31n << BigInt((row + 4) * 5);
            }
          }
        }
        return words;
      }
      const characters = display.characters(text);
      const padded = characters.length > 22 ? [...characters, " ", " ", " "] : characters;
      const offset = characters.length > 22 ? Math.floor(now / 220) % padded.length : 0;
      const visible = Array.from({length:Math.min(22, padded.length)}, (_, i) =>
        padded[(i + offset) % padded.length]).join("");
      display.place(words, visible, 22, 22);
      return words;
    }
  }

  function draw(canvas, words, labels, dim = 0) {
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const width = canvas.width, height = canvas.height;
    ctx.fillStyle = "#071713";
    ctx.fillRect(0, 0, width, height);
    const step = width / 138, top = height * .3;
    ctx.globalAlpha = [1, .85, .7, .6, .45][dim];
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
    ctx.globalAlpha = 1;
  }

  function mount(document, window) {
    const $ = id => document.getElementById(id);
    const demo = new Demo();
    let enabled = false, liveWords = Array(26).fill(0n), held = null;
    const storageKey = "sony-travel-demo-v1";
    try {
      const saved = JSON.parse(window.localStorage.getItem(storageKey) || "null");
      if (saved?.presets?.length === 16 && saved.presets.every(x => typeof x === "string"))
        demo.presets = saved.presets;
      enabled = saved?.enabled === true;
    } catch (_) {}
    if (new URLSearchParams(window.location.search).get("demo") === "1") enabled = true;
    const save = () => {
      try { window.localStorage.setItem(storageKey, JSON.stringify({enabled, presets:demo.presets})); }
      catch (_) {}
    };
    const settings = () => {
      $("demo-station").value = demo.station;
      $("demo-title").value = demo.title;
      $("demo-artist").value = demo.artist;
      $("demo-device").value = demo.device;
      $("demo-source").value = demo.airplay ? "AirPlay" : "Radio";
    };
    const act = (key, long = false) => {
      if (!enabled) return;
      demo.press(key, window.performance.now(), long);
      settings();
      save();
    };
    const groups = {
      power:["POWER", "PROGRAM", "CHECK"],
      display:["DISPLAY MODE"],
      category:["NEWS", "AFFAIRS", "INFO", "SPORT", "EDUCATE", "DRAMA",
        "CULTURE", "SCIENCE", "VARIED", "POP M", "ROCK M", "M.O.R. M",
        "MUSIC", "SPEECH", "LIGHT M", "CLASSICS", "OTHER M", "UNDEFINED"],
      block:["MEMORY", "BLOCK -", "BLOCK +"],
      presets:[...Array.from({length:16}, (_, i) => String(i + 1)), "MHz/CLEAR", "MONO MODE"],
      link:["DIRECT", "LINK"],
      fine:["FINE -", "FINE +"]
    };
    for (const [group, keys] of Object.entries(groups)) {
      for (const key of keys) {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = key;
        button.dataset.key = key;
        button.setAttribute("aria-label", key === "DISPLAY MODE" ? "Display mode; hold to change DIM" : key);
        button.addEventListener("pointerdown", event => {
          if (!enabled || held || event.button !== 0) return;
          event.preventDefault();
          button.setPointerCapture(event.pointerId);
          held = {button, key, start:window.performance.now(), long:false};
          button.classList.add("pressed");
        });
        button.addEventListener("pointerup", () => {
          if (!held || held.button !== button) return;
          const long = held.long;
          held = null;
          button.classList.remove("pressed");
          if (!long) act(key);
        });
        const cancel = () => {
          if (held?.button === button) held = null;
          button.classList.remove("pressed");
        };
        button.addEventListener("pointercancel", cancel);
        button.addEventListener("lostpointercapture", cancel);
        button.addEventListener("click", event => {
          if (event.detail === 0) act(key);
        });
        $("demo-" + group).appendChild(button);
      }
    }
    const modeChanged = () => {
      enabled = $("demo-enabled").checked;
      held?.button.classList.remove("pressed");
      held = null;
      $("hardware-bench").hidden = enabled;
      $("demo-panel").querySelectorAll("button").forEach(button => button.disabled = !enabled);
      $("demo-fixture").querySelectorAll("input, select, button").forEach(input => input.disabled = !enabled);
      $("demo-notice").textContent = enabled ?
        "TRAVEL DEMO: local dummy data only. No audio, GPIO, keypad or UART commands." :
        "Read-only display preview. Enable travel demo to use the virtual buttons.";
      save();
    };
    $("demo-enabled").checked = enabled;
    $("demo-enabled").addEventListener("change", modeChanged);
    $("demo-apply").addEventListener("click", () => {
      if (!enabled) return;
      demo.metadata({station:$("demo-station").value, title:$("demo-title").value,
        artist:$("demo-artist").value, device:$("demo-device").value,
        source:$("demo-source").value});
    });
    $("demo-assign").addEventListener("click", () => {
      if (!enabled) return;
      const number = Number($("demo-preset").value);
      if (Number.isInteger(number) && number >= 1 && number <= 16) {
        demo.presets[number - 1] = $("demo-preset-name").value.trim();
        save();
      }
    });
    window.addEventListener("blur", () => {
      held?.button.classList.remove("pressed");
      held = null;
    });
    const tick = () => {
      const now = window.performance.now();
      if (enabled && held?.key === "DISPLAY MODE" && !held.long && now - held.start >= 700) {
        held.long = true;
        act(held.key, true);
      }
      const words = enabled ? demo.frame(now) : liveWords;
      const labels = enabled ?
        `DEMO  ${demo.mode.replaceAll("_", " ").toUpperCase()}  DIM ${[1.2,1.4,1.6,1,.75][demo.dim]}%  ` +
        (demo.airplay ? "AIRPLAY" : demo.mono ? "MONO" : "STEREO") +
        (demo.memory ? "  MEMORY" : "") + (demo.direct ? "  DIRECT" : "") :
        "READ-ONLY CONTROLLER FRAME";
      draw($("demo-canvas"), words, labels, enabled ? demo.dim : 0);
      $("demo-state").textContent = enabled ?
        `Last: ${demo.lastButton || "none"} | Country: ${countries[demo.country]} | ` +
        `Category: ${demo.category || "none"} | Station: ${demo.station}` : "";
      window.requestAnimationFrame(tick);
    };
    settings();
    modeChanged();
    window.requestAnimationFrame(tick);
    return {observe(state) {
      if (state.words?.length === 26) liveWords = state.words.map(word => BigInt("0x" + word));
    }};
  }
  return {Demo, draw, mount, modes};
})();
if (typeof module !== "undefined") module.exports = SonyFrontPanel;
if (typeof document !== "undefined" && document.getElementById("travel-demo"))
  window.sonyFrontPanel = SonyFrontPanel.mount(document, window);
