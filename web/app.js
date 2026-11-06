// MiniSharp in the browser: the compiler and VM run as WebAssembly (minisharp.wasm, built from the same
// C sources as the 3DS app). This file gives it files, drawing, input and sound.
"use strict";

const $ = (s) => document.querySelector(s);
const enc = new TextEncoder(), dec = new TextDecoder();
const BUILD = "__BUILD__";                      // replaced by web/build.sh
const PROJECT_DIR = "/p";

// ---------------------------------------------------------------- in-memory file system (WASI)
const fsFiles = new Map();          // "/p/Game.cs" -> Uint8Array
let fds = new Map(), nextFd = 4;    // 3 = the preopened root "/"
let wasm = null;                    // instance exports
let onPrint = () => {};

const u8 = () => new Uint8Array(wasm.memory.buffer);
const dv = () => new DataView(wasm.memory.buffer);
const str = (ptr, len) => dec.decode(u8().subarray(ptr, ptr + len));

function normPath(p) {
  const out = [];
  for (const part of p.split("/")) {
    if (!part || part === ".") continue;
    if (part === "..") out.pop(); else out.push(part);
  }
  return "/" + out.join("/");
}
function isDir(path) {
  if (path === "/") return true;
  const pre = path + "/";
  for (const k of fsFiles.keys()) if (k.startsWith(pre)) return true;
  return false;
}
function dirEntries(path) {
  const pre = path === "/" ? "/" : path + "/";
  const seen = new Map();
  for (const k of fsFiles.keys()) {
    if (!k.startsWith(pre)) continue;
    const rest = k.slice(pre.length), slash = rest.indexOf("/");
    const name = slash < 0 ? rest : rest.slice(0, slash);
    if (!seen.has(name)) seen.set(name, slash < 0 ? 4 : 3);   // 4 = file, 3 = directory
  }
  return [...seen].sort((a, b) => (a[0] < b[0] ? -1 : 1));
}
function fdPath(fd) { return fd === 3 ? "/" : fds.get(fd)?.path; }

const E = { OK: 0, BADF: 8, INVAL: 28, ISDIR: 31, NOENT: 44, NOSYS: 52, NOTDIR: 54 };

const wasi = {
  proc_exit(code) { throw new Error("exit " + code); },
  clock_time_get(id, _prec, out) {
    const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
    dv().setBigUint64(out, ns, true);
    return E.OK;
  },
  fd_prestat_get(fd, buf) {
    if (fd !== 3) return E.BADF;
    dv().setUint8(buf, 0); dv().setUint32(buf + 4, 1, true);
    return E.OK;
  },
  fd_prestat_dir_name(fd, ptr, len) {
    if (fd !== 3) return E.BADF;
    u8()[ptr] = 47; // "/"
    return E.OK;
  },
  fd_fdstat_get(fd, buf) {
    const d = dv();
    let type = 2;                                       // character device (stdin/out/err)
    if (fd === 3) type = 3; else if (fd > 3) { const f = fds.get(fd); if (!f) return E.BADF; type = f.dir ? 3 : 4; }
    d.setUint8(buf, type); d.setUint16(buf + 2, 0, true);
    d.setBigUint64(buf + 8, 0xffffffffffffffffn, true); d.setBigUint64(buf + 16, 0xffffffffffffffffn, true);
    return E.OK;
  },
  fd_fdstat_set_flags() { return E.OK; },
  path_open(dirfd, _dirflags, pathPtr, pathLen, oflags, _rb, _ri, _fdflags, fdOut) {
    const base = fdPath(dirfd);
    if (base === undefined) return E.BADF;
    const path = normPath(base + "/" + str(pathPtr, pathLen));
    const wantDir = oflags & 2;
    let entry;
    if (fsFiles.has(path)) {
      if (wantDir) return E.NOTDIR;
      if (oflags & 8) fsFiles.set(path, new Uint8Array(0));
      entry = { path, dir: false, pos: 0 };
    } else if (isDir(path)) {
      entry = { path, dir: true, pos: 0 };
    } else if (oflags & 1) {
      fsFiles.set(path, new Uint8Array(0));
      entry = { path, dir: false, pos: 0 };
    } else return E.NOENT;
    const fd = nextFd++;
    fds.set(fd, entry);
    dv().setUint32(fdOut, fd, true);
    return E.OK;
  },
  fd_close(fd) { fds.delete(fd); return E.OK; },
  fd_read(fd, iovs, iovsLen, nread) {
    const f = fds.get(fd);
    if (!f) { if (fd === 0) { dv().setUint32(nread, 0, true); return E.OK; } return E.BADF; }
    if (f.dir) return E.ISDIR;
    const data = fsFiles.get(f.path) || new Uint8Array(0);
    const d = dv(), m = u8();
    let total = 0;
    for (let i = 0; i < iovsLen; i++) {
      const ptr = d.getUint32(iovs + i * 8, true), len = d.getUint32(iovs + i * 8 + 4, true);
      const chunk = data.subarray(f.pos, f.pos + len);
      m.set(chunk, ptr);
      f.pos += chunk.length; total += chunk.length;
      if (chunk.length < len) break;
    }
    d.setUint32(nread, total, true);
    return E.OK;
  },
  fd_write(fd, iovs, iovsLen, nwritten) {
    const d = dv();
    let total = 0;
    const parts = [];
    for (let i = 0; i < iovsLen; i++) {
      const ptr = d.getUint32(iovs + i * 8, true), len = d.getUint32(iovs + i * 8 + 4, true);
      parts.push(u8().slice(ptr, ptr + len)); total += len;
    }
    if (fd === 1 || fd === 2) parts.forEach((p) => onPrint(dec.decode(p)));
    else {
      const f = fds.get(fd);
      if (!f || f.dir) return E.BADF;
      let data = fsFiles.get(f.path);
      for (const p of parts) {
        const n = new Uint8Array(Math.max(data.length, f.pos + p.length));
        n.set(data); n.set(p, f.pos); data = n; f.pos += p.length;
      }
      fsFiles.set(f.path, data);
    }
    d.setUint32(nwritten, total, true);
    return E.OK;
  },
  fd_seek(fd, offset, whence, newOff) {
    const f = fds.get(fd);
    if (!f || f.dir) return E.BADF;
    const size = (fsFiles.get(f.path) || []).length;
    const base = whence === 0 ? 0 : whence === 1 ? f.pos : size;
    const pos = base + Number(offset);
    if (pos < 0) return E.INVAL;
    f.pos = pos;
    dv().setBigUint64(newOff, BigInt(pos), true);
    return E.OK;
  },
  fd_readdir(fd, buf, bufLen, cookie, bufUsed) {
    const f = fds.get(fd);
    if (!f || !f.dir) return E.NOTDIR;
    const entries = dirEntries(f.path);
    const bytes = [];
    for (let i = Number(cookie); i < entries.length; i++) {
      const name = enc.encode(entries[i][0]);
      const rec = new Uint8Array(24 + name.length), rd = new DataView(rec.buffer);
      rd.setBigUint64(0, BigInt(i + 1), true);
      rd.setBigUint64(8, BigInt(i + 1), true);
      rd.setUint32(16, name.length, true);
      rd.setUint8(20, entries[i][1]);
      rec.set(name, 24);
      bytes.push(...rec);
      if (bytes.length >= bufLen) break;
    }
    const n = Math.min(bytes.length, bufLen);
    u8().set(bytes.slice(0, n), buf);
    dv().setUint32(bufUsed, n, true);
    return E.OK;
  },
  path_filestat_get(dirfd, _flags, pathPtr, pathLen, buf) {
    const base = fdPath(dirfd);
    if (base === undefined) return E.BADF;
    const path = normPath(base + "/" + str(pathPtr, pathLen));
    const d = dv();
    for (let i = 0; i < 64; i += 8) d.setBigUint64(buf + i, 0n, true);
    if (fsFiles.has(path)) { d.setUint8(buf + 16, 4); d.setBigUint64(buf + 32, BigInt(fsFiles.get(path).length), true); }
    else if (isDir(path)) d.setUint8(buf + 16, 3);
    else return E.NOENT;
    d.setBigUint64(buf + 24, 1n, true);
    return E.OK;
  },
};
const wasiProxy = new Proxy(wasi, { get: (t, n) => t[n] || (() => E.NOSYS) });

