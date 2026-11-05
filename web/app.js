// MiniSharp in the browser: the compiler and VM run as WebAssembly (minisharp.wasm, built from the same
// C sources as the 3DS app). This file gives it files, drawing, input and sound.
"use strict";

const $ = (s) => document.querySelector(s);
const enc = new TextEncoder(), dec = new TextDecoder();
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
const STORE = "minisharp.project.v1";
let project = { files: {}, media: {}, current: "" };

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

function saveProject() {
  if (project.current) project.files[project.current] = editor.value;
  try {
    const media = {};
    for (const [k, v] of Object.entries(project.media)) media[k] = b64.from(v);
    localStorage.setItem(STORE, JSON.stringify({ files: project.files, media, current: project.current }));
  } catch (e) {
    try { localStorage.setItem(STORE, JSON.stringify({ files: project.files, media: {}, current: project.current })); } catch (e2) {}
  }
}

function loadSaved() {
  try {
    const s = JSON.parse(localStorage.getItem(STORE) || "null");
    if (s && s.files && Object.keys(s.files).length) {
      const media = {};
      for (const [k, v] of Object.entries(s.media || {})) media[k] = b64.to(v);
      return { files: s.files, media, current: s.files[s.current] !== undefined ? s.current : Object.keys(s.files)[0] };
    }
  } catch (e) {}
  return null;
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
  updateGutter();
  renderFiles();
}

function renderFiles() {
  const list = $("#files");
  list.innerHTML = "";
  const names = Object.keys(project.files).sort();
  for (const n of names) {
    const b = document.createElement("button");
    b.className = "file" + (n === project.current ? " active" : "");
    b.textContent = n;
    b.title = "Kliknij dwa razy, żeby zmienić nazwę";
    b.onclick = () => openFile(n);
    b.ondblclick = () => renameFile(n);
    if (names.length > 1) {
      const x = document.createElement("span");
      x.className = "x"; x.textContent = "×"; x.title = "Usuń plik";
      x.onclick = (e) => { e.stopPropagation(); if (confirm(`Usunąć ${n}?`)) { delete project.files[n]; if (project.current === n) project.current = ""; setProject(project); saveProject(); } };
      b.appendChild(x);
    }
    list.appendChild(b);
  }
  const add = document.createElement("button");
  add.className = "file add"; add.textContent = "+ plik";
  add.onclick = addFile;
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
function renameFile(old) {
  let n = prompt("Nowa nazwa pliku:", old);
  if (!n || n === old) return;
  n = n.trim(); if (!n.endsWith(".cs")) n += ".cs";
  if (!validName(n) || project.files[n] !== undefined) { alert("Ta nazwa jest zajęta albo niepoprawna."); return; }
  project.files[old] = old === project.current ? editor.value : project.files[old];
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

// line numbers
function updateGutter() {
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
editor.addEventListener("scroll", () => { gutter.scrollTop = editor.scrollTop; });
editor.addEventListener("keydown", (e) => {
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
  goToLine(+m[2]);
  if (isMobile()) showView("code");
}
function goToLine(line) {
  const lines = editor.value.split("\n");
  let start = 0;
  for (let i = 0; i < line - 1 && i < lines.length; i++) start += lines[i].length + 1;
  const end = start + (lines[line - 1] || "").length;
  const lh = parseFloat(getComputedStyle(editor).lineHeight) || 20;
  editor.scrollTop = Math.max(0, (line - 4) * lh);
  if (!isMobile()) { editor.focus({ preventScroll: true }); editor.setSelectionRange(start, end); }
  gutter.scrollTop = editor.scrollTop;
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
      if (!confirm(`Wczytać przykład „${ex.name}”? Obecny kod zostanie zastąpiony.`)) return;
      const files = {}, media = {};
      for (const f of ex.files) files[f.split("/").pop()] = await (await fetch("examples/" + f)).text();
      for (const f of ex.media) media[f.split("/").pop()] = new Uint8Array(await (await fetch("examples/" + f)).arrayBuffer());
      stopGame(true);
      setProject({ files, media, current: Object.keys(files).sort()[0] });
      saveProject();
      history.replaceState(null, "", location.pathname);
      setStatus(`Wczytano: ${ex.name}`, "");
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

// help: sdk/Help.txt, "## Title" starts a section
async function loadHelp() {
  try {
    const text = await (await fetch("help.txt")).text();
    const body = $("#helpBody");
    for (const sec of text.split(/^## /m).slice(1)) {
      const nl = sec.indexOf("\n");
      const title = sec.slice(0, nl).trim();
      if (title === "Edytor") continue;          // the console's own editor
      const d = document.createElement("details");
      const s = document.createElement("summary"); s.textContent = title;
      const pre = document.createElement("pre"); pre.textContent = sec.slice(nl + 1).trim();
      d.append(s, pre);
      body.appendChild(d);
    }
  } catch (e) {}
}

// ---------------------------------------------------------------- layout: phone or computer
const phoneQuery = matchMedia("(max-width: 760px), (pointer: coarse) and (max-width: 1100px)");
const isMobile = () => document.body.classList.contains("mobile");
function applyLayout() {
  document.body.classList.toggle("mobile", phoneQuery.matches);
  if (!phoneQuery.matches) document.body.dataset.view = "";
  else if (!document.body.dataset.view) document.body.dataset.view = "code";
}
function showView(v) {
  document.body.dataset.view = v;
  document.querySelectorAll("[data-view-btn]").forEach((b) => b.classList.toggle("active", b.dataset.viewBtn === v));
  if (v === "game") editor.blur();
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
  setProject((await loadFromHash()) || loadSaved() || { files: { "Game.cs": START_CODE }, media: {}, current: "Game.cs" });
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
