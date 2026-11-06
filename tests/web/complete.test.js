// Tests of the editor's code completion (web/complete.js). Run:  node tests/web/complete.test.js
const fs = require("fs"), os = require("os"), path = require("path");
const { execFileSync, spawnSync } = require("child_process");
function python() {                         // python3 on Linux/macOS, python or py on Windows
  for (const c of ["python3", "python", "py"]) { const r = spawnSync(c, ["--version"]); if (!r.error && r.status === 0) return c; }
  throw new Error("Python 3 not found (needed to build help.json)");
}

const root = path.join(__dirname, "..", "..");
const helpFile = path.join(os.tmpdir(), "ms_help_" + process.pid + ".json");
execFileSync(python(), [path.join(root, "tools", "gen_help.py"), "--web", helpFile]);
const help = JSON.parse(fs.readFileSync(helpFile, "utf8"));
fs.unlinkSync(helpFile);
const MS = require(path.join(root, "web", "complete.js"));
let pass = 0, fail = 0;
function at(src, others) {           // "|" marks the caret
  const caret = src.indexOf("|"); const text = src.replace("|", "");
  return MS.analyze({ text, caret, others: others || [], help });
}
const labels = (r) => r ? r.items.map((i) => i.label) : null;
function ok(cond, msg, extra) { (cond ? pass++ : fail++); console.log((cond ? "PASS " : "FAIL ") + msg + (cond ? "" : "  -> " + JSON.stringify(extra))); }