// ---------------------------------------------------------------- drawing
const SCALE = 3;                                   // canvas pixels per 3DS pixel (sharp text on any screen)
const canvases = [$("#top"), $("#bot")];
const ctxs = canvases.map((c, i) => {
  c.width = (i ? 320 : 400) * SCALE; c.height = 240 * SCALE;
  const x = c.getContext("2d", { alpha: false });
  x.setTransform(SCALE, 0, 0, SCALE, 0, 0);
  x.imageSmoothingEnabled = false;
  return x;
});
let ctx = ctxs[0], curScreen = 0;
const colorCache = new Map();
const css = (rgb) => {
  let s = colorCache.get(rgb);
  if (!s) { s = "#" + (rgb & 0xffffff).toString(16).padStart(6, "0"); colorCache.set(rgb, s); }
  return s;
};
function clearScreens() { for (const c of ctxs) { c.fillStyle = "#000"; c.fillRect(0, 0, 400, 240); } }

const images = [];
const sounds = [];
let audio = null;
const playing = new Map();   // sound id -> Set of sources
let startTime = performance.now(), fps = 60;

const env = {
  js_print(ptr, len) { onPrint(str(ptr, len)); },
  js_screen(s) { curScreen = s; ctx = ctxs[s]; },
  js_clear(rgb) { ctx.fillStyle = css(rgb); ctx.fillRect(0, 0, curScreen ? 320 : 400, 240); },
  js_rect(x, y, w, h, rgb) { ctx.fillStyle = css(rgb); ctx.fillRect(x, y, w, h); },
  js_circle(x, y, r, rgb) {
    if (r <= 0) return;
    ctx.fillStyle = css(rgb); ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2); ctx.fill();
  },
  js_line(x1, y1, x2, y2, w, rgb) {
    ctx.strokeStyle = css(rgb); ctx.lineWidth = w || 1;
    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
  },
  js_text(x, y, ptr, len, scale, rgb) {
    // the 3DS system font is about 30 px high at scale 1
    ctx.fillStyle = css(rgb);
    ctx.font = `${(26 * scale).toFixed(2)}px "Segoe UI", Roboto, Helvetica, Arial, sans-serif`;
    ctx.textBaseline = "top";
    const lines = str(ptr, len).split("\n");
    lines.forEach((l, i) => ctx.fillText(l, x, y + 2 * scale + i * 30 * scale));
  },
  js_millis() { return Math.floor(performance.now() - startTime); },
  js_fps() { return fps; },
  js_image_create(id, w, h, ptr) {
    const c = document.createElement("canvas");
    c.width = w; c.height = h;
    const data = new Uint8ClampedArray(wasm.memory.buffer, ptr, w * h * 4).slice();
    c.getContext("2d").putImageData(new ImageData(data, w, h), 0, 0);
    images[id] = c;
  },
  js_image_draw(id, x, y, sx, sy, sw, sh, scx, scy, angle) {
    const img = images[id];
    if (!img) return;
    const w = sw * Math.abs(scx), h = sh * Math.abs(scy);
    if (w <= 0 || h <= 0) return;
    ctx.save();
    ctx.translate(x + w / 2, y + h / 2);
    if (angle) ctx.rotate((angle * Math.PI) / 180);
    ctx.scale(Math.sign(scx) || 1, Math.sign(scy) || 1);
    ctx.drawImage(img, sx, sy, sw, sh, -w / 2, -h / 2, w, h);
    ctx.restore();
  },
  js_sound_create(id, ptr, frames, channels, rate) {
    const pcm = new Int16Array(wasm.memory.buffer, ptr, frames * channels).slice();
    sounds[id] = { pcm, frames, channels, rate, buffer: null };
  },
  js_sound_play(id, volume, loop) {
    const s = sounds[id];
    if (!s || !audio) return;
    if (!s.buffer) {
      s.buffer = audio.createBuffer(s.channels, Math.max(1, s.frames), s.rate);
      for (let c = 0; c < s.channels; c++) {
        const out = s.buffer.getChannelData(c);
        for (let i = 0; i < s.frames; i++) out[i] = s.pcm[i * s.channels + c] / 32768;
      }
    }
    const src = audio.createBufferSource(), gain = audio.createGain();
    src.buffer = s.buffer; src.loop = !!loop;
    gain.gain.value = Math.max(0, Math.min(1, volume));
    src.connect(gain).connect(audio.destination);
    if (!playing.has(id)) playing.set(id, new Set());
    playing.get(id).add(src);
    src.onended = () => playing.get(id)?.delete(src);
    src.start();
  },
  js_sound_stop(id) {
    for (const [k, set] of playing) {
      if (id >= 0 && k !== id) continue;
      for (const src of set) { try { src.stop(); } catch (e) {} }
      set.clear();
    }
  },
  js_media_reset() { env.js_sound_stop(-1); images.length = 0; sounds.length = 0; },
};

// ---------------------------------------------------------------- input
const BTN = { A: 1, B: 2, X: 4, Y: 8, L: 16, R: 32, ZL: 64, ZR: 128, UP: 256, DOWN: 512, LEFT: 1024, RIGHT: 2048, START: 4096, SELECT: 8192 };
const KEYS = {
  ArrowUp: BTN.UP, ArrowDown: BTN.DOWN, ArrowLeft: BTN.LEFT, ArrowRight: BTN.RIGHT,
  KeyX: BTN.A, KeyZ: BTN.B, KeyS: BTN.X, KeyA: BTN.Y, KeyQ: BTN.L, KeyW: BTN.R,
  KeyE: BTN.ZL, KeyR: BTN.ZR, ShiftLeft: BTN.SELECT, ShiftRight: BTN.SELECT,
};
const STICK = { KeyJ: [-1, 0], KeyL: [1, 0], KeyI: [0, -1], KeyK: [0, 1] };
const keysDown = new Set();
const padPointers = new Map();     // pointerId -> button bit
const touch = { on: false, x: 0, y: 0 };
let prevHeld = 0;

const typing = () => {
  const a = document.activeElement;
  return a && (a.tagName === "TEXTAREA" || a.tagName === "INPUT" || a.tagName === "SELECT");
};
addEventListener("keydown", (e) => {
  if (typing()) return;
  if (e.code === "Escape" && running) { stopGame(); return; }
  if (KEYS[e.code] || STICK[e.code]) {
    keysDown.add(e.code);
    if (running) e.preventDefault();
  }
});
addEventListener("keyup", (e) => keysDown.delete(e.code));
addEventListener("blur", () => { keysDown.clear(); padPointers.clear(); touch.on = false; });

function readInput() {
  let held = 0, sx = 0, sy = 0;
  for (const k of keysDown) {
    if (KEYS[k]) held |= KEYS[k];
    if (STICK[k]) { sx += STICK[k][0]; sy += STICK[k][1]; }
  }
  for (const b of padPointers.values()) held |= b;
  const pressed = held & ~prevHeld;
  prevHeld = held;
  return { held, pressed, sx, sy };
}

// touch screen = the bottom canvas, in 3DS pixels whatever its size on the page
function touchPos(e) {
  const r = canvases[1].getBoundingClientRect();
  touch.x = Math.max(0, Math.min(319, Math.floor(((e.clientX - r.left) / r.width) * 320)));
  touch.y = Math.max(0, Math.min(239, Math.floor(((e.clientY - r.top) / r.height) * 240)));
}
canvases[1].addEventListener("pointerdown", (e) => {
  canvases[1].setPointerCapture(e.pointerId); touch.on = true; touchPos(e); e.preventDefault();
});
canvases[1].addEventListener("pointermove", (e) => { if (touch.on) touchPos(e); });
for (const ev of ["pointerup", "pointercancel"]) canvases[1].addEventListener(ev, () => { touch.on = false; });

