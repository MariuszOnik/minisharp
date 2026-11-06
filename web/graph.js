/* MiniSharp graphs: the node catalog and the graph -> code generator.
 *
 * Pure functions, no DOM (the editor will sit on top of this later); see docs/graphs-notes.md for the idea.
 *
 *   const catalog = MSGraph.buildCatalog(help, MSGraph.userNodes(files));   // help = parsed help.json (every API function is a node);
 *                                                      // files = [{name, text}]: every "static" method in the project's code is a node too
 *   const out = MSGraph.generate(graph, catalog);      // -> { code, warnings, errors, emitted }
 *   MSGraph.extractGraph(code)                         // the graph back out of a generated file
 *   MSGraph.searchCatalog(catalog, "rect")             // the palette's "fuzzy finder"
 *
 * Graph (plain JSON):
 *   { className: "Game", instance: false,                              // instance: true = a class to make objects from (no "static")
 *     order: 0,                                                        // [Order(n)]: its Start/Update/Draw run before higher numbers
 *     variables: [{ name: "x", type: "float", init: "200" }],          // become "static" fields (or instance fields)
 *     nodes: [{ id: "n1", type: "event.Update", x: 40, y: 40, data: {} }, ...],
 *     edges: [{ from: "n1", out: "exit", to: "n2", in: "enter" }, ...] }
 *   node.data: { name } for var.get / var.set, { value } for lit, { defaults: { port: "raw C# expression" } }
 *   for the value inputs that have no wire.
 *
 * Every graph (and every class) may have its own Main, Update and Draw: the program calls them all, files by name.
 *
 * Two kinds of ports (as in the notes):
 *   flow ports  (enter, exit, body, true, false) give the ORDER of the statements,
 *   value ports build EXPRESSIONS: a value node without flow is computed where it is used;
 *   the result of a node that the flow runs through (an API call with a result, the index of a For) is a local variable.
 *
 * The generated file is plain MiniSharp/C#; the graph itself is kept in the last line as "// @graph {json}". */