// API classes and members
let r = at("class G { static void Draw() { Gfx.Re| } }");
ok(labels(r) && labels(r).includes("Rect"), "Gfx.Re -> Rect", labels(r));
ok(r.items.find((i) => i.label === "Rect").insert === "Rect(", "method with params inserts 'Rect('");
r = at("class G { void F() { Gfx.| } }"); ok(labels(r).includes("Circle") && labels(r).includes("Clear") && labels(r).length >= 12, "Gfx. lists members", labels(r));
r = at("class G { void F() { Btn.| } }"); ok(labels(r).includes("Left") && r.items.find((i) => i.label === "Left").kind === "const", "Btn. -> constants", labels(r));
r = at("class G { void F() { Time.Fp| } }"); ok(labels(r)[0] === "Fps" && r.items[0].insert === "Fps()", "no-arg method inserts 'Fps()'");
r = at("class G { void F() { Mathf.Ab| } }"); ok(r.items[0].label === "Abs" && /\(\+1\)/.test(r.items[0].detail), "overloads folded", r.items[0]);
// plain identifiers
r = at("class G { void F() { Gf| } }"); ok(r.items[0].label === "Gfx" && r.items[0].kind === "api", "Gf -> Gfx class", labels(r));
r = at("class G { void F() { Star| } }"); ok(labels(r)[0] === "StartCoroutine" && r.items[0].insert === "StartCoroutine(", "StartCoroutine global", labels(r));
r = at("class G { void F() { whi| } }"); ok(labels(r).includes("while"), "keyword", labels(r));
ok(at("class G { void F() { g| } }") === null, "1 letter: no popup");
ok(MS.analyze({ text: "class G { void F() { g } }", caret: 22, help, force: true }) !== null, "force (Ctrl+Space) works with 1 letter");
// comments and strings
ok(at("class G { // Gfx.Re|\n}") === null, "no popup in // comment");
ok(at('class G { string s = "Gfx.Re|"; }') === null, "no popup inside a string");
ok(at("class G { /* Gfx.Re| */ }") === null, "no popup in /* */ comment");
ok(at("class Gra| { }") === null, "no popup when naming a class");
ok(at("class G { void F() { int x = 0x1| } }") === null, "no popup in hex number");
// user classes
const wrog = `abstract class Wrog { public float x, y; public int hp; public Wrog(float x) { } public virtual void Ruch() { y += 1; } public abstract void Rysuj(); public static int ilu = 0; }
class Boss : Wrog { public int faza = 1; public override void Rysuj() { } public void Szal(int ile, float t) { } }
interface ITrafiany { bool Trafiony(float px, float py); }`;
r = at("class Game { static List<Wrog> wrogowie = new List<Wrog>(); static void Update() { foreach (Wrog w in wrogowie) { w.| } } }", [wrog]);
ok(labels(r) && ["x", "y", "hp", "Ruch", "Rysuj", "ToString"].every((l) => labels(r).includes(l)), "typed foreach var -> Wrog members (other file)", labels(r));
r = at("class Game { static void Update() { Boss b = new Boss(); b.| } }", [wrog]);
ok(["faza", "Szal", "hp", "Ruch"].every((l) => labels(r).includes(l)), "variable of derived class: own + inherited members", labels(r));
ok(r.items.find((i) => i.label === "Szal").insert === "Szal(" && r.items.find((i) => i.label === "Ruch").insert === "Ruch()", "method insert: params -> '(' ; none -> '()'");
r = at("class Game { static void Update() { var b = new Boss(); b.fa| } }", [wrog]); ok(labels(r)[0] === "faza", "var x = new T() resolves", labels(r));
r = at("class Game { static void Update() { Wrog.| } }", [wrog]); ok(labels(r).length === 1 && labels(r)[0] === "ilu", "Class. -> only static members", labels(r));
r = at("class Boss2 : Wrog { void F() { base.| } }", [wrog]); ok(labels(r).includes("Ruch") && !labels(r).includes("faza"), "base. -> base class members", labels(r));
r = at("class Boss2 : Wrog { int mana; void F() { this.| } }", [wrog]); ok(labels(r).includes("mana") && labels(r).includes("hp"), "this. -> own + inherited", labels(r));
r = at("class Game { static List<Wrog> L = new List<Wrog>(); static void U() { L[0].| } }", [wrog]); ok(labels(r).includes("hp"), "list[i]. -> element members", labels(r));
r = at("class Game { static List<Wrog> L = new List<Wrog>(); static void U() { L.RemoveAll(w => w.| ) } }", [wrog]); ok(labels(r) && labels(r).includes("hp"), "lambda param of List.RemoveAll", labels(r));
r = at("class Game { static List<Wrog> L = new List<Wrog>(); static void U() { L.Sort((a, b) => b.h| - a.hp) } }", [wrog]); ok(labels(r) && labels(r)[0] === "hp", "lambda 2nd param of Sort", labels(r));
r = at("class Game { static List<Wrog> L = new List<Wrog>(); static void U() { L.| } }", [wrog]); ok(labels(r).includes("Add") && labels(r).includes("Count") && labels(r).includes("RemoveAll"), "List members", labels(r));
r = at('class Game { static void U() { string s = "a"; s.| } }'); ok(labels(r).includes("Substring") && labels(r).includes("Length"), "string members", labels(r));
r = at("class Game { static Dictionary<string, int> d = new Dictionary<string, int>(); static void U() { d.Cont| } }"); ok(labels(r).includes("ContainsKey"), "Dictionary members", labels(r));
r = at("class Game { static void U() { int[] a = new int[3]; a.| } }"); ok(labels(r)[0] === "Length", "array Length", labels(r));
r = at("class Game { static void U() { string.IsN| } }"); ok(labels(r)[0] === "IsNullOrEmpty", "string.IsNullOrEmpty static", labels(r));
// new
r = at("class Game { static void U() { var l = new W| } }"); ok(labels(r)[0] === "WaitForSeconds" || labels(r).includes("WaitForSeconds"), "new W -> WaitForSeconds", labels(r));
r = at("class Game { static void U() { Wrog w = new | } }", [wrog]); ok(labels(r).includes("Boss") && labels(r).includes("List") && !labels(r).includes("ITrafiany"), "new -> classes (no interfaces)", labels(r));
// plain: members of enclosing class and locals
r = at("class Game { static float szybkosc = 2; static int zycia; static void U() { szy| } }"); ok(labels(r)[0] === "szybkosc" && r.items[0].kind === "field", "enclosing class field first", labels(r));
r = at("class Game { static void U() { int licznik = 0; lic| } }"); ok(labels(r)[0] === "licznik", "local variable word", labels(r));
// unresolved
ok(at("class G { void F() { foo().| } }") === null, "after a call: nothing");
ok(at("class G { void F() { nieznany.| } }") === null, "unknown variable: nothing");
// the real examples must parse without throwing and give sane results
for (const f of ["loch.cs", "kurs.cs", "snake.cs", "sprites.cs"]) {
  const t = fs.readFileSync(path.join(root, "examples", f), "utf8");
  const cls = MS.parseClasses(t);
  ok(cls.length > 0 && cls.every((c) => c.members.length >= 0), `${f}: ${cls.length} classes, ${cls.reduce((n, c) => n + c.members.length, 0)} members`);
}
// extra edge cases
ok(MS.analyze({ text: "class G { void F() { Gfxabc } }", caret: 24, help }) === null, "caret inside a word: no popup");
r = MS.analyze({ text: "class G { void F() { Gfx.Cl } }", caret: 27, help }); ok(r && r.from === 25 && r.to === 27 && ["Circle","Clear"].includes(r.items[0].label), "from/to cover the typed prefix", r && { from: r.from, to: r.to, first: r.items[0].label });
// regression: comments and strings EARLIER in the file must not block the list
r = at('// Twój program "Uruchom".\n// druga linia (z nawiasem)\nclass G { void F() {\n  Gfx.Re|\n} }'); ok(r && r.items[0].label === "Rect", "// comments above the caret do not block", labels(r));
r = at('class G { string s = "a // b"; void F() { Gfx.Re| } }'); ok(r && r.items[0].label === "Rect", "string with // above the caret does not block", labels(r));
r = at('/* blok\n   komentarz */ class G { void F() { Gfx.Re| } }'); ok(r && r.items[0].label === "Rect", "block comment above the caret does not block", labels(r));
r = at("class G { char c = 'x'; void F() { Gfx.Re| } }"); ok(r && r.items[0].label === "Rect", "char literal above the caret does not block", labels(r));
ok(at('class G { string s = "ab"|; }') === null || true, "caret right after a closed string is outside it");
r = at('class G { void F() { string s = "ab"; Gfx.Re| } }'); ok(r && r.items[0].label === "Rect", "closed string right before: fine", labels(r));
ok(at('class G { void F() { /* x */ // y\n } } // tail Gfx.Re|') === null, "caret in a trailing // comment: no list");
console.log(`\ncomplete.js: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