// on-screen buttons (phones): a finger may slide from one button to another, e.g. around the D-pad
const pad = $("#pad");
function padUpdate(e) {
  const el = document.elementFromPoint(e.clientX, e.clientY)?.closest("[data-btn]");
  const bit = el && pad.contains(el) ? BTN[el.dataset.btn] : 0;
  const before = padPointers.get(e.pointerId) || 0;
  if (bit) padPointers.set(e.pointerId, bit); else padPointers.delete(e.pointerId);
  if (bit && bit !== before && navigator.vibrate) navigator.vibrate(8);
  pad.querySelectorAll("[data-btn]").forEach((b) => {
    b.classList.toggle("held", [...padPointers.values()].includes(BTN[b.dataset.btn]));
  });
}
pad.addEventListener("pointerdown", (e) => {
  if (e.target.closest("[data-action]")) return;
  pad.setPointerCapture(e.pointerId); padUpdate(e); e.preventDefault();
});
pad.addEventListener("pointermove", (e) => { if (padPointers.has(e.pointerId) || e.buttons) padUpdate(e); });
for (const ev of ["pointerup", "pointercancel", "lostpointercapture"]) {
  pad.addEventListener(ev, (e) => {
    padPointers.delete(e.pointerId);
    pad.querySelectorAll("[data-btn]").forEach((b) => b.classList.toggle("held", [...padPointers.values()].includes(BTN[b.dataset.btn])));
  });
}
pad.addEventListener("contextmenu", (e) => e.preventDefault());

// ---------------------------------------------------------------- running
let running = false, raf = 0, lastTick = 0, fpsCount = 0, fpsTime = 0;
const out = $("#out");

function print(s) {
  out.textContent += s;
  if (out.textContent.length > 20000) out.textContent = out.textContent.slice(-15000);
  out.scrollTop = out.scrollHeight;
}
onPrint = print;

function cstr(s) {
  const b = enc.encode(s + "\0"), p = wasm.web_alloc(b.length);
  u8().set(b, p);
  return p;
}

function setStatus(text, kind) {
  const el = $("#status");
  el.textContent = text;
  el.className = kind || "";
}

function runProject() {
  if (!wasm) return;
  stopGame(true);
  saveProject();
  try {
    if (!audio) { const AC = window.AudioContext || window.webkitAudioContext; if (AC) audio = new AC(); }
    if (audio && audio.state === "suspended") audio.resume();
  } catch (e) {}
  // the project's files go to the in-memory file system under /p
  for (const k of [...fsFiles.keys()]) if (k.startsWith(PROJECT_DIR + "/")) fsFiles.delete(k);
  for (const [name, text] of Object.entries(project.files)) fsFiles.set(`${PROJECT_DIR}/${name}`, enc.encode(text));
  for (const [name, data] of Object.entries(project.media)) fsFiles.set(`${PROJECT_DIR}/${name}`, data);
  out.textContent = "";
  clearError();
  clearScreens();
  ctx = ctxs[0];
  startTime = performance.now();
  prevHeld = 0;
  if (isMobile()) showView("game");
  const p = cstr(PROJECT_DIR);
  const t0 = performance.now();
  let r;
  try { r = wasm.web_run(p); } catch (e) { r = -3; console.error(e); }
  wasm.web_free(p);
  const ms = Math.round(performance.now() - t0);
  if (r === 1) {
    running = true;
    document.body.classList.add("running");
    setStatus(`Gra działa (kompilacja + start: ${ms} ms)`, "ok");
    lastTick = fpsTime = performance.now(); fpsCount = 0;
    raf = requestAnimationFrame(tick);
  } else handleResult(r, ms);
}

function handleResult(r, ms) {
  if (r === 0) setStatus(ms !== undefined ? `Program zakończony (${ms} ms)` : "Program zakończony", "ok");
  else if (r === -1) showError("Błąd kompilacji", readError());
  else if (r === -2) showError("Błąd w trakcie działania", readError());
  else showError("Błąd", "Wewnętrzny błąd WebAssembly (szczegóły w konsoli przeglądarki). Odśwież stronę.");
}

function readError() {
  const p = wasm.web_error();
  const m = u8();
  let e = p; while (m[e]) e++;
  return dec.decode(m.subarray(p, e));
}

const FRAME = 1000 / 60;
function tick(now) {
  if (!running) return;
  raf = requestAnimationFrame(tick);
  // 60 updates a second like the console, also on 90/120 Hz screens
  if (now - lastTick < FRAME - 2) return;
  lastTick = now - Math.min(FRAME, (now - lastTick) % FRAME);
  fpsCount++;
  if (now - fpsTime >= 1000) { fps = Math.round((fpsCount * 1000) / (now - fpsTime)); fpsCount = 0; fpsTime = now; }
  const inp = readInput();
  wasm.web_input(inp.held, inp.pressed, touch.on ? 1 : 0, touch.x, touch.y, Math.max(-1, Math.min(1, inp.sx)), Math.max(-1, Math.min(1, inp.sy)));
  for (const c of ctxs) { c.fillStyle = "#000"; c.fillRect(0, 0, 400, 240); }
  let r;
  try { r = wasm.web_frame(); } catch (e) { r = -3; console.error(e); }
  if (r !== 1) {
    running = false;
    document.body.classList.remove("running");
    cancelAnimationFrame(raf);
    handleResult(r);
  }
}

function stopGame(quiet) {
  if (running) {
    running = false;
    cancelAnimationFrame(raf);
    wasm.web_stop();
    document.body.classList.remove("running");
    if (!quiet) setStatus("Zatrzymano", "");
  }
}

// ---------------------------------------------------------------- project and editor
const editor = $("#code"), gutter = $("#gutter");
let project = { id: "", name: "", files: {}, media: {}, current: "" };

const START_CODE = `// Twój pierwszy program. Naciśnij "Uruchom".
// Strzałki (albo przyciski na ekranie telefonu) przesuwają kulkę.
class Game {
    static float x = 200, y = 120;

    static void Update() {
        if (Input.Held(Btn.Left)) x -= 3;
        if (Input.Held(Btn.Right)) x += 3;
        if (Input.Held(Btn.Up)) y -= 3;
        if (Input.Held(Btn.Down)) y += 3;
        if (Input.Touching()) { x = Input.TouchX() * 400 / 320; y = Input.TouchY(); }
    }

    static void Draw() {
        Gfx.Clear(Gfx.Rgb(20, 24, 40));
        Gfx.Circle(x, y, 14, Color.Yellow);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.DarkGray);
        Gfx.Text(10, 10, "x = " + (int)x + ", y = " + (int)y, Color.White);
        Gfx.Text(10, 40, "Dotknij tego ekranu!", Color.Cyan);
    }
}
`;

const b64 = {
  from(bytes) { let s = ""; for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000)); return btoa(s); },
  to(s) { const b = atob(s), a = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) a[i] = b.charCodeAt(i); return a; },
};

// Projects: every project is its own localStorage entry, so one big project (images, sounds) cannot push the others out.
const INDEX_KEY = "minisharp.index.v2", PROJ_KEY = "minisharp.p.", OLD_KEY = "minisharp.project.v1";
let index = { active: "", list: [] };           // list: [{ id, name, updated }]
const mk = (tag, cls, text) => { const x = document.createElement(tag); if (cls) x.className = cls; if (text) x.textContent = text; return x; };
const newId = () => "p" + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
const SKELETON = `class Game {\n    static void Update() {\n        \n    }\n\n    static void Draw() {\n        Gfx.Clear(Color.Black);\n    }\n}\n`;