(function (root) {
"use strict";

const IDENT = /^[A-Za-z_]\w*$/;

// ---------------------------------------------------------------- the catalog
const flowIn = (name) => ({ name, flow: true });
const val = (name, type, def) => ({ name, type, def });

const BINARY = {            // id -> [symbol, precedence, default of the inputs]
  "op.add": ["+", 70, "0"], "op.sub": ["-", 70, "0"], "op.mul": ["*", 80, "1"], "op.div": ["/", 80, "1"], "op.mod": ["%", 80, "1"],
  "cmp.lt": ["<", 60, "0"], "cmp.le": ["<=", 60, "0"], "cmp.gt": [">", 60, "0"], "cmp.ge": [">=", 60, "0"],
  "cmp.eq": ["==", 50, "0"], "cmp.ne": ["!=", 50, "0"],
  "logic.and": ["&&", 40, "false"], "logic.or": ["||", 30, "false"],
};
const BINARY_TITLES = {
  "op.add": "Dodaj (+)", "op.sub": "Odejmij (-)", "op.mul": "Pomnóż (*)", "op.div": "Podziel (/)", "op.mod": "Reszta (%)",
  "cmp.lt": "Mniejsze (<)", "cmp.le": "Mniejsze lub równe (<=)", "cmp.gt": "Większe (>)", "cmp.ge": "Większe lub równe (>=)",
  "cmp.eq": "Równe (==)", "cmp.ne": "Różne (!=)", "logic.and": "I (&&)", "logic.or": "Lub (||)",
};

function defaultFor(type) {
  if (/^(int|float|double|long)$/.test(type)) return "0";
  if (type === "bool") return "false";
  if (type === "string" || type === "object") return '""';
  if (type === "char") return "' '";
  return "null";
}
function splitTop(s) {
  const out = []; let d = 0, cur = "";
  for (const c of s) {
    if ("<([".includes(c)) d++; else if (">)]".includes(c)) d--;
    if (c === "," && d === 0) { out.push(cur); cur = ""; } else cur += c;
  }
  if (cur.trim()) out.push(cur);
  return out.map((x) => x.trim());
}
function parseParams(text) {
  return splitTop(text).map((p) => { const w = p.split(/\s+/); return { type: w.slice(0, -1).join(" "), name: w[w.length - 1] }; });
}

function buildCatalog(help, userDefs) {
  const types = new Map();
  const add = (d) => { types.set(d.id, d); return d; };

  // events: the methods the program runs (one Main, one Update and one Draw per project) and the graph's own methods
  add({ id: "event.Main", title: "Start (Main)", category: "Zdarzenia", kind: "event", method: "Main", inputs: [], outputs: [flowIn("exit")],
    doc: "Wykonuje się raz na starcie. Każda klasa i każdy graf w projekcie może mieć własny Start: wykonują się po kolei." });
  add({ id: "event.Update", title: "Co klatkę (Update)", category: "Zdarzenia", kind: "event", method: "Update", inputs: [], outputs: [flowIn("exit")],
    doc: "Wykonuje się co klatkę. Każda klasa i każdy graf może mieć własny Update: wszystkie się wykonują, w kolejności plików w projekcie (po nazwie)." });
  add({ id: "event.Draw", title: "Rysowanie (Draw)", category: "Zdarzenia", kind: "event", method: "Draw", inputs: [], outputs: [flowIn("exit")],
    doc: "Wykonuje się co klatkę po Update, do rysowania. Każdy graf i klasa może mieć własny Draw; późniejszy plik rysuje na wierzchu." });
  add({ id: "event.Method", title: "Metoda (wołana z kodu)", category: "Zdarzenia", kind: "event", custom: true, inputs: [], outputs: [flowIn("exit")],
    doc: "Własna metoda grafu, bez parametrów, do wołania z kodu albo z innego grafu jako Klasa.Nazwa(), np. Graph.Tick(). Zmienne grafu to pola tej samej klasy." });

  // control
  add({ id: "if", title: "Jeśli (if)", category: "Sterowanie", kind: "if", inputs: [flowIn("enter"), val("cond", "bool", "false")], outputs: [flowIn("true"), flowIn("false"), flowIn("exit")] });
  add({ id: "for", title: "Powtórz (for)", category: "Sterowanie", kind: "for", inputs: [flowIn("enter"), val("first", "int", "0"), val("last", "int", "10"), val("step", "int", "1")], outputs: [flowIn("body"), flowIn("exit"), val("index", "int")] });
  add({ id: "while", title: "Dopóki (while)", category: "Sterowanie", kind: "while", inputs: [flowIn("enter"), val("cond", "bool", "false")], outputs: [flowIn("body"), flowIn("exit")] });

  // variables and literals
  add({ id: "var.set", title: "Ustaw zmienną", category: "Zmienne", kind: "setvar", inputs: [flowIn("enter"), val("value", "", "0")], outputs: [flowIn("exit")] });
  add({ id: "var.get", title: "Pobierz zmienną", category: "Zmienne", kind: "getvar", inputs: [], outputs: [val("value", "")] });
  add({ id: "lit", title: "Wartość", category: "Zmienne", kind: "lit", inputs: [], outputs: [val("value", "")] });

  // operators
  for (const [id, [sym, , def]] of Object.entries(BINARY)) {
    const logic = id.startsWith("logic."), cmp = id.startsWith("cmp.");
    add({ id, title: BINARY_TITLES[id], category: logic ? "Logika" : cmp ? "Porównania" : "Matematyka", kind: "binary", op: id,
      inputs: [val("a", "", def), val("b", "", def)], outputs: [val("result", cmp || logic ? "bool" : "")] });
  }
  add({ id: "logic.not", title: "Nie (!)", category: "Logika", kind: "unary", symbol: "!", inputs: [val("a", "bool", "false")], outputs: [val("result", "bool")] });
  add({ id: "op.neg", title: "Przeciwna (-)", category: "Matematyka", kind: "unary", symbol: "-", inputs: [val("a", "", "0")], outputs: [val("result", "")] });

  // the API: every function, constant and constructor of help.json is a node
  if (help) {
    const fns = new Map();                                            // "Group.Name" -> { paramCount -> entry }
    for (const e of help.entries) {
      let m;
      if ((m = /^const (\S+) (\w+) = /.exec(e.sig))) {
        add({ id: `api.${e.group}.${e.name}`, title: `${e.group}.${e.name}`, category: e.group, kind: "const", callee: e.insert, sig: e.sig, doc: e.doc,
          inputs: [], outputs: [val("value", m[1])] });
      } else if ((m = /^new (\w+)\((.*)\)$/.exec(e.sig))) {
        const params = parseParams(m[2]);
        add({ id: `api.new.${m[1]}`, title: `new ${m[1]}`, category: e.group, kind: "new", callee: `new ${m[1]}`, sig: e.sig, doc: e.doc,
          params, inputs: params.map((p) => val(p.name, p.type, defaultFor(p.type))), outputs: [val("value", m[1])] });
      } else if ((m = /^(\S+)\s+(\w+)\((.*)\)\s*$/.exec(e.sig))) {
        const key = `${e.group}.${e.name}`;
        if (!fns.has(key)) fns.set(key, new Map());
        const params = parseParams(m[3]);
        if (!fns.get(key).has(params.length)) fns.get(key).set(params.length, { e, ret: m[1], params });
      }
    }
    for (const [key, byCount] of fns) {
      for (const [count, { e, ret, params }] of byCount) {
        const many = byCount.size > 1;
        add({
          id: `api.${key}` + (many ? `/${count}` : ""), title: key + (many ? ` (${count})` : ""), category: e.group, kind: "call",
          callee: e.insert.replace(/\($/, ""), ret, params, sig: e.sig, doc: e.doc,
          inputs: [flowIn("enter"), ...params.map((p) => val(p.name, p.type, defaultFor(p.type)))],
          outputs: [flowIn("exit"), ...(ret === "void" ? [] : [val("result", ret)])],
        });
      }
    }
  }
  for (const d of userDefs || []) if (!types.has(d.id)) types.set(d.id, d);
  return { types, list: [...types.values()] };
}

/* Nodes from the project's own code: every "static" method of a class in a file that is not itself a graph file
 * (Main, Update and Draw are the program's entry points and are skipped).  The lines of // comments right above the
 * method are its description; "// @node Title" gives the node another title, "// @node hide" keeps a helper out of the palette.
 *   files: [{ name, text }] */
function userNodes(files) {
  const MSC = root.MSComplete || (typeof require !== "undefined" ? require("./complete.js") : null);
  if (!MSC) return [];
  const RESERVED = new Set(["enter", "exit", "result", "value"]);
  const defs = [];
  for (const f of files) {
    if (/^\/\/ @graph /m.test(f.text)) continue;                    // generated: its classes only hold the events
    let classes;
    try { classes = MSC.parseClasses(f.text); } catch (e) { continue; }
    for (const c of classes) {
      if (c.kind !== "class") continue;
      for (const m of c.members) {
        if (m.kind !== "method" || !m.isStatic || ["Main", "Update", "Draw"].includes(m.name)) continue;
        let title = `${c.name}.${m.name}`, hide = false;
        const doc = [];
        for (const line of m.docLines || []) {
          const t = /^@node\b\s*(.*)$/.exec(line);
          if (!t) { doc.push(line); continue; }
          if (/^hide$/i.test(t[1].trim())) hide = true; else if (t[1].trim()) title = t[1].trim();
        }
        if (hide) continue;
        const params = parseParams(m.params || "").map((p) => ({ type: p.type, name: p.name, port: RESERVED.has(p.name) ? p.name + "_" : p.name }));
        defs.push({
          id: `user.${c.name}.${m.name}`, title, category: `Kod: ${c.name}`, kind: "call", user: true, file: f.name,
          callee: `${c.name}.${m.name}`, ret: m.type, params, sig: `${m.type} ${m.name}(${(m.params || "").trim()})`, doc: doc.join(" "),
          inputs: [flowIn("enter"), ...params.map((p) => val(p.port, p.type, defaultFor(p.type)))],
          outputs: [flowIn("exit"), ...(m.type === "void" ? [] : [val("result", m.type)])],
        });
      }
    }
  }
  return defs;
}

function searchCatalog(catalog, query, limit) {
  const q = query.trim().toLowerCase();
  if (!q) return [...catalog.list.filter((d) => d.user), ...catalog.list.filter((d) => !d.user)].slice(0, limit || 50);
  const words = q.split(/\s+/);
  const scored = [];
  for (const d of catalog.list) {
    const title = d.title.toLowerCase(), hay = `${title} ${d.category.toLowerCase()} ${d.id.toLowerCase()} ${(d.doc || "").toLowerCase()}`;
    if (!words.every((w) => hay.includes(w))) continue;
    const s = title === q ? 0 : title.startsWith(q) ? 1 : title.split(/[.\s(]/).some((p) => p.startsWith(q)) ? 2 : words.every((w) => title.includes(w)) ? 3 : 4;
    scored.push([s, d.title.length, d]);
  }
  scored.sort((a, b) => a[0] - b[0] || (b[2].user ? 1 : 0) - (a[2].user ? 1 : 0) || a[1] - b[1] || a[2].title.localeCompare(b[2].title));
  return scored.slice(0, limit || 50).map((x) => x[2]);
}

// ---------------------------------------------------------------- graph helpers
const emptyGraph = () => ({ className: "Game", variables: [], nodes: [], edges: [] });
function nextId(graph) {
  let n = 1;
  const ids = new Set(graph.nodes.map((x) => x.id));
  while (ids.has("n" + n)) n++;
  return "n" + n;
}
function portOf(def, dir, name) {
  return (dir === "in" ? def.inputs : def.outputs).find((p) => p.name === name) || null;
}

/* problems that make a graph unusable: unknown node types, missing ports, flow wired to a value, ... */
function validate(graph, catalog) {
  const issues = [];
  const ids = new Set();
  for (const n of graph.nodes) {
    if (ids.has(n.id)) issues.push({ node: n.id, message: `powtórzony identyfikator węzła ${n.id}` });
    ids.add(n.id);
    if (!catalog.types.has(n.type)) issues.push({ node: n.id, message: `nieznany typ węzła „${n.type}”` });
  }
  const byId = new Map(graph.nodes.map((n) => [n.id, n]));
  const seenIn = new Set(), seenOut = new Set();
  for (const e of graph.edges) {
    const a = byId.get(e.from), b = byId.get(e.to);
    if (!a || !b) { issues.push({ node: (a ? e.to : e.from), message: "połączenie do nieistniejącego węzła" }); continue; }
    const da = catalog.types.get(a.type), db = catalog.types.get(b.type);
    if (!da || !db) continue;
    const po = portOf(da, "out", e.out), pi = portOf(db, "in", e.in);
    if (!po) { issues.push({ node: a.id, message: `węzeł ${da.title} nie ma wyjścia „${e.out}”` }); continue; }
    if (!pi) { issues.push({ node: b.id, message: `węzeł ${db.title} nie ma wejścia „${e.in}”` }); continue; }
    if (!!po.flow !== !!pi.flow) issues.push({ node: b.id, message: `połączono przepływ z wartością (${da.title}.${e.out} → ${db.title}.${e.in})` });
    if (!pi.flow) { const k = e.to + "/" + e.in; if (seenIn.has(k)) issues.push({ node: b.id, message: `wejście „${e.in}” węzła ${db.title} ma kilka połączeń` }); seenIn.add(k); }
    if (po.flow) { const k = e.from + "/" + e.out; if (seenOut.has(k)) issues.push({ node: a.id, message: `wyjście „${e.out}” węzła ${da.title} ma kilka połączeń` }); seenOut.add(k); }
  }
  return issues;
}

// ---------------------------------------------------------------- expressions: precedence and raw text
/* precedence of a raw expression typed into a node: 100 simple, 90 a leading minus, 0 anything with operators */
function rawPrec(raw) {
  let depth = 0, quote = "";
  for (let i = 0; i < raw.length; i++) {
    const c = raw[i];
    if (quote) { if (c === "\\") i++; else if (c === quote) quote = ""; continue; }
    if (c === '"' || c === "'") { quote = c; continue; }
    if (c === "(" || c === "[") { depth++; continue; }
    if (c === ")" || c === "]") { depth--; continue; }
    if (depth === 0 && /[\s+\-*/%<>=!&|?:,]/.test(c) && !(c === "-" && i === 0)) return 0;
  }
  return raw[0] === "-" ? 90 : 100;
}
const wrapLeft = (x, p) => (x.prec < p ? `(${x.code})` : x.code);
const wrapRight = (x, p) => (x.prec <= p ? `(${x.code})` : x.code);

// ---------------------------------------------------------------- the generator
function generate(graph, catalog, opts) {
  opts = opts || {};
  const warnings = [], errors = [];
  const warn = (node, message) => warnings.push({ node: node || null, message });
  const fail = (node, message) => errors.push({ node: node || null, message });

  for (const i of validate(graph, catalog)) fail(i.node, i.message);

  const nodes = new Map(graph.nodes.map((n) => [n.id, n]));
  const outE = new Map(), inE = new Map();
  for (const e of graph.edges) {
    const ko = e.from + "/" + e.out, ki = e.to + "/" + e.in;
    if (!outE.has(ko)) outE.set(ko, []);
    outE.get(ko).push(e);
    if (!inE.has(ki)) inE.set(ki, []);
    inE.get(ki).push(e);
  }
  const defOf = (n) => catalog.types.get(n.type);
  const className = IDENT.test(graph.className || "") ? graph.className : "Game";
  const inst = !!graph.instance;                     // a class to make objects from: fields and methods without "static"
  const stat = inst ? "" : "static ";
  if (graph.className && !IDENT.test(graph.className)) fail(null, `niepoprawna nazwa klasy „${graph.className}”`);

  // per method state
  let lines, locals, usedNames, scopeStack, scopeCount, onPath, curMethod;
  const emitted = new Set();
  const put = (depth, text) => lines.push("    ".repeat(depth) + text);
  const fresh = (base) => {
    base = (base || "v").replace(/[^A-Za-z0-9_]/g, "");
    base = base ? base[0].toLowerCase() + base.slice(1) : "v";
    let name = base, k = 2;
    while (usedNames.has(name)) name = base + k++;
    usedNames.add(name);
    return name;
  };
  const withScope = (fn) => { scopeStack.push(++scopeCount); try { fn(); } finally { scopeStack.pop(); } };

  const flowNext = (id, port) => {
    const es = outE.get(id + "/" + port) || [];
    return es.length ? es[0].to : null;
  };
  const rawDefault = (n, d, port) => {
    const p = portOf(d, "in", port);
    const given = n.data && n.data.defaults && n.data.defaults[port];
    return given !== undefined && given !== "" ? String(given) : (p && p.def !== undefined ? p.def : "0");
  };
  const exprInput = (n, d, port, stack) => {
    const es = inE.get(n.id + "/" + port) || [];
    if (es.length) return exprOf(es[0].from, es[0].out, stack);
    const raw = rawDefault(n, d, port);
    return { code: raw, prec: rawPrec(raw) };
  };
  const callArgs = (n, d, stack) => d.params.map((p) => exprInput(n, d, p.port || p.name, stack).code);

  function exprOf(id, port, stack) {
    const n = nodes.get(id), bad = { code: "0", prec: 100 };
    if (!n) return bad;
    const d = defOf(n);
    if (!d) return bad;
    const loc = locals.get(id + "/" + port);
    if (loc) {
      if (!scopeStack.includes(loc.scope)) warn(id, `wartość z węzła „${d.title}” jest użyta poza blokiem, w którym powstała`);
      return { code: loc.name, prec: 100 };
    }
    if (stack.has(id)) { fail(id, `cykl w połączeniach wartości (węzeł „${d.title}”)`); return bad; }
    stack.add(id);
    try {
      switch (d.kind) {
        case "lit": { const raw = String((n.data && n.data.value) !== undefined ? n.data.value : "0"); return { code: raw, prec: rawPrec(raw) }; }
        case "getvar": {
          const name = n.data && n.data.name;
          if (!IDENT.test(name || "")) { fail(id, "„Pobierz zmienną” bez poprawnej nazwy"); return bad; }
          if (!(graph.variables || []).some((v) => v.name === name)) warn(id, `zmienna „${name}” nie jest zadeklarowana w grafie`);
          return { code: name, prec: 100 };
        }
        case "const": return { code: d.callee, prec: 100 };
        case "new": return { code: `${d.callee}(${callArgs(n, d, stack).join(", ")})`, prec: 100 };
        case "binary": {
          const [sym, p] = BINARY[d.op];
          const a = exprInput(n, d, "a", stack), b = exprInput(n, d, "b", stack);
          return { code: `${wrapLeft(a, p)} ${sym} ${wrapRight(b, p)}`, prec: p };
        }
        case "unary": {
          const a = exprInput(n, d, "a", stack);
          return { code: d.symbol + (a.prec < 90 ? `(${a.code})` : a.code), prec: 90 };
        }
        case "call": {
          if (port !== "result") { fail(id, `„${d.title}” nie ma wartości „${port}”`); return bad; }
          if (((inE.get(id + "/enter") || []).length)) warn(id, `wartość z „${d.title}” jest użyta, zanim przepływ do niego dotarł`);
          return { code: `${d.callee}(${callArgs(n, d, stack).join(", ")})`, prec: 100 };
        }
        case "for": warn(id, "indeks pętli For użyty poza pętlą"); return { code: "i", prec: 100 };
        default: fail(id, `węzeł „${d.title}” nie ma wartości do użycia w wyrażeniu`); return bad;
      }
    } finally { stack.delete(id); }
  }

  const negate = (x) => (x.prec >= 90 ? "!" + x.code : `!(${x.code})`);
  const hasResultUse = (id) => (outE.get(id + "/result") || []).length > 0;

  function chainFrom(id, port, depth) {
    let cur = flowNext(id, port);
    const added = [];
    while (cur) {
      if (onPath.has(cur)) { warn(cur, "pętla w przepływie: użyj węzła For albo While"); break; }
      onPath.add(cur); added.push(cur);
      cur = emitNode(cur, depth);
    }
    for (const a of added) onPath.delete(a);
  }

  const LOOP_NAMES = ["i", "j", "k", "l", "m", "n"];
  function emitNode(id, depth) {
    const n = nodes.get(id);
    if (!n) return null;
    const d = defOf(n);
    if (!d) return null;
    emitted.add(id);
    const stack = new Set();
    switch (d.kind) {
      case "call": {
        // drawing outside Draw is wiped: Draw starts by clearing the screen after Update
        if ((curMethod === "Update" || curMethod === "Main") && /^Gfx\./.test(d.callee) && d.ret === "void" && !/^Gfx\.(Screen|Load|Background)/.test(d.callee))
          warn(id, `„${d.title}” rysuje w ${curMethod === "Main" ? "Start" : "Update"}, a ekran rysuje się w Draw (zwykle zaczyna od Gfx.Clear), więc tego nie zobaczysz: podłącz ten węzeł do zdarzenia Draw`);
        const call = `${d.callee}(${callArgs(n, d, stack).join(", ")})`;
        if (d.ret !== "void" && hasResultUse(id)) {
          const name = fresh(d.callee.split(".").pop());
          put(depth, `var ${name} = ${call};`);
          locals.set(id + "/result", { name, scope: scopeStack[scopeStack.length - 1] });
        } else put(depth, call + ";");
        return flowNext(id, "exit");
      }
      case "setvar": {
        const name = n.data && n.data.name;
        if (!IDENT.test(name || "")) { fail(id, "„Ustaw zmienną” bez poprawnej nazwy"); return flowNext(id, "exit"); }
        if (!(graph.variables || []).some((v) => v.name === name)) warn(id, `zmienna „${name}” nie jest zadeklarowana w grafie`);
        // x = x - 3  ->  x -= 3
        const ve = (inE.get(id + "/value") || [])[0];
        const src = ve && nodes.get(ve.from), sd = src && defOf(src);
        if (sd && sd.kind === "binary" && ["op.add", "op.sub", "op.mul", "op.div"].includes(sd.op) && !locals.has(ve.from + "/" + ve.out)) {
          const ae = (inE.get(src.id + "/a") || [])[0], an = ae && nodes.get(ae.from);
          if (an && defOf(an) && defOf(an).kind === "getvar" && an.data && an.data.name === name) {
            const b = exprInput(src, sd, "b", new Set([src.id]));
            put(depth, `${name} ${BINARY[sd.op][0]}= ${b.code};`);
            return flowNext(id, "exit");
          }
        }
        put(depth, `${name} = ${exprInput(n, d, "value", stack).code};`);
        return flowNext(id, "exit");
      }
      case "if": {
        const cond = exprInput(n, d, "cond", stack);
        const t = flowNext(id, "true"), f = flowNext(id, "false");
        if (t) {
          put(depth, `if (${cond.code}) {`);
          withScope(() => chainFrom(id, "true", depth + 1));
          if (f) { put(depth, "} else {"); withScope(() => chainFrom(id, "false", depth + 1)); }
          put(depth, "}");
        } else if (f) {
          put(depth, `if (${negate(cond)}) {`);
          withScope(() => chainFrom(id, "false", depth + 1));
          put(depth, "}");
        } else warn(id, "„Jeśli” bez żadnego połączonego wyjścia");
        return flowNext(id, "exit");
      }
      case "for": {
        const first = exprInput(n, d, "first", stack), last = exprInput(n, d, "last", stack), step = exprInput(n, d, "step", stack);
        const active = new Set([...locals.values()].filter((l) => scopeStack.includes(l.scope)).map((l) => l.name));
        const v = LOOP_NAMES.find((x) => !active.has(x)) || fresh("i");
        const down = step.code.startsWith("-");
        const inc = step.code === "1" ? `${v}++` : step.code === "-1" ? `${v}--` : `${v} += ${step.code}`;
        put(depth, `for (int ${v} = ${first.code}; ${v} ${down ? ">" : "<"} ${wrapRight(last, 60)}; ${inc}) {`);
        withScope(() => {
          locals.set(id + "/index", { name: v, scope: scopeStack[scopeStack.length - 1] });
          chainFrom(id, "body", depth + 1);
        });
        put(depth, "}");
        return flowNext(id, "exit");
      }
      case "while": {
        put(depth, `while (${exprInput(n, d, "cond", stack).code}) {`);
        withScope(() => chainFrom(id, "body", depth + 1));
        put(depth, "}");
        return flowNext(id, "exit");
      }
      default:
        warn(id, `węzeł „${d.title}” nie może stać w przepływie`);
        return null;
    }
  }

  // methods: every event node starts one; several nodes of the same event are run one after another
  const events = graph.nodes.filter((n) => defOf(n) && defOf(n).kind === "event").sort((a, b) => (a.y || 0) - (b.y || 0) || (a.x || 0) - (b.x || 0));
  const ENTRY = ["Main", "Update", "Draw"];
  const methods = new Map();
  for (const ev of events) {
    const d = defOf(ev);
    let m = d.method;
    if (inst && !d.custom) { fail(ev.id, `w klasie z obiektami nie działa „${d.title}” (Main, Update i Draw są statyczne): użyj węzła „Metoda” i wywołaj ją z kodu`); continue; }
    if (d.custom) {
      m = ev.data && ev.data.name;
      if (!IDENT.test(m || "")) { fail(ev.id, "„Metoda” bez poprawnej nazwy"); continue; }
      if (ENTRY.includes(m)) { fail(ev.id, `„${m}” to wejście programu: użyj węzła Start, Update albo Draw`); continue; }
      if ((graph.variables || []).some((v) => v.name === m)) { fail(ev.id, `„${m}” jest też nazwą zmiennej grafu`); continue; }
    }
    if (!methods.has(m)) methods.set(m, []);
    methods.get(m).push(ev.id);
  }
  const body = [];
  const order = [...ENTRY.filter((m) => methods.has(m)), ...[...methods.keys()].filter((m) => !ENTRY.includes(m)).sort()];
  for (const m of order) {
    lines = []; locals = new Map(); usedNames = new Set(); scopeStack = [0]; scopeCount = 0; onPath = new Set(); curMethod = m;
    for (const evId of methods.get(m)) { emitted.add(evId); chainFrom(evId, "exit", 2); }
    if (body.length) body.push("");
    if (!ENTRY.includes(m)) body.push(`    // Do wywołania z kodu: ${inst ? `var o = new ${className}(); o.${m}();` : `${className}.${m}();`}`);
    body.push(`    ${stat}void ${m}() {`, ...lines, "    }");
  }
  if (!events.length) warn(null, "graf nie ma żadnego węzła zdarzenia (Start, Update, Draw albo Metoda), więc nic się nie wykona");

  // flow nodes that no event leads to
  for (const n of graph.nodes) {
    const d = defOf(n);
    if (!d || emitted.has(n.id)) continue;
    const isFlow = ["setvar", "if", "for", "while"].includes(d.kind) || (d.kind === "call" && d.ret === "void");
    if (isFlow) warn(n.id, `węzeł „${d.title}” nie jest połączony z żadnym zdarzeniem, więc nie trafi do kodu`);
  }

  const fields = [];
  for (const v of graph.variables || []) {
    if (!IDENT.test(v.name || "")) { fail(null, `niepoprawna nazwa zmiennej „${v.name}”`); continue; }
    fields.push(`    ${stat}${v.type || "float"} ${v.name}${v.init !== undefined && v.init !== "" ? " = " + v.init : ""};`);
  }
  const out = ["// Wygenerowano z grafu. Zmiany w tym pliku nie wrócą do grafu, edytuj go w edytorze grafów."];
  if (Number.isInteger(graph.order) && graph.order) out.push(`[Order(${graph.order})]`);   // like DefaultExecutionOrder in Unity
  out.push(`class ${className} {`);
  if (fields.length) out.push(...fields, "");
  out.push(...body, "}");
  let code = out.join("\n") + "\n";
  if (opts.embed !== false) code += "\n" + serialize(graph) + "\n";
  return { code, warnings, errors, emitted };
}

// ---------------------------------------------------------------- the graph inside the generated file
function serialize(graph) {
  return "// @graph " + JSON.stringify(graph).replace(/\u2028/g, "\\u2028").replace(/\u2029/g, "\\u2029");
}
function extractGraph(code) {
  const m = /^\/\/ @graph (.+)$/m.exec(code);
  if (!m) return null;
  try {
    const g = JSON.parse(m[1]);
    return g && Array.isArray(g.nodes) && Array.isArray(g.edges) ? g : null;
  } catch (e) { return null; }
}

// ---------------------------------------------------------------- a ready graph: the ball from the first program
function sampleBall() {
  const g = emptyGraph();
  g.variables = [{ name: "x", type: "float", init: "200" }, { name: "y", type: "float", init: "120" }];
  const N = (id, type, x, y, data) => g.nodes.push({ id, type, x, y, data: data || {} });
  const F = (from, out, to, inp) => g.edges.push({ from, out, to, in: inp });
  // three columns: values on the left, the flow in the middle, the results on the right
  N("upd", "event.Update", 0, 0);
  const dirs = [["Left", "x", "op.sub"], ["Right", "x", "op.add"], ["Up", "y", "op.sub"], ["Down", "y", "op.add"]];
  let prev = ["upd", "exit"];
  dirs.forEach(([btn, v, op], i) => {
    const k = i + 1, y = 100 + i * 230;
    N("held" + k, "api.Input.Held", 0, y, { defaults: { button: "Btn." + btn } });
    N("if" + k, "if", 250, y);
    N("get" + k, "var.get", 0, y + 110, { name: v });
    N("op" + k, op, 250, y + 130, { defaults: { b: "3" } });
    N("set" + k, "var.set", 500, y, { name: v });
    F(...prev, "if" + k, "enter");
    F("held" + k, "result", "if" + k, "cond");
    F("if" + k, "true", "set" + k, "enter");
    F("get" + k, "value", "op" + k, "a");
    F("op" + k, "result", "set" + k, "value");
    prev = ["if" + k, "exit"];
  });
  const yt = 100 + 4 * 230;
  N("touching", "api.Input.Touching", 0, yt);
  N("touch", "if", 250, yt);
  N("setx", "var.set", 500, yt, { name: "x" });
  N("sety", "var.set", 500, yt + 120, { name: "y" });
  N("tx", "api.Input.TouchX", 0, yt + 110);
  N("mul", "op.mul", 250, yt + 130, { defaults: { b: "400" } });
  N("div", "op.div", 250, yt + 230, { defaults: { b: "320" } });
  N("ty", "api.Input.TouchY", 0, yt + 220);
  F(...prev, "touch", "enter"); F("touching", "result", "touch", "cond");
  F("touch", "true", "setx", "enter"); F("setx", "exit", "sety", "enter");
  F("tx", "result", "mul", "a"); F("mul", "result", "div", "a"); F("div", "result", "setx", "value"); F("ty", "result", "sety", "value");

  const yd = yt + 380;
  N("draw", "event.Draw", 0, yd);
  N("rgb", "api.Gfx.Rgb", 0, yd + 90, { defaults: { r: "20", g: "24", b: "40" } });
  N("clear", "api.Gfx.Clear", 250, yd);
  N("circle", "api.Gfx.Circle", 500, yd, { defaults: { r: "14" } });
  N("gx", "var.get", 250, yd + 110, { name: "x" });
  N("gy", "var.get", 250, yd + 200, { name: "y" });
  N("yellow", "api.Color.Yellow", 250, yd + 290);
  F("draw", "exit", "clear", "enter"); F("rgb", "result", "clear", "color");
  F("clear", "exit", "circle", "enter"); F("gx", "value", "circle", "x"); F("gy", "value", "circle", "y"); F("yellow", "value", "circle", "color");
  return g;
}

root.MSGraph = { buildCatalog, userNodes, searchCatalog, generate, validate, extractGraph, serialize, emptyGraph, nextId, sampleBall, rawPrec };
if (typeof module !== "undefined" && module.exports) module.exports = root.MSGraph;
})(typeof window !== "undefined" ? window : globalThis);
