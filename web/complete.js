/* MiniSharp editor: code completion ("IntelliSense").
 *
 * Pure functions, no DOM: web/app.js wires them to the editor and the popup.
 *   MSComplete.analyze({ text, caret, others, help, force })
 *     text    the open file; caret = cursor position in it
 *     others  texts of the project's other files (so classes from there are known)
 *     help    the parsed help.json (API classes, functions and constants), or null
 *     force   true for Ctrl+Space: also suggest after 0 or 1 typed letters
 *   returns { from, to, items: [{ label, kind, insert, detail, doc }] } or null.
 *
 * It understands classes, interfaces, their members and base classes, declared variable types,
 * "var x = new T()", foreach variables and lambda parameters of List methods, "this." and "base.".
 * It does not run the real compiler, so it is a helper and can be wrong on unusual code. */
(function (root) {
"use strict";

// shared with the highlighter in app.js
const lists = {
  keywords: ("abstract as base break case class const continue default do else for foreach if in interface " +
    "internal is namespace new not override private protected public readonly return sealed static switch this virtual while yield out").split(" "),
  literals: ["true", "false", "null"],
  types: "int float double long bool string char void var object List Dictionary Action Func Predicate Comparison IEnumerator Coroutine".split(" "),
  api: "Gfx Audio Input Btn Color Screen Time App Console Math Mathf Random WaitForSeconds WaitForSecondsRealtime WaitForEndOfFrame".split(" "),
};
const KW = new Set(lists.keywords.concat(lists.literals));
const TYPEKW = new Set(["int", "float", "double", "long", "bool", "string", "char", "void", "var", "object"]);
const MODS = new Set("public private protected internal static virtual override abstract sealed readonly const new".split(" "));
const TYPE = "[A-Za-z_][\\w.]*(?:<[^()=;{}]*>)?(?:\\[\\])*\\??";
const LIST_LAMBDAS = "Find|FindAll|FindLast|FindIndex|Exists|TrueForAll|RemoveAll|ForEach|Sort";

// ---------------------------------------------------------------- text helpers
const MASK = /\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$)|"(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])*'?/g;
const mask = (t) => t.replace(MASK, (m) => m.replace(/[^\n]/g, " "));   // comments and strings become spaces

function inCommentOrString(t, caret) {
  MASK.lastIndex = 0;
  let m;
  while ((m = MASK.exec(t))) {
    const s = m.index, e = s + m[0].length, x = m[0], c = x[0];
    if (s >= caret) return false;
    if (caret < e) return true;
    if (caret > e) continue;                                           // the token ended before the caret
    if (c === "/") { if (x[1] === "/" || !x.endsWith("*/") || x.length < 4) return true; }    // caret == e: after the token
    else if (!(x.length >= 2 && x[x.length - 1] === c && !/\\$/.test(x.slice(0, -1)))) return true;   // unterminated string
  }
  return false;
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
const baseName = (t) => t.replace(/<[\s\S]*$/, "").replace(/(\[\])+$/, "").replace(/\?$/, "").trim();
function elemType(t, forEach) {
  t = (t || "").trim();
  if (/\[\]$/.test(t)) return t.replace(/\[\]$/, "");
  let m = /^List\s*<([\s\S]+)>$/.exec(t);
  if (m) return m[1].trim();
  m = /^Dictionary\s*<([\s\S]+)>$/.exec(t);
  if (m && !forEach) return splitTop(m[1])[1] || null;
  return null;
}

// ---------------------------------------------------------------- classes of the project
// the comment lines (// or ///) right above a position in the text, without the slashes and xml tags
function docAbove(text, idx) {
  const lines = [];
  let end = text.lastIndexOf("\n", idx - 1) + 1;
  while (end > 0) {
    const prevEnd = end - 1, ls = text.lastIndexOf("\n", prevEnd - 1) + 1;
    const line = text.slice(ls, prevEnd).trim();
    if (line.startsWith("[")) { end = ls; continue; }          // an attribute line: look above it
    if (!line.startsWith("//")) break;
    lines.unshift(line.replace(/^\/\/\/?\s?/, "").replace(/<\/?\w+>/g, "").trim());
    end = ls;
  }
  return lines.filter(Boolean);
}
function parseMembers(body, cls, text, bodyStart) {
  // keep only the top level of the class: the insides of { } (method bodies, initializers) are blanked
  let flat = "", depth = 0;
  for (const c of body) {
    if (c === "{") { if (depth++ === 0) flat += ";"; else flat += " "; }
    else if (c === "}") { depth--; flat += " "; }
    else flat += depth === 0 ? c : (c === "\n" ? "\n" : " ");
  }
  const members = [];
  let pos = 0;
  for (const piece of flat.split(";")) {
    const at = bodyStart + pos + Math.max(0, piece.search(/\S/));
    pos += piece.length + 1;
    const chunk = piece.replace(/\s+/g, " ").trim();
    if (!chunk) continue;
    const docLines = text ? docAbove(text, at) : [];
    const mods = [], attrs = [];
    let rest = chunk, mm;
    while ((mm = /^\[([^\]]*)\]\s*/.exec(rest))) {           // [In], [Out, In], [Order(2)]: names only
      for (const a of mm[1].split(",")) attrs.push(a.trim().replace(/\s*\(.*$/, ""));
      rest = rest.slice(mm[0].length);
    }
    while ((mm = /^(\w+)\s+/.exec(rest)) && MODS.has(mm[1])) { mods.push(mm[1]); rest = rest.slice(mm[0].length); }
    const isStatic = mods.includes("static") || mods.includes("const");
    let m = new RegExp("^(" + TYPE + ")\\s+([A-Za-z_]\\w*)\\s*\\((.*?)\\)").exec(rest);
    if (m && !(KW.has(m[1]) && !TYPEKW.has(m[1]))) {
      members.push({ name: m[2], kind: "method", type: m[1], params: m[3], isStatic, cls, docLines, attrs });
      continue;
    }
    if (new RegExp("^" + cls + "\\s*\\(").test(rest)) continue;            // constructor
    m = new RegExp("^(" + TYPE + ")\\s+(.+)$").exec(rest);
    if (m && !(KW.has(m[1]) && !TYPEKW.has(m[1]))) {
      for (const part of splitTop(m[2])) {
        const n = /^([A-Za-z_]\w*)\s*(?:=|$)/.exec(part);
        if (n) members.push({ name: n[1], kind: "field", type: m[1], params: "", isStatic, cls, docLines, readOnly: mods.includes("const") || mods.includes("readonly"), attrs });
      }
    }
  }
  return members;
}
function parseClasses(text) {
  const masked = mask(text), out = [];
  const re = /\b(class|interface)\s+([A-Za-z_]\w*)\s*(?:<[^>{]*>)?\s*(?::\s*([^{;]*))?\{/g;
  let m;
  while ((m = re.exec(masked))) {
    const bodyStart = m.index + m[0].length;
    let depth = 1, i = bodyStart;
    for (; i < masked.length && depth; i++) { if (masked[i] === "{") depth++; else if (masked[i] === "}") depth--; }
    const bodyEnd = depth ? masked.length : i - 1;
    const before = masked.slice(0, m.index);
    const head = before.slice(Math.max(before.lastIndexOf(";"), before.lastIndexOf("{"), before.lastIndexOf("}")) + 1);
    out.push({
      name: m[2], kind: m[1], isAbstract: /\babstract\b/.test(head), docLines: docAbove(text, m.index),
      bases: (m[3] || "").split(",").map((s) => baseName(s)).filter(Boolean),
      bodyStart, bodyEnd, members: parseMembers(masked.slice(bodyStart, bodyEnd), m[2], text, bodyStart),
    });
    re.lastIndex = bodyEnd;
  }
  return out;
}

// ---------------------------------------------------------------- built-in members (string, List, ...)
const BUILTIN = {
  string: "Length Substring( IndexOf( Contains( StartsWith( EndsWith( ToUpper() ToLower() Trim() Replace( Split( ToString()",
  List: "Count Add( AddRange( Insert( Remove( RemoveAt( Clear() Contains( IndexOf( Reverse() Sort( Find( FindLast( FindIndex( FindAll( Exists( TrueForAll( RemoveAll( ForEach(",
  Dictionary: "Count Keys Values Add( Remove( ContainsKey( ContainsValue( Clear() TryGetValue(",
  array: "Length",
  delegate: "Invoke(",
};
const BUILTIN_STATIC = { string: "IsNullOrEmpty(", int: "Parse(", float: "Parse(", double: "Parse(" };
function builtinItems(spec) {
  return spec.split(" ").map((n) => {
    const isCall = n.endsWith("(") || n.endsWith("()");
    return { label: n.replace(/\(\)?$/, ""), kind: isCall ? "method" : "field", insert: n, detail: isCall ? n.replace(/\($/, "(…)") : n, doc: "" };
  });
}
function builtinKey(type) {
  if (/\[\]$/.test(type.trim())) return "array";
  const b = baseName(type);
  if (["Func", "Action", "Predicate", "Comparison"].includes(b)) return "delegate";
  return BUILTIN[b] ? b : null;
}

// ---------------------------------------------------------------- the API from help.json
const apiCache = new WeakMap();
function apiIndex(help) {
  if (!help) return null;
  let ix = apiCache.get(help);
  if (ix) return ix;
  const byGroup = new Map();
  for (const e of help.entries) { if (!byGroup.has(e.group)) byGroup.set(e.group, []); byGroup.get(e.group).push(e); }
  ix = { byGroup, groups: help.groups.filter((g) => g.api) };
  apiCache.set(help, ix);
  return ix;
}
function apiMembers(ix, group) {            // "Gfx." -> its functions and constants
  const byName = new Map();
  for (const e of ix.byGroup.get(group) || []) {
    if (e.sig.startsWith("new ")) continue;
    if (!byName.has(e.name)) byName.set(e.name, []);
    byName.get(e.name).push(e);
  }
  const items = [];
  for (const [name, es] of byName) {
    const e = es[0], isConst = e.sig.startsWith("const ");
    const noParams = es.every((x) => /\(\s*\)\s*$/.test(x.sig));
    items.push({
      label: name, kind: isConst ? "const" : "method",
      insert: isConst ? name : name + (noParams ? "()" : "("),
      detail: e.sig + (es.length > 1 ? `  (+${es.length - 1})` : ""), doc: e.doc,
    });
  }
  return items;
}

// ---------------------------------------------------------------- analysis
function analyze(o) {
  const text = o.text, caret = o.caret, help = o.help || null, force = !!o.force;
  if (inCommentOrString(text, caret)) return null;
  const before = text.slice(0, caret);
  const pm = /[A-Za-z_]\w*$/.exec(before);
  const prefix = pm ? pm[0] : "";
  const from = caret - prefix.length;
  if (from > 0 && /\w/.test(text[from - 1])) return null;              // in the middle of a number or word
  if (/\w/.test(text[caret] || "")) return null;                       // the word continues after the caret
  const head = before.slice(0, from);
  if (/\b(class|interface|namespace)\s+$/.test(head)) return null;     // naming something new

  const masked = mask(text);
  const maskedOthers = (o.others || []).map(mask);
  const allClasses = new Map();
  const mine = parseClasses(text);
  for (const t of o.others || []) for (const c of parseClasses(t)) allClasses.set(c.name, c);
  for (const c of mine) allClasses.set(c.name, c);
  const enclosing = mine.find((c) => c.bodyStart <= caret && caret <= c.bodyEnd) || null;
  const ix = apiIndex(help);
  const apiGroupNames = new Set(ix ? ix.groups.map((g) => g.name) : lists.api);

  // ---- types of expressions
  function chainOf(c) {                                          // members of class + its bases
    const out = [], seen = new Set();
    (function walk(n) {
      const k = allClasses.get(n);
      if (!k || seen.has(n)) return;
      seen.add(n);
      out.push(...k.members);
      k.bases.forEach(walk);
    })(c);
    return out;
  }
  function findMember(type, name) { return chainOf(baseName(type)).find((m) => m.name === name) || null; }

  function typeOfIdent(name, depth) {
    if (depth > 4) return null;
    if (name === "this") return enclosing ? enclosing.name : null;
    if (name === "base") return enclosing && enclosing.bases.find((b) => allClasses.get(b) && allClasses.get(b).kind === "class") || null;
    const near = (re, txt) => { let last = null, m; re.lastIndex = 0; while ((m = re.exec(txt))) { if (m.index < caret || !last) last = m; if (m.index >= caret && last) break; } return last; };
    // foreach (var x in list)
    let m = near(new RegExp("foreach\\s*\\(\\s*var\\s+" + name + "\\s+in\\s+([\\w.]+)\\s*\\)", "g"), masked);
    if (m) { const r = resolveChain(m[1], depth + 1); const t = r && r.kind === "type" ? elemType(r.type, true) : null; if (t) return t; }
    // list.Find(x => ...), list.Sort((a, b) => ...)
    m = near(new RegExp("([\\w.]+)\\s*\\.\\s*(?:" + LIST_LAMBDAS + ")\\s*\\(\\s*\\(?\\s*(?:\\w+\\s*,\\s*)?" + name + "(?:\\s*,\\s*\\w+)?\\s*\\)?\\s*=>", "g"), masked);
    if (m) { const r = resolveChain(m[1], depth + 1); const t = r && r.kind === "type" ? elemType(r.type) : null; if (t) return t; }
    // Type x ...   (a declaration, parameter, field, "is Type x")
    for (const txt of [masked, ...maskedOthers]) {
      const re = new RegExp("(?:^|[^\\w.])(" + TYPE + ")\\s+" + name + "(?!\\w)\\s*(?=[=;,):]|\\bin\\b|$)", "gm");
      let best = null, mm;
      while ((mm = re.exec(txt))) {
        if (KW.has(mm[1]) && !TYPEKW.has(mm[1])) continue;
        if (!best || mm.index < caret || txt !== masked) best = mm;
        if (txt === masked && mm.index >= caret) break;
      }
      if (best) {
        if (best[1] === "var") {
          const init = new RegExp("^\\s*=\\s*new\\s+(" + TYPE + ")").exec(txt.slice(best.index + best[0].length));
          return init ? init[1] : null;
        }
        return best[1];
      }
    }
    return null;
  }
  function resolveChain(str, depth) {                                  // "a.b.c" with no calls
    const parts = str.split(/\s*\.\s*/);
    const first = parts[0];
    let cur;
    if (allClasses.has(first) || apiGroupNames.has(first) || BUILTIN_STATIC[first]) cur = { kind: "class", name: first };
    else { const t = typeOfIdent(first, depth); if (!t) return null; cur = { kind: "type", type: t }; }
    for (const p of parts.slice(1)) {
      const k = cur.kind === "class" ? cur.name : baseName(cur.type);
      const mem = findMember(k, p);
      if (!mem || mem.kind !== "field") return null;
      cur = { kind: "type", type: mem.type };
    }
    return cur;
  }
  function resolveExpr(expr) {
    expr = expr.replace(/\s+$/, "");
    if (expr.endsWith("]")) {                                          // list[i]. / array[i].
      let d = 0, i = expr.length - 1;
      for (; i >= 0; i--) { if (expr[i] === "]") d++; else if (expr[i] === "[" && --d === 0) break; }
      if (i < 0) return null;
      const r = resolveExpr(expr.slice(0, i));
      const t = r && r.kind === "type" ? elemType(r.type) : null;
      return t ? { kind: "type", type: t } : null;
    }
    const m = /((?:[A-Za-z_]\w*\s*\.\s*)*[A-Za-z_]\w*)$/.exec(expr);
    if (!m) return null;
    const prev = expr.slice(0, m.index).replace(/\s+$/, "").slice(-1);
    if (prev === ")" || prev === "]") return null;                     // after a call: unknown
    return resolveChain(m[1], 0);
  }

  // ---- items
  const memberItem = (m) => ({
    label: m.name, kind: m.kind === "method" ? "method" : "field",
    insert: m.kind === "method" ? (m.params.trim() ? m.name + "(" : m.name + "()") : m.name,
    detail: m.kind === "method" ? `${m.type} ${m.name}(${m.params.trim()})` : `${m.type} ${m.name}`, doc: m.cls,
  });
  let items = [], plain = false;

  const dm = /\.\s*$/.exec(head);
  if (dm) {
    const r = resolveExpr(head.slice(0, dm.index));
    if (!r) return null;
    if (r.kind === "class") {
      const k = allClasses.get(r.name);
      if (k) items = chainOf(r.name).filter((m) => m.isStatic).map(memberItem);
      else if (ix && apiGroupNames.has(r.name)) items = apiMembers(ix, r.name);
      else if (BUILTIN_STATIC[r.name]) items = builtinItems(BUILTIN_STATIC[r.name]);
    } else {
      const k = baseName(r.type);
      if (allClasses.has(k)) { items = chainOf(k).map(memberItem); items.push({ label: "ToString", kind: "method", insert: "ToString()", detail: "string ToString()", doc: "" }); }
      else { const bk = builtinKey(r.type); if (bk) items = builtinItems(BUILTIN[bk]); }
    }
  } else if (/\bnew\s+$/.test(head)) {
    for (const c of allClasses.values()) if (c.kind === "class") items.push({ label: c.name, kind: "class", insert: c.name + "(", detail: "klasa", doc: "" });
    for (const t of ["List", "Dictionary"]) items.push({ label: t, kind: "type", insert: t + "<", detail: t + "<…>", doc: "" });
    if (ix) for (const e of help.entries) if (e.sig.startsWith("new ")) items.push({ label: e.name, kind: "api", insert: e.name + "(", detail: e.sig, doc: e.doc });
  } else {
    plain = true;
    if (!force && prefix.length < 2) return null;
    const seen = new Set();
    const add = (it) => { if (!seen.has(it.label)) { seen.add(it.label); items.push(it); } };
    if (enclosing) for (const m of chainOf(enclosing.name)) add(memberItem(m));
    for (const c of allClasses.values()) add({ label: c.name, kind: "class", insert: c.name, detail: c.kind === "interface" ? "interfejs" : "klasa", doc: "" });
    if (ix) {
      for (const g of ix.groups) {
        const es = ix.byGroup.get(g.name) || [];
        if (g.name === "Coroutines") { for (const it of apiMembers(ix, g.name)) add(it); continue; }
        if (es.length && !es.every((e) => e.sig.startsWith("new "))) add({ label: g.name, kind: "api", insert: g.name, detail: "API", doc: g.doc, dot: true });
      }
    } else for (const n of lists.api) if (!/^WaitFor/.test(n)) add({ label: n, kind: "api", insert: n, detail: "API", doc: "", dot: true });
    for (const t of lists.types) add({ label: t, kind: "type", insert: t, detail: "", doc: "" });
    for (const k of lists.keywords.concat(lists.literals)) add({ label: k, kind: "kw", insert: k, detail: "", doc: "" });
    const words = masked.match(/\b[A-Za-z_]\w{2,}\b/g) || [];
    for (const w of words) if (w !== prefix) add({ label: w, kind: "var", insert: w, detail: "", doc: "" });
  }

  // ---- filter and order
  const q = prefix.toLowerCase();
  const score = (l) => (!q ? 3 : l.startsWith(prefix) ? 0 : l.toLowerCase().startsWith(q) ? 1 : q.length >= 2 && l.toLowerCase().includes(q) ? 2 : -1);
  const pri = { field: 0, method: 0, var: 1, class: 1, api: 2, const: 2, type: 3, kw: 4 };
  const seenLabels = new Set();
  const out = [];
  for (const it of items) {
    const s = score(it.label);
    if (s < 0 || seenLabels.has(it.label)) continue;
    seenLabels.add(it.label);
    out.push({ it, s });
  }
  out.sort((a, b) => a.s - b.s || (plain ? (pri[a.it.kind] - pri[b.it.kind]) : 0) || a.it.label.localeCompare(b.it.label));
  const result = out.slice(0, 40).map((x) => x.it);
  if (!result.length) return null;
  if (result.length === 1 && result[0].label === prefix && !dm) return null;      // already typed in full
  return { from, to: caret, items: result };
}

root.MSComplete = { analyze, lists, parseClasses };
if (typeof module !== "undefined" && module.exports) module.exports = root.MSComplete;
})(typeof window !== "undefined" ? window : globalThis);