function readIndex() {
  try { const i = JSON.parse(localStorage.getItem(INDEX_KEY) || "null"); if (i && Array.isArray(i.list)) return i; } catch (e) {}
  return null;
}
function writeIndex() { try { localStorage.setItem(INDEX_KEY, JSON.stringify(index)); } catch (e) {} }
function uniqueName(base, exceptId) {
  const names = new Set(index.list.filter((p) => p.id !== exceptId).map((p) => p.name));
  let n = base, i = 2;
  while (names.has(n)) n = `${base} (${i++})`;
  return n;
}
function writeProjectData(p) {                  // false when the images and sounds did not fit
  const key = PROJ_KEY + p.id;
  try {
    const media = {};
    for (const [k, v] of Object.entries(p.media)) media[k] = b64.from(v);
    localStorage.setItem(key, JSON.stringify({ files: p.files, media, current: p.current }));
    return true;
  } catch (e) {
    try { localStorage.setItem(key, JSON.stringify({ files: p.files, media: {}, current: p.current })); } catch (e2) {}
    return false;
  }
}
function syncEditor() { if (project.current) project.files[project.current] = editor.value; }
function saveProject() {
  if (!project.id) return;
  syncEditor();
  const ok = writeProjectData(project);
  const meta = index.list.find((x) => x.id === project.id);
  if (meta) { meta.updated = Date.now(); meta.name = project.name; }
  writeIndex();
  if (!ok && Object.keys(project.media).length) setStatus("Brak miejsca w przeglądarce: obrazki i dźwięki nie zostały zapisane.", "bad");
}
function readProjectData(id) {
  try {
    const s = JSON.parse(localStorage.getItem(PROJ_KEY + id) || "null");
    if (s && s.files && Object.keys(s.files).length) {
      const media = {};
      for (const [k, v] of Object.entries(s.media || {})) media[k] = b64.to(v);
      return { files: s.files, media, current: s.files[s.current] !== undefined ? s.current : Object.keys(s.files).sort()[0] };
    }
  } catch (e) {}
  return null;
}
function readOldProject() {                     // the single-project version of this page
  try {
    const s = JSON.parse(localStorage.getItem(OLD_KEY) || "null");
    if (s && s.files && Object.keys(s.files).length) {
      const media = {};
      for (const [k, v] of Object.entries(s.media || {})) media[k] = b64.to(v);
      return { files: s.files, media, current: s.files[s.current] !== undefined ? s.current : Object.keys(s.files).sort()[0] };
    }
  } catch (e) {}
  return null;
}
const starterData = () => ({ files: { "Game.cs": SKELETON }, media: {}, current: "Game.cs" });

function activate(meta, data) {
  stopGame(true);
  setProject({ id: meta.id, name: meta.name, files: data.files, media: data.media || {}, current: data.current });
  index.active = meta.id;
  writeIndex();
}
function switchProject(id) {
  if (id === project.id) return;
  const meta = index.list.find((x) => x.id === id);
  if (!meta) return;
  saveProject();
  activate(meta, readProjectData(id) || starterData());
  setStatus(`Projekt: ${meta.name}`, "");
}
function createProject(name, data) {
  saveProject();
  const meta = { id: newId(), name: uniqueName(name), updated: Date.now() };
  index.list.push(meta);
  activate(meta, { files: data.files, media: data.media || {}, current: data.current || Object.keys(data.files).sort()[0] });
  saveProject();
  return meta;
}
function askName(msg, def) {
  let n = prompt(msg, def);
  if (n === null) return null;
  n = n.trim().slice(0, 40);
  return n || null;
}
function newProject() {
  const n = askName("Nazwa nowego projektu:", "Nowa gra");
  if (!n) return false;
  const meta = createProject(n, starterData());
  setStatus(`Utworzono projekt: ${meta.name}`, "ok");
  return true;
}
function renameProject(id) {
  const meta = index.list.find((x) => x.id === id);
  if (!meta) return;
  const n = askName("Nowa nazwa projektu:", meta.name);
  if (!n || n === meta.name) return;
  meta.name = uniqueName(n, id);
  if (id === project.id) { project.name = meta.name; saveProject(); renderFiles(); } else writeIndex();
}
function duplicateProject(id) {
  const meta = index.list.find((x) => x.id === id);
  if (!meta) return;
  if (id === project.id) saveProject();
  const data = id === project.id ? project : readProjectData(id);
  if (!data) return;
  const files = JSON.parse(JSON.stringify(data.files)), media = {};
  for (const [k, v] of Object.entries(data.media || {})) media[k] = new Uint8Array(v);
  const copy = createProject(`${meta.name} (kopia)`, { files, media, current: data.current });
  setStatus(`Utworzono kopię: ${copy.name}`, "ok");
}
function deleteProject(id) {
  const meta = index.list.find((x) => x.id === id);
  if (!meta) return;
  if (!confirm(`Usunąć projekt „${meta.name}” razem z plikami? Tego nie da się cofnąć.`)) return;
  try { localStorage.removeItem(PROJ_KEY + id); } catch (e) {}
  index.list = index.list.filter((x) => x.id !== id);
  if (id !== project.id) { writeIndex(); return; }
  project.id = "";                              // so the deleted project is not saved again
  const next = [...index.list].sort((a, b) => b.updated - a.updated)[0];
  if (next) { activate(next, readProjectData(next.id) || starterData()); setStatus(`Projekt: ${next.name}`, ""); }
  else createProject("Mój projekt", starterData());
}

async function initProjects() {
  index = readIndex() || { active: "", list: [] };
  const shared = await loadFromHash();
  if (!index.list.length) {
    const old = readOldProject();                // migrate the single project of the older version
    if (old) createProject("Mój projekt", old);
  }
  if (shared) {
    const same = index.list.find((m) => { const d = readProjectData(m.id); return d && JSON.stringify(d.files) === JSON.stringify(shared.files); });
    if (same) activate(same, readProjectData(same.id));
    else createProject("Z linku", shared);
    history.replaceState(null, "", location.pathname);
  } else if (index.list.length) {
    const meta = index.list.find((m) => m.id === index.active) || [...index.list].sort((a, b) => b.updated - a.updated)[0];
    activate(meta, readProjectData(meta.id) || starterData());
  } else {
    createProject("Mój projekt", { files: { "Game.cs": START_CODE }, media: {}, current: "Game.cs" });
  }
}

function setProject(p) {
  project = p;
  if (!project.files[project.current]) project.current = Object.keys(project.files).sort()[0] || "";
  renderFiles();
  openFile(project.current, true);
}

function openFile(name, force) {
  if (!force && project.current) project.files[project.current] = editor.value;
  project.current = name;
  editor.value = project.files[name] ?? "";
  editor.scrollTop = 0; editor.scrollLeft = 0;
  acClose();
  updateGutter();
  renderFiles();
  afterOpenFile();
}

function renderFiles() {
  const list = $("#files");
  list.innerHTML = "";
  const names = Object.keys(project.files).sort();
  const chip = mk("button", "file proj", `${project.name || "Projekt"} ▾`);
  chip.title = "Projekty i pliki";
  chip.onclick = openProjects;
  list.appendChild(chip);
  for (const n of names) {
    const b = document.createElement("button");
    const isGraph = hasGraph(n === project.current ? editor.value : project.files[n]);
    b.className = "file" + (n === project.current ? " active" : "") + (isGraph ? " graph" : "");
    b.textContent = n;
    b.title = "Kliknij dwa razy, żeby zmienić nazwę";
    b.onclick = () => { openFile(n); if (hasGraph(project.files[n])) setStatus("Plik z grafem: kod powstaje z grafu (karta Graf), tutaj można go tylko czytać.", ""); };
    b.ondblclick = () => renameFile(n);
    if (names.length > 1) {
      const x = document.createElement("span");
      x.className = "x"; x.textContent = "×"; x.title = "Usuń plik";
      x.onclick = (e) => { e.stopPropagation(); deleteFile(n); };
      b.appendChild(x);
    }
    list.appendChild(b);
  }
  const add = mk("button", "file add sticky", "+ dodaj");
  add.title = "Nowy plik z kodem albo z grafem";
  add.onclick = openAddMenu;
  list.appendChild(add);
  const media = Object.keys(project.media).sort();
  const ml = $("#media");
  ml.innerHTML = "";
  for (const n of media) {
    const s = document.createElement("span");
    s.className = "asset";
    s.textContent = n;
    const x = document.createElement("button");
    x.textContent = "×"; x.title = "Usuń";
    x.onclick = () => { delete project.media[n]; renderFiles(); saveProject(); };
    s.appendChild(x);
    ml.appendChild(s);
  }
  $("#mediaRow").classList.toggle("empty", media.length === 0);
}

