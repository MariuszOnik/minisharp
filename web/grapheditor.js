/* MiniSharp graph editor (SVG, works with touch and mouse).  The graph model and the code generator are in graph.js.
 *
 *   const ed = MSGraphEditor.create(hostElement, { onChange(graph), prompt(msg, def), notify(text),
 *                                                 leading: [elements put first in the toolbar], extraMenu() -> [{label, note, fn}],
 *                                                 refreshCatalog() -> a new catalog (called when the palette opens) });
 *   ed.load(graph, catalog)   show a graph          ed.getGraph()      the current graph
 *   ed.setIssues([{node, message}])   warnings from the generator, listed behind the "Uwagi" button
 *
 * Gestures: drag the background = move the view, two fingers or the wheel = zoom, drag a node by its title bar,
 * drag from a port to another port to wire them, or tap one port and then the other. Tap a wire or a node to select it
 * (then "Usuń" removes it). Tap a value in a node (a default of an input, the number of a "Wartość" node, a variable
 * name) to change it. */
(function (root) {
"use strict";

const NS = "http://www.w3.org/2000/svg";
const ROW = 26, HEAD = 30, CHAR = 7.4, GRID = 10;
const HEAD_COLOR = { "Zdarzenia": "#e5736b", "Sterowanie": "#c792ea", "Zmienne": "#7fd1c7", "Matematyka": "#ffd479", "Porównania": "#f78c6c", "Logika": "#f78c6c" };
const FLOW_COLOR = "#dfe5ee", VALUE_COLOR = "#82aaff";
const HIDDEN_LABELS = new Set(["enter", "exit", "result", "value"]);
const IDENT = /^[A-Za-z_]\w*$/;

function svgEl(tag, attrs, parent) {
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}
function html(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function create(host, opts) {
  opts = opts || {};
  const ask = opts.prompt || ((m, d) => window.prompt(m, d));
  const notify = opts.notify || (() => {});
  let graph = { className: "Game", variables: [], nodes: [], edges: [] };
  let catalog = null;
  let issues = [];
  const view = { tx: 0, ty: 0, s: 1 };
  let sel = null;                 // { kind: "node", id } | { kind: "edge", edge }
  let pending = null;             // a port waiting for the second tap: { node, dir, name }
  let tempWire = null;            // { from: {x, y}, to: {x, y}, flow }
  let lay = new Map();            // node id -> layout

  // ---------------------------------------------------------------- DOM
  host.textContent = "";
  host.classList.add("gedit");
  const tools = html("div", "gtools");
  const btnAdd = html("button", "primary", "+ Węzeł"), btnDel = html("button", "danger", "Usuń"),
    btnMore = html("button", "", "⋯"), btnIssues = html("button", "issues", "⚠");
  btnMore.title = "Więcej: zmienne, widok, uwagi"; btnMore.setAttribute("aria-label", "Więcej");
  for (const b of [btnAdd, btnDel, btnMore, btnIssues]) b.type = "button";
  tools.append(...(opts.leading || []), btnAdd, btnDel, btnMore, btnIssues);
  btnDel.disabled = true;
  const canvas = html("div", "gcanvas");
  const svg = svgEl("svg", { class: "gsvg", width: "100%", height: "100%" });
  const btnFab = html("button", "gfab", "⤢");
  btnFab.type = "button"; btnFab.title = "Dopasuj widok do całego grafu"; btnFab.setAttribute("aria-label", "Dopasuj widok");
  canvas.append(svg, btnFab);
  host.append(tools, canvas);
  const defs = svgEl("defs", {}, svg);
  const pattern = svgEl("pattern", { id: "gdots", width: 20, height: 20, patternUnits: "userSpaceOnUse" }, defs);
  svgEl("circle", { cx: 1, cy: 1, r: 1, fill: "#2a3342" }, pattern);
  const bg = svgEl("rect", { class: "gbg", width: "100%", height: "100%", fill: "url(#gdots)" }, svg);
  const world = svgEl("g", { class: "world" }, svg);
  const gWires = svgEl("g", { class: "wires" }, world);
  const gNodes = svgEl("g", { class: "nodes" }, world);
  const gTemp = svgEl("path", { class: "tempwire", fill: "none", "pointer-events": "none" }, world);

  // ---------------------------------------------------------------- helpers
  const defOf = (n) => (catalog ? catalog.types.get(n.type) : null);
  const nodeById = (id) => graph.nodes.find((n) => n.id === id);
  const snap = (v) => Math.round(v / GRID) * GRID;
  function toWorld(cx, cy) {
    const r = svg.getBoundingClientRect();
    return { x: (cx - r.left - view.tx) / view.s, y: (cy - r.top - view.ty) / view.s };
  }
  function applyView() {
    world.setAttribute("transform", `translate(${view.tx} ${view.ty}) scale(${view.s})`);
    pattern.setAttribute("patternTransform", `translate(${view.tx} ${view.ty}) scale(${view.s})`);
  }
  function zoomAt(cx, cy, k) {
    const r = svg.getBoundingClientRect(), s = Math.max(0.25, Math.min(2, view.s * k)), f = s / view.s;
    const px = cx - r.left, py = cy - r.top;
    view.tx = px - (px - view.tx) * f; view.ty = py - (py - view.ty) * f; view.s = s;
    applyView();
  }
  const isConnected = (id, dir, name) => graph.edges.some((e) => (dir === "in" ? e.to === id && e.in === name : e.from === id && e.out === name));
  const shortText = (t, n) => (t.length > n ? t.slice(0, n - 1) + "…" : t);

  function defaultText(n, d, p) {
    const given = n.data && n.data.defaults && n.data.defaults[p.name];
    return given !== undefined && given !== "" ? String(given) : (p.def !== undefined ? p.def : "");
  }
  function layout(n) {
    const d = defOf(n);
    if (!d) return { d: null, w: 160, h: HEAD + ROW, inY: {}, outY: {}, dataRows: 0, inLabel: {}, outLabel: {} };
    const dataRows = d.kind === "lit" || d.kind === "getvar" || d.kind === "setvar" || d.custom ? 1 : 0;
    const inLabel = {}, outLabel = {}, inY = {}, outY = {};
    let left = 0, right = 0;
    d.inputs.forEach((p, i) => {
      let t = p.flow ? (HIDDEN_LABELS.has(p.name) ? "" : p.name) : p.name;
      if (!p.flow && !isConnected(n.id, "in", p.name)) t += "  " + shortText(defaultText(n, d, p), 10);
      inLabel[p.name] = t; left = Math.max(left, t.length);
      inY[p.name] = HEAD + dataRows * ROW + i * ROW + ROW / 2;
    });
    d.outputs.forEach((p, i) => {
      const t = HIDDEN_LABELS.has(p.name) ? "" : p.name;
      outLabel[p.name] = t; right = Math.max(right, t.length);
      outY[p.name] = HEAD + dataRows * ROW + i * ROW + ROW / 2;
    });
    let dataLen = 0;
    if (dataRows) dataLen = shortText(dataText(n, d), 16).length + 2;
    const w = Math.min(340, Math.max(124, d.title.length * CHAR * 1.05 + 26, (left + right) * CHAR + 56, dataLen * CHAR + 30));
    const rows = Math.max(d.inputs.length, d.outputs.length);
    return { d, w: Math.ceil(w / GRID) * GRID, h: HEAD + (dataRows + rows) * ROW + 8, inY, outY, dataRows, inLabel, outLabel };
  }
  function dataText(n, d) {
    if (d.custom) return (n.data && n.data.name) || "(nazwa metody)";
    if (d.kind === "lit") return (n.data && n.data.value) !== undefined ? String(n.data.value) : "0";
    return (n.data && n.data.name) || "(wybierz zmienną)";
  }
  const portPos = (n, dir, name) => {
    const l = lay.get(n.id);
    return dir === "in" ? { x: n.x, y: n.y + (l.inY[name] ?? HEAD) } : { x: n.x + l.w, y: n.y + (l.outY[name] ?? HEAD) };
  };

  // ---------------------------------------------------------------- drawing
  function wirePath(a, b) {
    const dx = Math.max(40, Math.abs(b.x - a.x) / 2);
    return `M ${a.x} ${a.y} C ${a.x + dx} ${a.y}, ${b.x - dx} ${b.y}, ${b.x} ${b.y}`;
  }
  function renderWires() {
    gWires.textContent = "";
    for (const e of graph.edges) {
      const a = nodeById(e.from), b = nodeById(e.to);
      if (!a || !b || !lay.has(a.id) || !lay.has(b.id)) continue;
      const da = defOf(a), po = da && da.outputs.find((p) => p.name === e.out);
      const flow = !!(po && po.flow), p1 = portPos(a, "out", e.out), p2 = portPos(b, "in", e.in);
      const selected = sel && sel.kind === "edge" && sel.edge === e;
      const d = wirePath(p1, p2);
      const g = svgEl("g", { class: "wire" + (selected ? " selected" : "") }, gWires);
      svgEl("path", { d, fill: "none", stroke: selected ? "#ffffff" : flow ? FLOW_COLOR : VALUE_COLOR, "stroke-width": selected ? 4 : flow ? 3 : 2.2, "pointer-events": "none" }, g);
      const hit = svgEl("path", { d, fill: "none", stroke: "transparent", "stroke-width": 20, class: "whit" }, g);
      hit._edge = e;
    }
    if (tempWire) gTemp.setAttribute("d", wirePath(tempWire.from, tempWire.to)), gTemp.setAttribute("stroke", tempWire.flow ? FLOW_COLOR : VALUE_COLOR);
    else gTemp.removeAttribute("d");
    gTemp.setAttribute("stroke-width", 2.5); gTemp.setAttribute("stroke-dasharray", "6 5");
  }
  function drawPortShape(parent, flow, x, y, filled, color) {
    if (flow) return svgEl("polygon", { points: `${x - 6},${y - 7} ${x + 6},${y} ${x - 6},${y + 7}`, fill: filled ? color : "#171c25", stroke: color, "stroke-width": 2, "pointer-events": "none" }, parent);
    return svgEl("circle", { cx: x, cy: y, r: 6, fill: filled ? color : "#171c25", stroke: color, "stroke-width": 2, "pointer-events": "none" }, parent);
  }
  function renderNodes() {
    gNodes.textContent = "";
    lay = new Map();
    for (const n of graph.nodes) lay.set(n.id, layout(n));
    for (const n of graph.nodes) {
      const l = lay.get(n.id), d = l.d;
      const selected = sel && sel.kind === "node" && sel.id === n.id;
      const g = svgEl("g", { class: "node" + (selected ? " selected" : "") + (d ? "" : " unknown"), transform: `translate(${n.x} ${n.y})`, "data-id": n.id }, gNodes);
      const color = d ? (HEAD_COLOR[d.category] || VALUE_COLOR) : "#ff7a7a";
      const hasIssue = issues.some((i) => i.node === n.id);
      svgEl("rect", { class: "nbody", width: l.w, height: l.h, rx: 9, fill: "#1e2531", stroke: selected ? "#ffffff" : hasIssue ? "#ffb454" : "#3c4a60", "stroke-width": selected || hasIssue ? 2.5 : 1.5 }, g);
      svgEl("path", { d: `M0 ${HEAD} V9 a9 9 0 0 1 9 -9 H${l.w - 9} a9 9 0 0 1 9 9 V${HEAD} Z`, fill: color, "fill-opacity": 0.28, class: "nhead" }, g);
      svgEl("text", { x: 11, y: 20, class: "ntitle", fill: color, "pointer-events": "none" }, g).textContent = d ? d.title : `? ${n.type}`;
      const head = svgEl("rect", { x: 0, y: 0, width: l.w, height: HEAD, fill: "transparent", class: "nhit" }, g);
      head.setAttribute("data-drag", n.id);
      if (!d) continue;
      if (l.dataRows) {
        const y = HEAD + ROW / 2;
        svgEl("rect", { x: 8, y: HEAD + 3, width: l.w - 16, height: ROW - 6, rx: 5, fill: "#11151c", stroke: "#2a3342", "data-edit": "data" }, g).setAttribute("data-node", n.id);
        svgEl("text", { x: l.w / 2, y: y + 4.5, "text-anchor": "middle", class: "ndata", "pointer-events": "none" }, g).textContent = shortText(dataText(n, d), 24);
      }
      d.inputs.forEach((p) => {
        const y = l.inY[p.name], conn = isConnected(n.id, "in", p.name), color2 = p.flow ? FLOW_COLOR : VALUE_COLOR;
        const editable = !p.flow && !conn;
        if (editable) { const r = svgEl("rect", { x: 14, y: y - ROW / 2, width: l.w / 2 + 6, height: ROW, fill: "transparent", "data-edit": "default" }, g); r.setAttribute("data-node", n.id); r.setAttribute("data-name", p.name); }
        if (l.inLabel[p.name]) svgEl("text", { x: 14, y: y + 4.5, class: "plabel" + (editable ? " editable" : ""), "pointer-events": "none" }, g).textContent = l.inLabel[p.name];
        drawPortShape(g, p.flow, 0, y, conn, color2);
        const hit = svgEl("circle", { cx: 0, cy: y, r: 17, fill: "transparent", class: "phit" }, g);
        hit.setAttribute("data-port", "1"); hit.setAttribute("data-node", n.id); hit.setAttribute("data-dir", "in"); hit.setAttribute("data-name", p.name);
        if (pending && pending.node === n.id && pending.dir === "in" && pending.name === p.name) svgEl("circle", { cx: 0, cy: y, r: 12, fill: "none", stroke: "#ffd479", "stroke-width": 2.5, "pointer-events": "none" }, g);
      });
      d.outputs.forEach((p) => {
        const y = l.outY[p.name], conn = isConnected(n.id, "out", p.name), color2 = p.flow ? FLOW_COLOR : VALUE_COLOR;
        if (l.outLabel[p.name]) svgEl("text", { x: l.w - 14, y: y + 4.5, "text-anchor": "end", class: "plabel", "pointer-events": "none" }, g).textContent = l.outLabel[p.name];
        drawPortShape(g, p.flow, l.w, y, conn, color2);
        const hit = svgEl("circle", { cx: l.w, cy: y, r: 17, fill: "transparent", class: "phit" }, g);
        hit.setAttribute("data-port", "1"); hit.setAttribute("data-node", n.id); hit.setAttribute("data-dir", "out"); hit.setAttribute("data-name", p.name);
        if (pending && pending.node === n.id && pending.dir === "out" && pending.name === p.name) svgEl("circle", { cx: l.w, cy: y, r: 12, fill: "none", stroke: "#ffd479", "stroke-width": 2.5, "pointer-events": "none" }, g);
      });
    }
    renderWires();
    updateTools();
  }
  function render() { renderNodes(); }
  function updateTools() {
    btnDel.disabled = !sel;
    const bad = issues.length;
    btnIssues.hidden = !bad;
    btnIssues.textContent = bad ? `⚠ ${bad}` : "⚠";
    btnIssues.title = "Uwagi do grafu";
  }

  // ---------------------------------------------------------------- editing
  function changed() { render(); if (opts.onChange) opts.onChange(graph); }
  function portKindOf(node, dir, name) {
    const d = defOf(node), p = d && (dir === "in" ? d.inputs : d.outputs).find((q) => q.name === name);
    return p ? (p.flow ? "flow" : "value") : null;
  }
  function connect(a, b) {
    if (!a || !b) return false;
    if (a.dir === b.dir) { notify("Połącz wyjście (po prawej) z wejściem (po lewej)."); return false; }
    const out = a.dir === "out" ? a : b, inn = a.dir === "in" ? a : b;
    if (out.node === inn.node) { notify("Nie można połączyć węzła z samym sobą."); return false; }
    const ka = portKindOf(nodeById(out.node), "out", out.name), kb = portKindOf(nodeById(inn.node), "in", inn.name);
    if (!ka || !kb) return false;
    if (ka !== kb) { notify(ka === "flow" ? "Przepływ (trójkąt) łączy się tylko z przepływem." : "Wartość (kółko) łączy się tylko z wartością."); return false; }
    if (graph.edges.some((e) => e.from === out.node && e.out === out.name && e.to === inn.node && e.in === inn.name)) return false;
    if (kb === "value") graph.edges = graph.edges.filter((e) => !(e.to === inn.node && e.in === inn.name));      // one wire into a value input
    if (ka === "flow") graph.edges = graph.edges.filter((e) => !(e.from === out.node && e.out === out.name));    // one wire out of a flow output
    graph.edges.push({ from: out.node, out: out.name, to: inn.node, in: inn.name });
    return true;
  }
  function removeSelection() {
    if (!sel) return;
    if (sel.kind === "node") {
      graph.nodes = graph.nodes.filter((n) => n.id !== sel.id);
      graph.edges = graph.edges.filter((e) => e.from !== sel.id && e.to !== sel.id);
    } else graph.edges = graph.edges.filter((e) => e !== sel.edge);
    sel = null; pending = null;
    changed();
  }
  // the nearest place to the centre of the view where the new node does not cover another one
  function freeSpot(c, n) {
    const l = layout(n);
    const boxes = graph.nodes.map((m) => { const lm = lay.get(m.id) || layout(m); return { x: m.x, y: m.y, w: lm.w, h: lm.h }; });
    const busy = (x, y) => boxes.some((b) => x < b.x + b.w + 20 && x + l.w + 20 > b.x && y < b.y + b.h + 20 && y + l.h + 20 > b.y);
    const cx = snap(c.x - l.w / 2), cy = snap(c.y - l.h / 2);
    for (let r = 0; r < 14; r++) {
      for (let dy = -r; dy <= r; dy++) {
        for (let dx = -r; dx <= r; dx++) {
          if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) continue;
          const x = cx + dx * 50, y = cy + dy * 50;
          if (!busy(x, y)) return { x, y };
        }
      }
    }
    return { x: cx, y: cy };
  }
  function addNode(typeId, at) {
    const d = catalog && catalog.types.get(typeId);
    if (!d) return null;
    const r = svg.getBoundingClientRect();
    let c = at || toWorld(r.left + r.width / 2, r.top + r.height / 2);
    const n = { id: root.MSGraph.nextId(graph), type: typeId, x: 0, y: 0, data: {} };
    // with a node selected, the new one goes to its right: chains grow from left to right
    const from = !at && sel && sel.kind === "node" ? nodeById(sel.id) : null;
    if (from && lay.has(from.id)) { const lf = lay.get(from.id), ln = layout(Object.assign({}, n, { type: typeId })); c = { x: from.x + lf.w + 70 + ln.w / 2, y: from.y + ln.h / 2 }; }
    if (d.kind === "lit") n.data.value = "0";
    if (d.kind === "getvar" || d.kind === "setvar") n.data.name = (graph.variables[0] && graph.variables[0].name) || "";
    if (d.custom) { let k = 1; while (graph.nodes.some((m) => m.data && m.data.name === "Metoda" + k)) k++; n.data.name = "Metoda" + k; }
    const spot = freeSpot(c, n);
    n.x = spot.x; n.y = spot.y;
    graph.nodes.push(n);
    sel = { kind: "node", id: n.id };
    changed();
    return n;
  }
  function editData(n) {
    const d = defOf(n);
    if (!d) return;
    if (d.custom) {
      const v = ask("Nazwa metody (z kodu wywołasz ją jako Klasa.Nazwa(), np. Graph.Tick()):", (n.data && n.data.name) || "");
      if (v === null) return;
      if (!IDENT.test(v.trim())) { notify("Nazwa metody: litery, cyfry i _, nie może zaczynać się od cyfry."); return; }
      if (["Main", "Update", "Draw"].includes(v.trim())) { notify("Main, Update i Draw mają własne węzły zdarzeń."); return; }
      n.data.name = v.trim();
    } else if (d.kind === "lit") {
      const v = ask('Wartość (wyrażenie, np. 10, 1.5f, true, "tekst", Btn.A):', (n.data && n.data.value) !== undefined ? String(n.data.value) : "0");
      if (v === null) return;
      n.data.value = v.trim() === "" ? "0" : v.trim();
    } else {
      const names = (graph.variables || []).map((x) => x.name);
      const v = ask(names.length ? `Nazwa zmiennej (${names.join(", ")}):` : "Nazwa zmiennej (najpierw dodaj ją w „Zmienne”):", (n.data && n.data.name) || names[0] || "");
      if (v === null) return;
      if (!IDENT.test(v.trim())) { notify("Nazwa zmiennej: litery, cyfry i _, nie może zaczynać się od cyfry."); return; }
      n.data.name = v.trim();
    }
    changed();
  }
  // text typed for a string (or object) input gets its quotes; a number, true/false/null, a name or a call stays as typed
  function needsQuotes(type, v) {
    if (/^["']/.test(v)) return false;
    if (type === "string") return !IDENT.test(v);
    if (type === "object") return !(/^-?\d[\w.]*$/.test(v) || /^(true|false|null)$/.test(v) || /^[A-Za-z_][\w.]*(\(.*\))?$/.test(v));
    return false;
  }
  function editDefault(n, portName) {
    const d = defOf(n), p = d && d.inputs.find((q) => q.name === portName);
    if (!p) return;
    const hint = p.type === "string" ? " (tekst: cudzysłów dodam sam, chyba że to nazwa zmiennej)"
      : p.type === "object" ? " (tekst: cudzysłów dodam sam; liczba, true/false i nazwa zmiennej zostają bez zmian)" : "";
    let v = ask(`${d.title}: ${p.name}${p.type ? " (" + p.type + ")" : ""}${hint}`, defaultText(n, d, p));
    if (v === null) return;
    v = v.trim();
    n.data = n.data || {};
    n.data.defaults = n.data.defaults || {};
    if (v === "") delete n.data.defaults[p.name];
    else {
      if (needsQuotes(p.type, v)) v = '"' + v.replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
      n.data.defaults[p.name] = v;
    }
    changed();
  }

  // ---------------------------------------------------------------- gestures
  const pointers = new Map();
  let gesture = null, pinch = null;
  const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
  function portFromEl(el) {
    return el && el.getAttribute && el.getAttribute("data-port") ? { node: el.getAttribute("data-node"), dir: el.getAttribute("data-dir"), name: el.getAttribute("data-name") } : null;
  }
  function select(s) { sel = s; updateTools(); renderNodes(); }

  svg.addEventListener("pointerdown", (e) => {
    if (e.button > 0) return;
    svg.setPointerCapture(e.pointerId);
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pointers.size === 2) {                                   // two fingers: zoom and move
      const [a, b] = [...pointers.values()];
      pinch = { d: dist(a, b), s: view.s, cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2, tx: view.tx, ty: view.ty };
      gesture = { type: "pinch" };
      tempWire = null; renderWires();
      return;
    }
    const t = e.target, port = portFromEl(t.closest && t.closest("[data-port]"));
    const dragEl = t.closest && t.closest("[data-drag]"), editEl = t.closest && t.closest("[data-edit]"), wireEl = t.closest && t.closest(".whit");
    gesture = { type: "bg", sx: e.clientX, sy: e.clientY, moved: false, tx: view.tx, ty: view.ty };
    if (port) gesture = { type: "port", port, sx: e.clientX, sy: e.clientY, moved: false };
    else if (dragEl) {
      const n = nodeById(dragEl.getAttribute("data-drag"));
      gesture = { type: "node", n, sx: e.clientX, sy: e.clientY, x0: n.x, y0: n.y, moved: false };
      if (!(sel && sel.kind === "node" && sel.id === n.id)) { sel = { kind: "node", id: n.id }; updateTools(); renderNodes(); gesture.dragEl = gNodes.querySelector(`[data-id="${n.id}"]`); }
      else gesture.dragEl = gNodes.querySelector(`[data-id="${n.id}"]`);
    } else if (editEl) gesture = { type: "edit", el: editEl, sx: e.clientX, sy: e.clientY, moved: false };
    else if (wireEl) gesture = { type: "wire", edge: wireEl._edge, sx: e.clientX, sy: e.clientY, moved: false };
    e.preventDefault();
  });
  svg.addEventListener("pointermove", (e) => {
    if (!pointers.has(e.pointerId)) return;
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (!gesture) return;
    if (gesture.type === "pinch") {
      if (pointers.size < 2 || !pinch) return;
      const [a, b] = [...pointers.values()], k = dist(a, b) / pinch.d;
      const r = svg.getBoundingClientRect(), s = Math.max(0.25, Math.min(2, pinch.s * k)), f = s / pinch.s;
      const cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
      const px = pinch.cx - r.left, py = pinch.cy - r.top;
      view.s = s;
      view.tx = (cx - r.left) - (px - pinch.tx) * f;
      view.ty = (cy - r.top) - (py - pinch.ty) * f;
      applyView();
      return;
    }
    const dx = e.clientX - gesture.sx, dy = e.clientY - gesture.sy;
    if (!gesture.moved && Math.hypot(dx, dy) > 6) gesture.moved = true;
    if (!gesture.moved) return;
    if (gesture.type === "bg") { view.tx = gesture.tx + dx; view.ty = gesture.ty + dy; applyView(); }
    else if (gesture.type === "node") {
      gesture.n.x = gesture.x0 + dx / view.s; gesture.n.y = gesture.y0 + dy / view.s;
      if (gesture.dragEl) gesture.dragEl.setAttribute("transform", `translate(${gesture.n.x} ${gesture.n.y})`);
      renderWires();
    } else if (gesture.type === "port") {
      const p = gesture.port, n = nodeById(p.node);
      tempWire = { from: portPos(n, p.dir, p.name), to: toWorld(e.clientX, e.clientY), flow: portKindOf(n, p.dir, p.name) === "flow" };
      if (p.dir === "in") tempWire = { from: tempWire.to, to: tempWire.from, flow: tempWire.flow };
      renderWires();
    }
  });
  function endPointer(e, cancelled) {
    if (!pointers.has(e.pointerId)) return;
    pointers.delete(e.pointerId);
    const g = gesture;
    if (g && g.type === "pinch") { if (pointers.size < 2) { gesture = null; pinch = null; } return; }
    gesture = null;
    if (!g || cancelled) { tempWire = null; renderNodes(); return; }
    if (g.type === "bg") { if (!g.moved) { sel = null; pending = null; renderNodes(); } }
    else if (g.type === "node") {
      if (g.moved) { g.n.x = snap(g.n.x); g.n.y = snap(g.n.y); changed(); }
    } else if (g.type === "port") {
      tempWire = null;
      if (g.moved) {
        const target = portFromEl(document.elementFromPoint(e.clientX, e.clientY));
        if (target && !(target.node === g.port.node && target.dir === g.port.dir && target.name === g.port.name)) {
          pending = null;
          if (connect(g.port, target)) { changed(); return; }
        }
        renderNodes();
      } else {                                                  // a tap: first port, then the second one
        if (!pending) pending = g.port;
        else if (pending.node === g.port.node && pending.dir === g.port.dir && pending.name === g.port.name) pending = null;
        else {
          const first = pending; pending = null;
          if (connect(first, g.port)) { changed(); return; }
        }
        renderNodes();
      }
    } else if (g.type === "edit") {
      if (!g.moved) {
        const n = nodeById(g.el.getAttribute("data-node"));
        if (n) { if (g.el.getAttribute("data-edit") === "data") editData(n); else editDefault(n, g.el.getAttribute("data-name")); }
      } else renderNodes();
    } else if (g.type === "wire") {
      if (!g.moved) select({ kind: "edge", edge: g.edge });
    }
  }
  svg.addEventListener("pointerup", (e) => endPointer(e, false));
  svg.addEventListener("pointercancel", (e) => endPointer(e, true));
  svg.addEventListener("wheel", (e) => { e.preventDefault(); zoomAt(e.clientX, e.clientY, Math.exp(-e.deltaY * 0.0015)); }, { passive: false });

  // ---------------------------------------------------------------- dialogs: palette, variables, issues
  function dialog(title, small) {
    const dlg = html("dialog", "gdialog" + (small ? " small" : ""));
    const head = html("div", "ghead");
    const x = html("button", "", "×"); x.type = "button"; x.setAttribute("aria-label", "Zamknij");
    x.onclick = () => dlg.close();
    head.append(html("strong", "", title), x);
    dlg.append(head);
    dlg.addEventListener("close", () => dlg.remove());
    document.body.append(dlg);
    return dlg;
  }
  function openMenu() {
    const dlg = dialog("Graf", true);
    const list = html("div", "glist");
    dlg.append(list);
    const item = (title, note, fn) => {
      const b = html("button", "gitem"); b.type = "button";
      b.append(html("span", "gtitle", title), html("small", "gsig", note));
      b.onclick = () => { dlg.close(); fn(); };
      list.append(b);
    };
    item("Zmienne grafu", "pola static, np. x, hp, wynik", openVars);
    item("Dopasuj widok", "pokaż cały graf na ekranie", fit);
    item(issues.length ? `Uwagi (${issues.length})` : "Uwagi", issues.length ? "ostrzeżenia generatora kodu" : "brak uwag do grafu", openIssues);
    for (const it of (opts.extraMenu ? opts.extraMenu() : [])) item(it.label, it.note || "", it.fn);
    dlg.showModal();
  }
  function openPalette() {
    if (opts.refreshCatalog) { const c = opts.refreshCatalog(); if (c) { catalog = c; render(); } }      // the code may have changed since the graph was opened
    if (!catalog) return;
    const dlg = dialog("Dodaj węzeł");
    const input = html("input", "gsearch"); input.type = "search"; input.placeholder = "Szukaj (np. rect, held, jeśli, dodaj)"; input.autocomplete = "off"; input.autocapitalize = "off"; input.spellcheck = false;
    const list = html("div", "glist");
    dlg.append(input, list);
    function fill() {
      list.textContent = "";
      const found = root.MSGraph.searchCatalog(catalog, input.value, input.value.trim() ? 60 : 200);
      let cat = "";
      for (const d of found) {
        if (!input.value.trim() && d.category !== cat) { cat = d.category; list.append(html("h4", "", cat)); }
        const row = html("button", "gitem");
        row.type = "button";
        row.append(html("span", "gtitle", d.title), html("small", "gcat", input.value.trim() ? d.category : ""));
        if (d.sig) row.append(html("small", "gsig", d.sig));
        else if (d.doc) row.append(html("small", "gsig", d.doc));
        if (d.user && d.doc) row.append(html("small", "gsig gdoc", d.doc));              // the comment above the method
        row.onclick = () => { dlg.close(); addNode(d.id); };
        list.append(row);
      }
      if (!found.length) list.append(html("p", "gnone", "Nic nie znaleziono."));
      else if (!input.value.trim() && !catalog.list.some((d) => d.user))
        list.append(html("p", "gnote", "Własne węzły: napisz w pliku z kodem metodę static, np. static float Dystans(float a, float b), a pojawi się tutaj. Komentarz // nad metodą to jej opis, „// @node Nazwa” zmienia tytuł węzła, „// @node hide” ukrywa metodę."));
    }
    input.addEventListener("input", fill);
    fill();
    dlg.showModal();
  }
  function openVars() {
    const dlg = dialog("Zmienne grafu");
    const body = html("div", "gvars");
    dlg.append(html("p", "gnote", "To pola „static” klasy. Węzły „Pobierz/Ustaw zmienną” odwołują się do nich po nazwie."), body);
    function fill() {
      body.textContent = "";
      for (const v of graph.variables) {
        const row = html("div", "gvar");
        row.append(html("code", "", `${v.type} ${v.name}${v.init !== undefined && v.init !== "" ? " = " + v.init : ""}`));
        const del = html("button", "danger", "Usuń"); del.type = "button";
        del.onclick = () => { graph.variables = graph.variables.filter((x) => x !== v); changed(); fill(); };
        const edit = html("button", "", "Zmień"); edit.type = "button";
        edit.onclick = () => { if (askVar(v)) { changed(); fill(); } };
        row.append(edit, del);
        body.append(row);
      }
      const add = html("button", "primary gbig", "+ Zmienna"); add.type = "button";
      add.onclick = () => { const v = { name: "", type: "float", init: "0" }; if (askVar(v)) { graph.variables.push(v); changed(); fill(); } };
      body.append(add);
    }
    function askVar(v) {
      const name = ask("Nazwa zmiennej (np. x, hp, wynik):", v.name);
      if (name === null) return false;
      if (!IDENT.test(name.trim())) { notify("Nazwa zmiennej: litery, cyfry i _, nie może zaczynać się od cyfry."); return false; }
      if (graph.variables.some((x) => x !== v && x.name === name.trim())) { notify("Zmienna o tej nazwie już istnieje."); return false; }
      const type = ask("Typ (int, float, bool, string, List<int> ...):", v.type || "float");
      if (type === null) return false;
      const init = ask("Wartość początkowa (może być pusta):", v.init === undefined ? "" : v.init);
      if (init === null) return false;
      v.name = name.trim(); v.type = type.trim() || "float"; v.init = init.trim();
      return true;
    }
    fill();
    dlg.showModal();
  }
  function openIssues() {
    const dlg = dialog("Uwagi do grafu");
    const list = html("div", "glist");
    dlg.append(list);
    if (!issues.length) list.append(html("p", "gnone", "Brak uwag."));
    for (const i of issues) {
      const n = i.node && nodeById(i.node), d = n && defOf(n);
      const row = html("button", "gitem");
      row.type = "button";
      row.append(html("span", "gtitle", i.message), html("small", "gcat", d ? d.title : "cały graf"));
      row.onclick = () => { dlg.close(); if (n) { select({ kind: "node", id: n.id }); centerOn(n); } };
      list.append(row);
    }
    dlg.showModal();
  }
  function centerOn(n) {
    const r = svg.getBoundingClientRect(), l = lay.get(n.id) || { w: 120, h: 60 };
    view.tx = r.width / 2 - (n.x + l.w / 2) * view.s; view.ty = r.height / 2 - (n.y + l.h / 2) * view.s;
    applyView();
  }
  function fit() {
    const r = svg.getBoundingClientRect();
    if (!graph.nodes.length || !r.width) { view.tx = 20; view.ty = 20; view.s = 1; applyView(); return; }
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const n of graph.nodes) { const l = lay.get(n.id) || { w: 140, h: 80 }; x0 = Math.min(x0, n.x); y0 = Math.min(y0, n.y); x1 = Math.max(x1, n.x + l.w); y1 = Math.max(y1, n.y + l.h); }
    const pad = 24, s = Math.max(0.3, Math.min(1.2, (r.width - 2 * pad) / (x1 - x0), (r.height - 2 * pad) / (y1 - y0)));
    view.s = s; view.tx = (r.width - (x1 - x0) * s) / 2 - x0 * s; view.ty = (r.height - (y1 - y0) * s) / 2 - y0 * s;
    applyView();
  }

  btnAdd.onclick = openPalette;
  btnDel.onclick = removeSelection;
  btnMore.onclick = openMenu;
  btnFab.onclick = fit;
  btnIssues.onclick = openIssues;
  window.addEventListener("keydown", (e) => {
    if (!host.offsetParent) return;
    const a = document.activeElement;
    if (a && (a.tagName === "INPUT" || a.tagName === "TEXTAREA")) return;
    if ((e.key === "Delete" || e.key === "Backspace") && sel) { e.preventDefault(); removeSelection(); }
  });

  applyView();
  return {
    load(g, cat) { graph = g; catalog = cat; sel = null; pending = null; tempWire = null; if (!graph.variables) graph.variables = []; render(); fit(); },
    getGraph: () => graph,
    setCatalog(cat) { catalog = cat; render(); },
    setIssues(list) { issues = list || []; render(); },
    refit: () => fit(),
    addNode, removeSelection, connect, openPalette, openVars, openIssues, openMenu,
    // for tests
    _state: () => ({ view, sel, pending, lay }),
    _render: render,
    _setView(v) { Object.assign(view, v); applyView(); },
  };
}

root.MSGraphEditor = { create };
})(typeof window !== "undefined" ? window : globalThis);