function validName(n) { return /^[A-Za-z0-9_][A-Za-z0-9_.\-]*\.cs$/.test(n); }
function addFile() {
  let n = prompt("Nazwa nowego pliku (np. Player.cs):", "Player.cs");
  if (!n) return;
  n = n.trim(); if (!n.endsWith(".cs")) n += ".cs";
  if (!validName(n)) { alert("Nazwa może mieć litery, cyfry, _ - . i musi kończyć się na .cs"); return; }
  if (project.files[n] !== undefined) { openFile(n); return; }
  const cls = n.slice(0, -3).replace(/[^A-Za-z0-9_]/g, "");
  project.files[n] = `class ${cls} {\n    \n}\n`;
  openFile(n);
  saveProject();
}
function deleteFile(n) {
  if (Object.keys(project.files).length < 2) { alert("Projekt musi mieć przynajmniej jeden plik."); return; }
  if (!confirm(`Usunąć ${n}?`)) return;
  syncEditor();
  delete project.files[n];
  if (project.current === n) project.current = "";
  setProject(project); saveProject();
}
function renameFile(old) {
  let n = prompt("Nowa nazwa pliku:", old);
  if (!n || n === old) return;
  n = n.trim(); if (!n.endsWith(".cs")) n += ".cs";
  if (!validName(n) || project.files[n] !== undefined) { alert("Ta nazwa jest zajęta albo niepoprawna."); return; }
  syncEditor();
  project.files[n] = project.files[old];
  delete project.files[old];
  if (project.current === old) project.current = n;
  setProject(project); saveProject();
}

$("#addMedia").onchange = async (e) => {
  for (const f of e.target.files) project.media[f.name] = new Uint8Array(await f.arrayBuffer());
  e.target.value = "";
  renderFiles(); saveProject();
};

// the "Projekty i pliki" dialog: big touch targets, so it works on a phone as well
function openProjects() { renderProjects(); $("#projects").showModal(); }
function renderProjects() {
  const body = $("#projectsBody"), dlg = $("#projects");
  body.textContent = "";
  const btn = (label, fn, cls) => { const b = mk("button", cls || "", label); b.type = "button"; b.onclick = fn; return b; };
  const when = (t) => new Date(t).toLocaleString("pl-PL", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });

  body.append(mk("h3", "", "Projekty"));
  for (const m of [...index.list].sort((a, b) => b.updated - a.updated)) {
    const row = mk("div", "prow" + (m.id === project.id ? " active" : ""));
    const name = btn("", () => { switchProject(m.id); dlg.close(); }, "pname");
    name.append(mk("span", "", m.name), mk("small", "", m.id === project.id ? "otwarty teraz" : "zmieniony " + when(m.updated)));
    row.append(name,
      btn("Nazwa", () => { renameProject(m.id); renderProjects(); }, "act"),
      btn("Kopia", () => { duplicateProject(m.id); renderProjects(); }, "act"),
      btn("Usuń", () => { deleteProject(m.id); renderProjects(); }, "act danger"));
    body.append(row);
  }
  body.append(btn("+ Nowy projekt", () => { if (newProject()) dlg.close(); }, "primary pbig"));

  body.append(mk("h3", "", `Pliki w projekcie „${project.name}”`));
  for (const n of Object.keys(project.files).sort()) {
    const row = mk("div", "prow" + (n === project.current ? " active" : ""));
    const name = btn("", () => { openFile(n); dlg.close(); }, "pname");
    name.append(mk("span", "", n));
    row.append(name,
      btn("Nazwa", () => { renameFile(n); renderProjects(); }, "act"),
      btn("Usuń", () => { deleteFile(n); renderProjects(); }, "act danger"));
    body.append(row);
  }
  body.append(btn("+ Nowy plik", () => { const before = project.current; addFile(); if (project.current !== before) dlg.close(); }, "pbig"));
}
$("#projectsClose").onclick = () => $("#projects").close();

// graphs (graph.js = model and generator, grapheditor.js = the editor): a .cs file with a "// @graph" line is a graph file,
// its code is generated from the graph every time the graph changes
const gpane = $("#graphhost"), gempty = $("#graphempty");
let graphEditor = null, graphCatalog = null, graphToken = 0, graphSaveTimer = 0, inGraphSwitch = false;
const lastGraph = new Map();                   // project id -> the graph file last shown on the Graf tab
const hasGraph = (t) => /^\/\/ @graph /m.test(t || "");
const currentView = () => document.body.dataset.view;
const graphFileChip = mk("button", "gfile", "Graf ▾");
graphFileChip.type = "button";
graphFileChip.title = "Pliki z grafem w projekcie";
graphFileChip.onclick = () => openGraphFiles();

// a small menu dialog: items = [{ title, note, fn }]
function menuDialog(title, items) {
  const dlg = document.createElement("dialog");
  dlg.className = "gdialog small";
  const head = mk("div", "ghead");
  const x = mk("button", "", "×"); x.type = "button"; x.setAttribute("aria-label", "Zamknij"); x.onclick = () => dlg.close();
  head.append(mk("strong", "", title), x);
  const list = mk("div", "glist");
  for (const it of items) {
    const b = mk("button", "gitem"); b.type = "button";
    b.append(mk("span", "gtitle", it.title));
    if (it.note) b.append(mk("small", "gsig", it.note));
    b.onclick = () => { dlg.close(); it.fn(); };
    list.append(b);
  }
  dlg.append(head, list);
  dlg.addEventListener("close", () => dlg.remove());
  document.body.append(dlg);
  dlg.showModal();
  return dlg;
}
// the nodes of the project's own code: every static method of the files that are not graphs
const projectNodes = () => MSGraph.userNodes(Object.keys(project.files).map((name) => ({ name, text: name === project.current ? editor.value : project.files[name] })));
const freshCatalog = () => (graphCatalog = MSGraph.buildCatalog(helpData, projectNodes()));
function graphEditorInit() {
  if (!graphEditor) graphEditor = MSGraphEditor.create(gpane, {
    leading: [graphFileChip], extraMenu: graphMenu, refreshCatalog: freshCatalog, onChange: onGraphChange,
    notify: (t) => setStatus(t, "bad"), prompt: (m, d) => prompt(m, d),
  });
  return graphEditor;
}
const graphFiles = () => Object.keys(project.files).sort().filter((n) => hasGraph(n === project.current ? editor.value : project.files[n]));
function pickGraphFile() {
  const last = lastGraph.get(project.id);
  if (last && hasGraph(project.files[last])) return last;
  return graphFiles()[0] || null;
}
// the Graf tab shows the graph of the open file, or of the last/first graph file of the project, or an invitation to make one
async function refreshGraphTab() {
  const token = ++graphToken;
  syncEditor();
  const name = hasGraph(project.files[project.current]) ? project.current : pickGraphFile();
  gempty.hidden = !!name;
  gpane.hidden = !name;
  if (!name) return;
  if (name !== project.current) {
    inGraphSwitch = true;
    try { openFile(name); } finally { inGraphSwitch = false; }
  }
  const help = await helpReady;
  if (token !== graphToken) return;
  const text = editor.value, g = MSGraph.extractGraph(text);
  if (!g) { showView("code"); return; }
  graphCatalog = MSGraph.buildCatalog(help, projectNodes());
  const gen = MSGraph.generate(g, graphCatalog);
  if (gen.code.trim() !== text.trim() && !confirm("Kod w tym pliku różni się od grafu (był edytowany ręcznie albo powstał w starszej wersji). Zmiana grafu zastąpi go kodem z grafu. Kontynuować?")) { showView("code"); return; }
  lastGraph.set(project.id, name);
  graphFileChip.textContent = name + " ▾";
  graphEditorInit().load(g, graphCatalog);
  graphEditor.setIssues(gen.warnings.concat(gen.errors, projectIssues(g)));
}
// remarks that need the other files of the project (the generator sees only the graph)
function projectIssues(g) {
  const out = [];
  const has = (t, m) => new RegExp("\\bstatic\\s+void\\s+" + m + "\\s*\\(").test((t || "").replace(/\/\/.*$/gm, ""));
  const texts = Object.keys(project.files).map((n) => (n === project.current ? editor.value : project.files[n]));
  if (!texts.some((t) => has(t, "Main") || has(t, "Update"))) out.push({ node: null, message: "Projekt nie ma ani Main, ani Update, więc nie ma od czego zacząć: dodaj węzeł Start albo Update (albo napisz je w kodzie)." });
  return out;
}
function onGraphChange(g) {
  const out = MSGraph.generate(g, graphCatalog);
  editor.value = out.code;
  project.files[project.current] = out.code;
  updateGutter();
  graphEditor.setIssues(out.warnings.concat(out.errors, projectIssues(g)));
  clearTimeout(graphSaveTimer);
  graphSaveTimer = setTimeout(saveProject, 400);
}
function afterOpenFile() {
  editor.readOnly = hasGraph(editor.value);      // the code of a graph file is generated: read only
  if (!inGraphSwitch && currentView() === "graph") refreshGraphTab();
}
function openGraphFiles() {
  const items = graphFiles().map((n) => ({ title: n, note: n === project.current ? "otwarty teraz" : "", fn: () => openFile(n) }));
  items.push({ title: "+ Nowy graf", note: "plik z pustym grafem", fn: addGraphFile });
  menuDialog("Pliki z grafem", items);
}
function toggleInstance() {
  const g = graphEditor.getGraph();
  g.instance = !g.instance;
  onGraphChange(g);
  setStatus(g.instance ? "Klasa z obiektami: robisz je w kodzie (var o = new Klasa(); o.Metoda();). Start, Update i Draw tu nie działają, użyj węzłów „Metoda”." : "Klasa statyczna: metody wołasz jako Klasa.Metoda().", "");
}
function setOrder() {
  const g = graphEditor.getGraph();
  const v = prompt("Kolejność (jak DefaultExecutionOrder w Unity): mniejsza liczba = Start, Update i Draw tego grafu idą przed innymi plikami. Domyślnie 0, np. -10 albo 5:", String(g.order || 0));
  if (v === null) return;
  const n = parseInt(v.trim() || "0", 10);
  if (!Number.isInteger(n)) { setStatus("Kolejność to liczba całkowita, np. -10, 0, 5.", "err"); return; }
  if (n) g.order = n; else delete g.order;
  onGraphChange(g);
}
function graphMenu() {
  const inst = graphEditor && graphEditor.getGraph().instance;
  const ord = (graphEditor && graphEditor.getGraph().order) || 0;
  return [
    { label: `Kolejność: ${ord}`, note: "mniejsza = Update i Draw tego grafu wcześniej (jak w Unity)", fn: setOrder },
    { label: inst ? "Zmień na klasę statyczną" : "Zmień na klasę z obiektami", note: inst ? "zmienne i metody znów static" : "szablon dla new Klasa(), własny stan obiektu", fn: toggleInstance },
    { label: "Pokaż wygenerowany kod", note: "karta Kod, tylko do czytania", fn: () => showView("code") },
    { label: "Zamień na zwykły kod", note: "odłącz graf, dalej edytujesz kod ręcznie", fn: detachGraph },
  ];
}
function detachGraph() {
  if (!confirm("Zamienić ten plik na zwykły kod? Graf zniknie z pliku (kod zostaje), a edytor grafu przestanie go otwierać.")) return;
  syncEditor();
  const t = editor.value.replace(/\n*^\/\/ @graph .*$\n?/m, "\n").replace(/^\/\/ Wygenerowano z grafu\..*\n/, "// Kod powstał z grafu, teraz to zwykły plik.\n");
  editor.value = t;
  project.files[project.current] = t;
  editor.readOnly = false;
  updateGutter();
  saveProject();
  showView("code");
  renderFiles();
  setStatus("Graf odłączony: to zwykły kod, możesz go edytować.", "ok");
}
$("#graphNew").onclick = () => addGraphFile();
// "+ dodaj": a file with code or a file with a graph (one button that always stays on screen, the tab row scrolls)
function openAddMenu() {
  menuDialog("Dodaj plik do projektu", [
    { title: "Plik z kodem (.cs)", note: "pusta klasa, piszesz kod ręcznie", fn: addFile },
    { title: "Plik z grafem (.cs)", note: "programowanie węzłami, kod powstaje sam", fn: addGraphFile },
  ]);
}
async function addGraphFile() {
  let n = prompt("Nazwa pliku z grafem (np. Graph.cs):", "Graph.cs");
  if (!n) return;
  n = n.trim(); if (!n.endsWith(".cs")) n += ".cs";
  if (!validName(n)) { alert("Nazwa może mieć litery, cyfry, _ - . i musi kończyć się na .cs"); return; }
  if (project.files[n] !== undefined) { openFile(n); return; }
  syncEditor();
  const help = await helpReady;
  const cat = MSGraph.buildCatalog(help, projectNodes());
  const g = MSGraph.emptyGraph();
  const cls = n.slice(0, -3).replace(/[^A-Za-z0-9_]/g, "");
  if (/^[A-Za-z_]/.test(cls)) g.className = cls;
  // every graph may have its own Update and Draw (the program calls all of them), so a new graph starts with both
  g.nodes.push({ id: MSGraph.nextId(g), type: "event.Update", x: 40, y: 40, data: {} });
  g.nodes.push({ id: MSGraph.nextId(g), type: "event.Draw", x: 40, y: 220, data: {} });
  project.files[n] = MSGraph.generate(g, cat).code;
  openFile(n);
  saveProject();
  showView("graph");
}

// code completion (IntelliSense): a list near the cursor; tap, Tab or Enter takes the highlighted suggestion.
// The analysis itself is in complete.js (MSComplete).
let helpData = null;                            // help.json, set by loadHelp()
let helpResolve;
const helpReady = new Promise((r) => { helpResolve = r; });   // resolves with help.json (or null when it did not load)
const acBox = $("#ac"), acMeasure = $("#acm");
const ac = { open: false, items: [], sel: 0, from: 0, to: 0, skip: false, frame: 0, force: false };
const AC_BADGE = { field: "f", method: "m", var: "v", class: "C", api: "A", const: "c", type: "T", kw: "k" };

function acClose() { ac.open = false; acBox.hidden = true; }
function acUpdate(force) {
  ac.frame = 0; ac.force = false;
  if (document.activeElement !== editor || editor.selectionStart !== editor.selectionEnd || editor.value.length > 200000) return acClose();
  const others = Object.keys(project.files).filter((n) => n !== project.current).map((n) => project.files[n]);
  let r = null;
  try { r = MSComplete.analyze({ text: editor.value, caret: editor.selectionStart, others, help: helpData, force: !!force }); }
  catch (e) { console.error(e); }
  if (!r) return acClose();
  Object.assign(ac, { open: true, items: r.items, sel: 0, from: r.from, to: r.to });
  acRender();
}
function acSchedule(force) {
  if (force) ac.force = true;
  if (!ac.frame) ac.frame = requestAnimationFrame(() => acUpdate(ac.force));
}
function acRender() {
  acBox.textContent = "";
  const list = mk("div", "aclist");
  const typed = editor.value.slice(ac.from, ac.to), n = typed.length;
  ac.items.forEach((it, i) => {
    const row = mk("div", "acitem" + (i === ac.sel ? " sel" : ""));
    row.dataset.i = i;
    row.append(mk("span", "k k-" + it.kind, AC_BADGE[it.kind] || "·"));
    const lbl = mk("span", "lbl");
    if (n && it.label.toLowerCase().startsWith(typed.toLowerCase())) lbl.append(mk("b", "", it.label.slice(0, n)), it.label.slice(n));
    else lbl.textContent = it.label;
    row.append(lbl);
    if (it.detail) row.append(mk("span", "det", it.detail));
    list.append(row);
  });
  acBox.append(list, mk("div", "acdoc"));
  acDoc();
  acBox.hidden = false;
  acPosition();
}
function acDoc() { const d = acBox.querySelector(".acdoc"); if (d) d.textContent = (ac.items[ac.sel] && ac.items[ac.sel].doc) || ""; }
function acMove(d) {
  const rows = acBox.querySelectorAll(".acitem");
  if (!rows.length) return;
  rows[ac.sel].classList.remove("sel");
  ac.sel = (ac.sel + d + rows.length) % rows.length;
  const r = rows[ac.sel], list = acBox.querySelector(".aclist");
  r.classList.add("sel");
  // keep the row in view inside the list (scrollIntoView could also scroll the whole page)
  if (r.offsetTop < list.scrollTop) list.scrollTop = r.offsetTop;
  else if (r.offsetTop + r.offsetHeight > list.scrollTop + list.clientHeight) list.scrollTop = r.offsetTop + r.offsetHeight - list.clientHeight;
  acDoc();
}
function acAccept(i) {
  const it = ac.items[i];
  if (!it) return;
  const { from, to } = ac;
  acClose();
  ac.skip = true;                               // the insertion below must not open the list again
  editor.focus();
  editor.setSelectionRange(from, to);
  document.execCommand("insertText", false, it.insert + (it.dot ? "." : ""));
  ac.skip = false;
  if (it.dot) acSchedule(true);                 // "Gfx" -> "Gfx." and straight on to its members
}
// where is the start of the typed word on the screen? (the editor font is monospace)
function acPosition() {
  const v = editor.value, cs = getComputedStyle(editor);
  const lineStart = v.lastIndexOf("\n", ac.from - 1) + 1;
  let col = 0, line = 0;
  for (let i = lineStart; i < ac.from; i++) col = v[i] === "\t" ? col + 4 - (col % 4) : col + 1;
  for (let i = v.indexOf("\n"); i !== -1 && i < ac.from; i = v.indexOf("\n", i + 1)) line++;
  const lh = parseFloat(cs.lineHeight) || 20, cw = acMeasure.getBoundingClientRect().width / 40;
  const x = parseFloat(cs.paddingLeft) + col * cw - editor.scrollLeft;
  const y = parseFloat(cs.paddingTop) + line * lh - editor.scrollTop;
  const wr = editor.parentElement.getBoundingClientRect();
  const bw = acBox.offsetWidth, bh = acBox.offsetHeight;
  const vv = window.visualViewport, vBottom = Math.min(wr.bottom, vv ? vv.offsetTop + vv.height : innerHeight);
  const below = vBottom - (wr.top + y + lh), above = wr.top + y;
  let top = y + lh + 2;
  if (below < bh + 8 && above > below) top = y - bh - 2;              // no room under the line (keyboard): above it
  acBox.style.left = Math.max(4, Math.min(x, wr.width - bw - 4)) + "px";
  acBox.style.top = Math.max(0, top) + "px";
}
editor.addEventListener("input", (e) => {
  if (ac.skip) { ac.skip = false; return; }
  if (e.inputType && e.inputType.startsWith("delete")) return acClose();
  acSchedule();
});
editor.addEventListener("blur", acClose);
editor.addEventListener("mousedown", acClose);
editor.addEventListener("keyup", (e) => { if (["ArrowLeft", "ArrowRight", "Home", "End", "PageUp", "PageDown"].includes(e.key)) acClose(); });
editor.addEventListener("scroll", () => { if (ac.open) acPosition(); });
window.visualViewport?.addEventListener("resize", () => { if (ac.open) acPosition(); });
acBox.addEventListener("mousedown", (e) => e.preventDefault());       // keep the focus (and the phone keyboard) in the editor
acBox.addEventListener("click", (e) => { const r = e.target.closest(".acitem"); if (r) acAccept(+r.dataset.i); });

// syntax highlighting (a coloured copy of the text under the transparent textarea)
const HL_KEYWORDS = new Set(MSComplete.lists.keywords);
const HL_LITERALS = new Set(MSComplete.lists.literals);
const HL_TYPES = new Set(MSComplete.lists.types);
const HL_API = new Set(MSComplete.lists.api);
const HL_TOKEN = /\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$)|"(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])*'?|\b0x[0-9a-fA-F]+\b|\b\d+(?:\.\d+)?f?\b|[A-Za-z_]\w*/g;
const hlEsc = (t) => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
function highlightMS(text) {
  // class/interface names declared anywhere in the project (other files too)
  const userTypes = new Set();
  for (const src of [text, ...Object.values(project.files)])
    for (const m of src.matchAll(/\b(?:class|interface)\s+([A-Za-z_]\w*)/g)) userTypes.add(m[1]);
  let out = "", last = 0, m;
  HL_TOKEN.lastIndex = 0;
  while ((m = HL_TOKEN.exec(text))) {
    const t = m[0], c = t[0];
    let cls = "";
    if (c === "/") cls = "tk-com";
    else if (c === '"' || c === "'") cls = "tk-str";
    else if (c >= "0" && c <= "9") cls = "tk-num";
    else if (HL_LITERALS.has(t)) cls = "tk-lit";
    else if (HL_KEYWORDS.has(t)) cls = "tk-kw";
    else if (HL_TYPES.has(t) || userTypes.has(t)) cls = "tk-ty";
    else if (HL_API.has(t)) cls = "tk-api";
    else if (/^[ \t]*\(/.test(text.slice(m.index + t.length, m.index + t.length + 20))) cls = "tk-fn";
    out += hlEsc(text.slice(last, m.index));
    out += cls ? '<span class="' + cls + '">' + hlEsc(t) + "</span>" : hlEsc(t);
    last = m.index + t.length;
  }
  return out + hlEsc(text.slice(last)) + "\n";
}
const hl = $("#hl");
let hlFrame = 0;
function updateHighlight() {
  if (hlFrame) return;
  hlFrame = requestAnimationFrame(() => { hlFrame = 0; hl.innerHTML = highlightMS(editor.value); syncScroll(); });
}
function syncScroll() {
  gutter.scrollTop = editor.scrollTop;
  hl.scrollTop = editor.scrollTop; hl.scrollLeft = editor.scrollLeft;
}

// line numbers
function updateGutter() {
  updateHighlight();
  const n = editor.value.split("\n").length;
  if (gutter.dataset.n !== String(n)) {
    let s = "";
    for (let i = 1; i <= n; i++) s += i + "\n";
    gutter.textContent = s;
    gutter.dataset.n = n;
  }
  gutter.scrollTop = editor.scrollTop;
}
editor.addEventListener("input", () => { updateGutter(); clearTimeout(editor._t); editor._t = setTimeout(saveProject, 500); });
editor.addEventListener("scroll", syncScroll);
editor.addEventListener("keydown", (e) => {
  if (ac.open) {
    if (e.key === "ArrowDown") { e.preventDefault(); acMove(1); return; }
    if (e.key === "ArrowUp") { e.preventDefault(); acMove(-1); return; }
    if (e.key === "Escape") { e.preventDefault(); acClose(); return; }
    if (e.key === "Tab" || (e.key === "Enter" && !e.ctrlKey && !e.metaKey && !isMobile())) { e.preventDefault(); acAccept(ac.sel); return; }
  }
  if (e.key === " " && e.ctrlKey) { e.preventDefault(); acUpdate(true); return; }     // Ctrl+Space: suggestions now
  if (e.key === "Tab" && !e.shiftKey) {
    e.preventDefault();
    document.execCommand("insertText", false, "    ");
  } else if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
    e.preventDefault(); runProject();
  } else if (e.key === "Enter") {
    // keep the indentation of the current line, one more after "{"
    const v = editor.value, p = editor.selectionStart;
    const lineStart = v.lastIndexOf("\n", p - 1) + 1;
    let indent = /^[ \t]*/.exec(v.slice(lineStart, p))[0];
    if (v[p - 1] === "{") indent += "    ";
    e.preventDefault();
    document.execCommand("insertText", false, "\n" + indent);
  }
});

// errors: "Game.cs line 12: message" -> open the file and select the line
function clearError() { $("#error").hidden = true; }
function showError(title, msg) {
  setStatus(title, "bad");
  $("#errorTitle").textContent = title;
  $("#errorMsg").textContent = msg;
  $("#error").hidden = false;
  const m = /^(?:(\S+\.cs) )?line (\d+)/.exec(msg);
  if (!m) return;
  const file = m[1] && project.files[m[1]] !== undefined ? m[1] : project.current;
  if (file !== project.current) openFile(file);
  showView("code");
  goToLine(+m[2]);
}
function goToLine(line) {
  const lines = editor.value.split("\n");
  let start = 0;
  for (let i = 0; i < line - 1 && i < lines.length; i++) start += lines[i].length + 1;
  const end = start + (lines[line - 1] || "").length;
  const lh = parseFloat(getComputedStyle(editor).lineHeight) || 20;
  editor.scrollTop = Math.max(0, (line - 4) * lh);
  if (!isMobile()) { editor.focus({ preventScroll: true }); editor.setSelectionRange(start, end); }
  syncScroll();
}

// examples
async function loadExamples() {
  try {
    const list = await (await fetch("examples/index.json")).json();
    const sel = $("#examples");
    list.forEach((ex, i) => { const o = document.createElement("option"); o.value = i; o.textContent = ex.name; sel.appendChild(o); });
    sel.onchange = async () => {
      const ex = list[sel.value];
      sel.value = "";
      if (!ex) return;
      const files = {}, media = {};
      for (const f of ex.files) files[f.split("/").pop()] = await (await fetch("examples/" + f)).text();
      for (const f of ex.media) media[f.split("/").pop()] = new Uint8Array(await (await fetch("examples/" + f)).arrayBuffer());
      const meta = createProject(ex.name, { files, media });
      history.replaceState(null, "", location.pathname);
      setStatus(`Nowy projekt z przykładu: ${meta.name}`, "ok");
    };
  } catch (e) { $("#examples").disabled = true; }
}

// sharing: the .cs files go into the link (compressed when the browser can)
async function shareLink() {
  saveProject();
  const json = enc.encode(JSON.stringify({ f: project.files, c: project.current }));
  let data = json, tag = "j";
  if (window.CompressionStream) {
    data = new Uint8Array(await new Response(new Blob([json]).stream().pipeThrough(new CompressionStream("deflate-raw"))).arrayBuffer());
    tag = "z";
  }
  const code = tag + b64.from(data).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  const url = `${location.origin}${location.pathname}#code=${code}`;
  history.replaceState(null, "", url);
  let note = Object.keys(project.media).length ? " (obrazki i dźwięki nie mieszczą się w linku)" : "";
  try { await navigator.clipboard.writeText(url); setStatus("Link skopiowany do schowka" + note, "ok"); }
  catch (e) { prompt("Skopiuj link:", url); }
}
async function loadFromHash() {
  const m = /#code=([jz])([A-Za-z0-9_-]+)/.exec(location.hash);
  if (!m) return null;
  try {
    let data = b64.to(m[2].replace(/-/g, "+").replace(/_/g, "/"));
    if (m[1] === "z") data = new Uint8Array(await new Response(new Blob([data]).stream().pipeThrough(new DecompressionStream("deflate-raw"))).arrayBuffer());
    const o = JSON.parse(dec.decode(data));
    if (o.f && Object.keys(o.f).length) return { files: o.f, media: {}, current: o.c };
  } catch (e) {}
  return null;
}

// help: web/dist/help.json, generated by tools/gen_help.py from sdk/Help.txt (topics) and sdk/MiniSharp.Api.cs (API)
async function loadHelp() {
  const body = $("#helpBody"), search = $("#helpSearch");
  let data;
  try { data = await (await fetch("help.json")).json(); } catch (e) { helpResolve(null); return; }
  helpData = data;
  helpResolve(data);
  const entriesOf = {};
  for (const e of data.entries) (entriesOf[e.group] ||= []).push(e);
  const topics = data.groups.filter((g) => !g.api && g.name !== "Edytor");   // "Edytor" is the console's own editor
  const classes = data.groups.filter((g) => g.api && (entriesOf[g.name] || []).length);
  const el = (tag, cls, text) => { const x = document.createElement(tag); if (cls) x.className = cls; if (text) x.textContent = text; return x; };

  // paste an API call into the code, at the cursor (like "insert" in the 3DS help)
  function insertCode(text) {
    $("#help").close();
    if (isMobile()) showView("code");
    editor.focus();
    document.execCommand("insertText", false, text);
  }
  function entryEl(e) {
    const d = el("div", "apientry");
    d.append(el("code", "", e.sig));
    if (e.doc) d.append(el("p", "", e.doc));
    const b = el("button", "", "Wstaw");
    b.type = "button";
    b.onclick = () => insertCode(e.insert);
    d.append(b);
    return d;
  }
  function topicEl(t, open) {
    const d = el("details"), s = el("summary", "", t.name);
    d.open = !!open;
    d.append(s, el("pre", "", t.doc));
    return d;
  }
  function classEl(c, open) {
    const d = el("details"), s = el("summary", "", c.name);
    d.open = !!open;
    if (c.doc) s.append(el("small", "", c.doc));
    d.append(s);
    for (const e of entriesOf[c.name]) d.append(entryEl(e));
    return d;
  }

  function render() {
    body.textContent = "";
    const q = search.value.trim().toLowerCase();
    if (!q) {
      body.append(el("h3", "", "Język"));
      topics.forEach((t) => body.append(topicEl(t)));
      body.append(el("h3", "", "API"));
      classes.forEach((c) => body.append(classEl(c)));
      return;
    }
    const hitTopics = topics.filter((t) => (t.name + "\n" + t.doc).toLowerCase().includes(q));
    const hitEntries = data.entries.filter((e) => (e.group + "." + e.name + " " + e.sig + " " + e.doc).toLowerCase().includes(q));
    if (hitTopics.length) { body.append(el("h3", "", "Język")); hitTopics.forEach((t) => body.append(topicEl(t, true))); }
    if (hitEntries.length) {
      body.append(el("h3", "", "API"));
      hitEntries.slice(0, 60).forEach((e) => body.append(entryEl(e)));
      if (hitEntries.length > 60) body.append(el("p", "helpempty", "…i " + (hitEntries.length - 60) + " więcej, wpisz dokładniej."));
    }
    if (!hitTopics.length && !hitEntries.length) body.append(el("p", "helpempty", "Nic nie znaleziono."));
  }
  search.addEventListener("input", render);
  render();
  $("#build").textContent = "Wersja strony: " + (BUILD.startsWith("__") ? "dev" : BUILD);
}

// ---------------------------------------------------------------- layout: phone or computer
const phoneQuery = matchMedia("(max-width: 760px), (pointer: coarse) and (max-width: 1100px)");
const isMobile = () => document.body.classList.contains("mobile");
function applyLayout() {
  document.body.classList.toggle("mobile", phoneQuery.matches);
  const v = document.body.dataset.view;
  if (phoneQuery.matches) { if (!v) document.body.dataset.view = "code"; }
  else if (v !== "graph") document.body.dataset.view = "code";       // on a computer: Kod or Graf next to the game
}
function showView(v) {
  if (!isMobile() && v === "game") v = "code";
  document.body.dataset.view = v;
  document.querySelectorAll("[data-view-btn]").forEach((b) => b.classList.toggle("active", b.dataset.viewBtn === v));
  if (v === "game") editor.blur();
  if (v === "graph") refreshGraphTab(); else graphToken++;
}
phoneQuery.addEventListener?.("change", applyLayout);
document.querySelectorAll("[data-view-btn]").forEach((b) => (b.onclick = () => showView(b.dataset.viewBtn)));

$("#run").onclick = runProject;
$("#stop").onclick = () => stopGame();
document.querySelectorAll("[data-action=stop]").forEach((b) => (b.onclick = () => stopGame()));
document.querySelectorAll("[data-action=run]").forEach((b) => (b.onclick = runProject));
$("#share").onclick = shareLink;
$("#helpBtn").onclick = () => $("#help").showModal();
$("#helpClose").onclick = () => $("#help").close();
$("#errorClose").onclick = clearError;
document.addEventListener("visibilitychange", () => { if (!document.hidden) lastTick = performance.now(); });

// ---------------------------------------------------------------- start
(async function main() {
  applyLayout();
  if (isMobile()) showView("code");
  clearScreens();
  await initProjects();
  loadExamples();
  loadHelp();
  setStatus("Ładowanie kompilatora…", "");
  try {
    const imports = { wasi_snapshot_preview1: wasiProxy, env };
    const res = await fetch("minisharp.wasm");
    const { instance } = WebAssembly.instantiateStreaming && res.headers.get("content-type") === "application/wasm"
      ? await WebAssembly.instantiateStreaming(res, imports)
      : await WebAssembly.instantiate(await res.arrayBuffer(), imports);
    wasm = instance.exports;
    wasm._initialize();
    $("#run").disabled = false;
    setStatus("Gotowe. Ctrl+Enter albo „Uruchom”.", "");
  } catch (e) {
    console.error(e);
    setStatus("Ta przeglądarka nie uruchomi kompilatora (potrzebny WebAssembly z obsługą wyjątków).", "bad");
  }
})();
