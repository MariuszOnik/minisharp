// Tests of the graph -> code generator (web/graph.js).  Run:  node tests/web/graph.test.js
// With the PC build present (make -f Makefile.pc) the generated programs are also compiled and run by the real
// MiniSharp compiler, and the picture of the last frame is compared with a hand-written program.
const fs = require("fs"), os = require("os"), path = require("path");
const { execFileSync, spawnSync } = require("child_process");
function python() {                         // python3 on Linux/macOS, python or py on Windows
  for (const c of ["python3", "python", "py"]) { const r = spawnSync(c, ["--version"]); if (!r.error && r.status === 0) return c; }
  throw new Error("Python 3 not found (needed to build help.json)");
}

const root = path.join(__dirname, "..", "..");
const helpFile = path.join(os.tmpdir(), "ms_help_g_" + process.pid + ".json");
execFileSync(python(), [path.join(root, "tools", "gen_help.py"), "--web", helpFile]);
const help = JSON.parse(fs.readFileSync(helpFile, "utf8"));
fs.unlinkSync(helpFile);
const G = require(path.join(root, "web", "graph.js"));
const catalog = G.buildCatalog(help);

// MS_DUMP_GRAPHS=dir: every generate() and userNodes() call of these tests is written to dir as a fixture,
// and the C port (src/fgraph.c, `minisharp --fgraph-check dir`) must give the same results for each.
if (process.env.MS_DUMP_GRAPHS) {
  const dir = process.env.MS_DUMP_GRAPHS;
  fs.mkdirSync(dir, { recursive: true });
  let k = 0;
  const save = (name, obj) => fs.writeFileSync(path.join(dir, name), JSON.stringify(obj, null, 1) + "\n");
  save("catalog.json", catalog.list.map((d) => ({ id: d.id, title: d.title, category: d.category, kind: d.kind, callee: d.callee, ret: d.ret,
    params: d.params, inputs: d.inputs, outputs: d.outputs })));
  const generate = G.generate, userNodes = G.userNodes;
  G.generate = (graph, cat, opts) => {
    const before = JSON.stringify(graph);
    const r = generate(graph, cat, opts);
    save(`g${String(++k).padStart(3, "0")}.json`, { graph: JSON.parse(before), user: cat.list.filter((d) => d.user),
      embed: !(opts && opts.embed === false), code: r.code, warnings: r.warnings, errors: r.errors });
    return r;
  };
  G.userNodes = (files) => {
    const r = userNodes(files);
    save(`u${String(++k).padStart(3, "0")}.json`, { files, defs: r });
    return r;
  };
}

let pass = 0, fail = 0;
function ok(cond, msg, extra) { (cond ? pass++ : fail++); console.log((cond ? "PASS " : "FAIL ") + msg + (cond ? "" : "  -> " + (typeof extra === "string" ? "\n" + extra : JSON.stringify(extra)))); }
const same = (actual, expected, msg) => ok(actual === expected, msg, "--- got:\n" + actual + "\n--- expected:\n" + expected);

// tiny graph builder
function build(fn) {
  const g = G.emptyGraph();
  const N = (id, type, data, x, y) => g.nodes.push({ id, type, x: x || 0, y: y || 0, data: data || {} });
  const F = (from, out, to, inp) => g.edges.push({ from, out, to, in: inp });
  fn(N, F, g);
  return g;
}
const gen = (g, opts) => G.generate(g, catalog, Object.assign({ embed: false }, opts));
const body = (code) => code.split("\n").slice(2).join("\n").replace(/\n}\n?$/, "");   // without the header comment, "class Game {" and its closing brace

// ---------------------------------------------------------------- catalog
ok(catalog.list.length > 100, `catalog has ${catalog.list.length} node types`);
ok(catalog.types.has("api.Gfx.Rect") && catalog.types.get("api.Gfx.Rect").inputs.map((p) => p.name).join() === "enter,x,y,w,h,color", "Gfx.Rect: enter + its five parameters");
ok(catalog.types.has("api.Gfx.Text/4") && catalog.types.has("api.Gfx.Text/5"), "overloads with different parameter counts get separate nodes");
ok(catalog.types.has("api.Math.Abs") && !catalog.types.has("api.Math.Abs/1"), "overloads that differ only in type are one node");
ok(catalog.types.get("api.Coroutines.StartCoroutine").callee === "StartCoroutine", "global functions are called without the class name");
ok(catalog.types.get("api.new.WaitForSeconds").callee === "new WaitForSeconds", "constructors are nodes");
ok(catalog.types.get("api.Btn.Left").kind === "const" && catalog.types.get("api.Btn.Left").callee === "Btn.Left", "constants are value nodes");
ok(catalog.types.get("api.Input.Held").outputs.some((p) => p.name === "result"), "functions with a result have a result port");
ok(!catalog.types.get("api.Gfx.Rect").outputs.some((p) => p.name === "result"), "void functions have none");
let r = G.searchCatalog(catalog, "rect"); ok(r[0].id === "api.Gfx.Rect", "search 'rect' -> Gfx.Rect first", r.slice(0, 3).map((d) => d.id));
r = G.searchCatalog(catalog, "held"); ok(r[0].id === "api.Input.Held", "search 'held' -> Input.Held", r.slice(0, 3).map((d) => d.id));
r = G.searchCatalog(catalog, "gfx circle"); ok(r[0].id === "api.Gfx.Circle", "two words: 'gfx circle'", r.slice(0, 3).map((d) => d.id));
ok(G.searchCatalog(catalog, "").length > 0 && G.searchCatalog(catalog, "zzzzqqq").length === 0, "empty query lists, nonsense finds nothing");

// ---------------------------------------------------------------- the ball
const BALL = `class Game {
    static float x = 200;
    static float y = 120;

    static void Update() {
        if (Input.Held(Btn.Left)) {
            x -= 3;
        }
        if (Input.Held(Btn.Right)) {
            x += 3;
        }
        if (Input.Held(Btn.Up)) {
            y -= 3;
        }
        if (Input.Held(Btn.Down)) {
            y += 3;
        }
        if (Input.Touching()) {
            x = Input.TouchX() * 400 / 320;
            y = Input.TouchY();
        }
    }

    static void Draw() {
        Gfx.Clear(Gfx.Rgb(20, 24, 40));
        Gfx.Circle(x, y, 14, Color.Yellow);
    }
}
`;
r = gen(G.sampleBall());
same(r.code.split("\n").slice(1).join("\n"), BALL, "sample ball graph -> the hand-written program");
ok(r.errors.length === 0 && r.warnings.length === 0, "no errors or warnings for the ball", [r.errors, r.warnings]);

// a constant node wired in instead of a typed default gives the same code
{
  const g = build((N, F) => {
    N("u", "event.Update"); N("c", "api.Gfx.Clear"); N("col", "api.Color.Red");
    F("u", "exit", "c", "enter"); F("col", "value", "c", "color");
  });
  ok(body(gen(g).code).includes("Gfx.Clear(Color.Red);"), "a wired constant node");
}

// ---------------------------------------------------------------- expressions and precedence
function expr(build2) {      // Update: x = <expression>;
  const g = build((N, F, gr) => {
    gr.variables = [{ name: "x", type: "float", init: "0" }, { name: "a", type: "float", init: "1" }, { name: "b", type: "float", init: "2" }, { name: "c", type: "float", init: "3" }, { name: "d", type: "float", init: "4" }];
    N("u", "event.Update"); N("s", "var.set", { name: "x" }); F("u", "exit", "s", "enter");
    build2(N, F);
  });
  const out = gen(g);
  const m = /\n        (x [-+*\/]?= .*);\n/.exec(out.code);
  return m ? m[1] : out.code;
}
const V = (N, id, name) => N(id, "var.get", { name });
same(expr((N, F) => { V(N, "a", "a"); V(N, "b", "b"); V(N, "c", "c"); N("p", "op.add"); N("m", "op.mul"); F("a", "value", "p", "a"); F("b", "value", "p", "b"); F("p", "result", "m", "a"); F("c", "value", "m", "b"); F("m", "result", "s", "value"); }),
  "x = (a + b) * c", "(a + b) * c keeps its parentheses");
same(expr((N, F) => { V(N, "a", "a"); V(N, "b", "b"); V(N, "c", "c"); N("m", "op.mul"); N("p", "op.add"); F("b", "value", "m", "a"); F("c", "value", "m", "b"); F("a", "value", "p", "a"); F("m", "result", "p", "b"); F("p", "result", "s", "value"); }),
  "x = a + b * c", "a + b * c needs none");
same(expr((N, F) => { V(N, "a", "a"); V(N, "b", "b"); V(N, "c", "c"); N("s1", "op.sub"); N("s2", "op.sub"); F("b", "value", "s2", "a"); F("c", "value", "s2", "b"); F("a", "value", "s1", "a"); F("s2", "result", "s1", "b"); F("s1", "result", "s", "value"); }),
  "x = a - (b - c)", "a - (b - c): the right operand of the same precedence is wrapped");
same(expr((N, F) => { V(N, "a", "a"); V(N, "b", "b"); V(N, "c", "c"); N("s1", "op.sub"); N("s2", "op.sub"); F("a", "value", "s2", "a"); F("b", "value", "s2", "b"); F("s2", "result", "s1", "a"); F("c", "value", "s1", "b"); F("s1", "result", "s", "value"); }),
  "x = a - b - c", "(a - b) - c needs none");
same(expr((N, F) => { V(N, "a", "a"); V(N, "b", "b"); N("d", "op.div"); N("e", "op.mul"); F("a", "value", "d", "a"); F("b", "value", "d", "b"); F("d", "result", "e", "a"); F("b", "value", "e", "b"); F("e", "result", "s", "value"); }),
  "x = a / b * b", "a / b * b: left-associative, no parentheses");
same(expr((N, F) => { V(N, "a", "a"); N("n", "op.neg"); N("m", "op.mul", { defaults: { b: "2" } }); F("a", "value", "n", "a"); F("n", "result", "m", "a"); F("m", "result", "s", "value"); }),
  "x = -a * 2", "unary minus binds tighter than *");
same(expr((N, F) => { V(N, "a", "a"); V(N, "b", "b"); N("p", "op.add"); N("n", "op.neg"); F("a", "value", "p", "a"); F("b", "value", "p", "b"); F("p", "result", "n", "a"); F("n", "result", "s", "value"); }),
  "x = -(a + b)", "unary minus of a sum");
{
  const g = build((N, F, gr) => {
    gr.variables = ["a", "b", "c", "d"].map((n) => ({ name: n, type: "float", init: "0" }));
    N("u", "event.Update"); N("i", "if"); F("u", "exit", "i", "enter");
    V(N, "a", "a"); V(N, "b", "b"); V(N, "c", "c"); V(N, "d", "d");
    N("e1", "cmp.eq"); N("l1", "cmp.lt"); N("and", "logic.and"); N("not", "logic.not");
    F("a", "value", "e1", "a"); F("b", "value", "e1", "b"); F("c", "value", "l1", "a"); F("d", "value", "l1", "b");
    F("e1", "result", "and", "a"); F("l1", "result", "and", "b"); F("and", "result", "not", "a"); F("not", "result", "i", "cond");
    N("t", "api.Audio.StopAll"); F("i", "true", "t", "enter");
  });
  ok(gen(g).code.includes("if (!(a == b && c < d)) {"), "logic: !(a == b && c < d)");
}
ok(G.rawPrec("x") === 100 && G.rawPrec("Btn.Left") === 100 && G.rawPrec("-5") === 90 && G.rawPrec("a + b") === 0 && G.rawPrec('"a + b"') === 100 && G.rawPrec("Foo(1, 2)") === 100, "rawPrec of typed expressions");
same(expr((N, F) => { N("m", "op.mul", { defaults: { a: "x + 1", b: "2" } }); F("m", "result", "s", "value"); }), "x = (x + 1) * 2", "a typed default with an operator is wrapped");

// ---------------------------------------------------------------- control flow
{
  const g = build((N, F, gr) => {
    gr.variables = [{ name: "hp", type: "int", init: "10" }];
    N("u", "event.Update"); N("i", "if"); N("h", "var.get", { name: "hp" }); N("lt", "cmp.lt", { defaults: { b: "5" } });
    F("u", "exit", "i", "enter"); F("h", "value", "lt", "a"); F("lt", "result", "i", "cond");
    N("t", "api.Console.WriteLine/1", { defaults: { value: '"low"' } }); N("f", "api.Console.WriteLine/1", { defaults: { value: '"ok"' } }); N("after", "api.Console.WriteLine/1", { defaults: { value: '"done"' } });
    F("i", "true", "t", "enter"); F("i", "false", "f", "enter"); F("i", "exit", "after", "enter");
  });
  same(body(gen(g).code), `    static int hp = 10;

    static void Update() {
        if (hp < 5) {
            Console.WriteLine("low");
        } else {
            Console.WriteLine("ok");
        }
        Console.WriteLine("done");
    }`, "if / else, then the exit chain");
}
{
  const g = build((N, F) => {
    N("u", "event.Update"); N("i", "if"); N("t", "api.Input.Touching"); N("p", "api.Audio.StopAll");
    F("u", "exit", "i", "enter"); F("t", "result", "i", "cond"); F("i", "false", "p", "enter");
  });
  ok(gen(g).code.includes("if (!Input.Touching()) {"), "only the false output: negated condition");
}
{
  const g = build((N, F) => {    // nested loops with the indices used in expressions
    N("d", "event.Draw"); N("fi", "for", { defaults: { last: "10" } }); N("fj", "for", { defaults: { last: "5" } }); N("r", "api.Gfx.Rect", { defaults: { w: "24", h: "24" } });
    N("ix", "op.mul", { defaults: { b: "30" } }); N("ix2", "op.add", { defaults: { b: "10" } }); N("jx", "op.mul", { defaults: { b: "30" } }); N("jx2", "op.add", { defaults: { b: "10" } });
    N("rgb", "api.Gfx.Rgb", { defaults: { b: "200" } }); N("ri", "op.mul", { defaults: { b: "25" } }); N("gj", "op.mul", { defaults: { b: "50" } });
    F("d", "exit", "fi", "enter"); F("fi", "body", "fj", "enter"); F("fj", "body", "r", "enter");
    F("fi", "index", "ix", "a"); F("ix", "result", "ix2", "a"); F("ix2", "result", "r", "x");
    F("fj", "index", "jx", "a"); F("jx", "result", "jx2", "a"); F("jx2", "result", "r", "y");
    F("fi", "index", "ri", "a"); F("fj", "index", "gj", "a"); F("ri", "result", "rgb", "r"); F("gj", "result", "rgb", "g"); F("rgb", "result", "r", "color");
  });
  const out = gen(g);
  same(body(out.code), `    static void Draw() {
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 5; j++) {
                Gfx.Rect(i * 30 + 10, j * 30 + 10, 24, 24, Gfx.Rgb(i * 25, j * 50, 200));
            }
        }
    }`, "nested For: i and j, the indices inside expressions");
  ok(out.errors.length === 0 && out.warnings.length === 0, "no errors or warnings for the loops", [out.errors, out.warnings]);
}
{
  const g = build((N, F) => {
    N("m", "event.Main"); N("fa", "for", { defaults: { first: "10", last: "0", step: "-2" } }); N("w", "api.Console.WriteLine/1"); N("fb", "for", { defaults: { last: "3", step: "2" } });
    F("m", "exit", "fa", "enter"); F("fa", "body", "w", "enter"); F("fa", "index", "w", "value"); F("fa", "exit", "fb", "enter");
    N("w2", "api.Console.WriteLine/1"); F("fb", "body", "w2", "enter"); F("fb", "index", "w2", "value");
  });
  same(body(gen(g).code), `    static void Main() {
        for (int i = 10; i > 0; i += -2) {
            Console.WriteLine(i);
        }
        for (int i = 0; i < 3; i += 2) {
            Console.WriteLine(i);
        }
    }`, "negative step counts down; sequential loops reuse i");
}
{
  const g = build((N, F, gr) => {
    gr.variables = [{ name: "n", type: "int", init: "10" }];
    N("m", "event.Main"); N("w", "while"); N("g", "var.get", { name: "n" }); N("gt", "cmp.gt", { defaults: { b: "0" } }); N("s", "var.set", { name: "n" }); N("sub", "op.sub", { defaults: { b: "1" } }); N("g2", "var.get", { name: "n" });
    F("m", "exit", "w", "enter"); F("g", "value", "gt", "a"); F("gt", "result", "w", "cond"); F("w", "body", "s", "enter"); F("g2", "value", "sub", "a"); F("sub", "result", "s", "value");
  });
  ok(body(gen(g).code).includes("while (n > 0) {\n            n -= 1;\n        }"), "while loop with a compound assignment");
}

// ---------------------------------------------------------------- results of calls become locals
{
  const g = build((N, F) => {
    N("m", "event.Main"); N("load", "api.Gfx.LoadImage", { defaults: { path: '"hero.png"' } }); N("w", "api.Console.WriteLine/1"); N("w2", "api.Console.WriteLine/1"); N("rnd", "api.Random.Float");
    F("m", "exit", "load", "enter"); F("load", "exit", "w", "enter"); F("w", "exit", "w2", "enter");
    F("load", "result", "w", "value"); F("load", "result", "w2", "value");
  });
  const out = gen(g);
  same(body(out.code), `    static void Main() {
        var loadImage = Gfx.LoadImage("hero.png");
        Console.WriteLine(loadImage);
        Console.WriteLine(loadImage);
    }`, "a call the flow runs through, with its result used twice -> one local");
}
{
  const g = build((N, F) => {   // the same call without flow: computed where it is used (and twice)
    N("m", "event.Main"); N("w", "api.Console.WriteLine/1"); N("w2", "api.Console.WriteLine/1"); N("rnd", "api.Random.Float");
    F("m", "exit", "w", "enter"); F("w", "exit", "w2", "enter"); F("rnd", "result", "w", "value"); F("rnd", "result", "w2", "value");
  });
  ok(body(gen(g).code).split("Random.Float()").length === 3, "a pure call is evaluated where used");
}
{
  const g = build((N, F) => {
    N("m", "event.Main"); N("load", "api.Gfx.LoadImage"); F("m", "exit", "load", "enter");
  });
  ok(body(gen(g).code).includes('Gfx.LoadImage("");') && !body(gen(g).code).includes("var "), "an unused result is not stored; default argument by type");
}
{
  const g = build((N, F) => {      // a local used outside the block that made it
    N("m", "event.Main"); N("i", "if"); N("load", "api.Gfx.LoadImage"); N("w", "api.Console.WriteLine/1");
    F("m", "exit", "i", "enter"); F("i", "true", "load", "enter"); F("i", "exit", "w", "enter"); F("load", "result", "w", "value");
  });
  const out = gen(g);
  ok(out.warnings.some((w) => /poza blokiem/.test(w.message)), "warning: a local used outside its block", out.warnings);
}

// ---------------------------------------------------------------- several events, warnings and errors
{
  const g = build((N, F) => {
    N("u1", "event.Update", {}, 0, 100); N("u2", "event.Update", {}, 0, 0);
    N("a", "api.Audio.StopAll"); N("b", "api.Console.WriteLine/1", { defaults: { value: '"b"' } });
    F("u1", "exit", "b", "enter"); F("u2", "exit", "a", "enter");
  });
  same(body(gen(g).code), `    static void Update() {
        Audio.StopAll();
        Console.WriteLine("b");
    }`, "two Update events run one after another, upper one first");
}
{
  const g = build((N, F) => { N("u", "event.Update"); N("a", "api.Audio.StopAll"); });
  const out = gen(g);
  ok(out.warnings.some((w) => w.node === "a" && /nie jest połączony/.test(w.message)), "warning for a flow node no event leads to", out.warnings);
  ok(!out.code.includes("StopAll"), "…and it is not in the code");
}
ok(gen(G.emptyGraph()).warnings.some((w) => /zdarzenia/.test(w.message)), "warning: no event node at all");
ok(gen(build((N) => N("d", "event.Draw"))).warnings.length === 0, "a Draw-only graph is fine for the generator (Update may live in another file)");
{
  const inUpdate = build((N, F) => { N("u", "event.Update"); N("c", "api.Gfx.Circle"); F("u", "exit", "c", "enter"); });
  ok(gen(inUpdate).warnings.some((w) => w.node === "c" && /rysuje w Update/.test(w.message)), "warning: drawing in Update is wiped by Draw's Gfx.Clear");
  const inDraw = build((N, F) => { N("d", "event.Draw"); N("c", "api.Gfx.Circle"); F("d", "exit", "c", "enter"); });
  ok(!gen(inDraw).warnings.some((w) => /rysuje/.test(w.message)), "drawing in Draw: no warning");
  const ordered = build((N, F, gr) => { gr.order = -5; N("u", "event.Update"); });
  ok(gen(ordered).code.includes("\n[Order(-5)]\nclass Game {"), "graph.order -5 -> [Order(-5)] above the class");
  const noOrder = build((N, F, gr) => { gr.order = 2.5; N("u", "event.Update"); });
  ok(!gen(noOrder).code.includes("[Order"), "a non-integer order is ignored");
  const screen = build((N, F) => { N("u", "event.Update"); N("s", "api.Gfx.Screen"); F("u", "exit", "s", "enter"); });
  ok(!gen(screen).warnings.some((w) => /rysuje/.test(w.message)), "Gfx.Screen in Update is not drawing");
}
{
  const g = build((N, F) => { N("u", "event.Update"); N("s", "var.set", { name: "ghost" }); F("u", "exit", "s", "enter"); });
  ok(gen(g).warnings.some((w) => /ghost/.test(w.message)), "warning: undeclared variable");
}
{
  const g = build((N, F) => { N("u", "event.Update"); N("s", "var.set", { name: "1bad" }); F("u", "exit", "s", "enter"); });
  ok(gen(g).errors.some((e) => /nazwy/.test(e.message)), "error: bad variable name");
}
{
  const g = build((N, F, gr) => {
    gr.variables = [{ name: "x", type: "float", init: "0" }];
    N("u", "event.Update"); N("s", "var.set", { name: "x" }); N("a", "op.add"); N("b", "op.add");
    F("u", "exit", "s", "enter"); F("a", "result", "b", "a"); F("b", "result", "a", "a"); F("b", "result", "s", "value");
  });
  ok(gen(g).errors.some((e) => /cykl/.test(e.message)), "error: a cycle in the value wires");
}
{
  const g = build((N, F) => {
    N("u", "event.Update"); N("a", "api.Audio.StopAll"); N("b", "api.Audio.StopAll");
    F("u", "exit", "a", "enter"); F("a", "exit", "b", "enter"); F("b", "exit", "a", "enter");
  });
  const out = gen(g);
  ok(out.warnings.some((w) => /pętla w przepływie/.test(w.message)) && out.code.includes("static void Update"), "a loop in the flow is reported, generation still ends", out.warnings);
}
{
  const g = build((N, F) => { N("u", "event.Update"); N("z", "no.such.node"); F("u", "exit", "z", "enter"); });
  ok(G.validate(g, catalog).some((i) => /nieznany typ/.test(i.message)), "validate: unknown node type");
  const g2 = build((N, F) => { N("u", "event.Update"); N("a", "api.Audio.StopAll"); F("u", "nope", "a", "enter"); });
  ok(G.validate(g2, catalog).some((i) => /nie ma wyjścia/.test(i.message)), "validate: missing port");
  const g3 = build((N, F) => { N("u", "event.Update"); N("a", "api.Audio.StopAll"); N("h", "api.Input.Touching"); F("u", "exit", "a", "enter"); F("h", "result", "a", "enter"); });
  ok(G.validate(g3, catalog).some((i) => /przepływ z wartością/.test(i.message)), "validate: flow wired to a value");
  const g4 = build((N, F) => { N("u", "event.Update"); N("a", "api.Audio.Play/1"); N("c1", "lit", { value: "1" }); N("c2", "lit", { value: "2" }); F("u", "exit", "a", "enter"); F("c1", "value", "a", "sound"); F("c2", "value", "a", "sound"); });
  ok(G.validate(g4, catalog).some((i) => /kilka połączeń/.test(i.message)), "validate: two wires into one value input");
}


// ---------------------------------------------------------------- nodes from the project's own code
const LIB = `class Lib {
    // Kwadrat liczby.
    static float Sq(float v) { return v * v; }

    /// <summary>Kropka w danym miejscu</summary>
    // @node Narysuj kropkę
    static void Dot(float x, float y) { Gfx.Circle(x, y, 5, Color.White); }

    // @node hide
    static int Secret() { return 1; }

    static void Say(string value, int result) { Console.WriteLine(value); }

    float Inst(float a) { return a; }
    static void Update() { }
    static void Draw() { }
}
`;
const GRAPHFILE = "class Graph { static void Hidden() { } }\n// @graph {\"nodes\":[],\"edges\":[]}\n";
const un = G.userNodes([{ name: "Lib.cs", text: LIB }, { name: "Graph.cs", text: GRAPHFILE }]);
ok(un.map((d) => d.id).sort().join() === "user.Lib.Dot,user.Lib.Say,user.Lib.Sq", "static methods become nodes (not instance methods, Main/Update/Draw, hidden ones or graph files)", un.map((d) => d.id));
const sq = un.find((d) => d.id === "user.Lib.Sq"), dot = un.find((d) => d.id === "user.Lib.Dot");
ok(sq.doc === "Kwadrat liczby." && sq.title === "Lib.Sq" && sq.category === "Kod: Lib" && sq.sig === "float Sq(float v)", "description from the comment above, category = class", sq);
ok(dot.title === "Narysuj kropkę" && dot.doc === "Kropka w danym miejscu", "// @node Title renames the node; xml summary tags are stripped", dot);
ok(sq.inputs.map((p) => p.name).join() === "enter,v" && sq.outputs.map((p) => p.name).join() === "exit,result", "ports: flow in/out, a value in per parameter, the result");
ok(dot.outputs.map((p) => p.name).join() === "exit", "a void method has no result port");
const say = un.find((d) => d.id === "user.Lib.Say");
ok(say.inputs.map((p) => p.name).join() === "enter,value_,result_", "parameter names that clash with port names get a trailing _", say.inputs.map((p) => p.name));
const catU = G.buildCatalog(help, un);
ok(catU.types.has("user.Lib.Dot") && catU.types.has("api.Gfx.Rect"), "the catalog holds both the API and the project's nodes");
ok(G.searchCatalog(catU, "kropk")[0].id === "user.Lib.Dot" && G.searchCatalog(catU, "dot")[0].id === "user.Lib.Dot", "palette search finds them by title and by name", G.searchCatalog(catU, "kropk").slice(0, 2).map((d) => d.id));
ok(G.searchCatalog(catU, "").slice(0, 3).every((d) => d.user), "the empty palette lists the project's nodes first");
{
  const g = build((N, F) => {
    N("u", "event.Update"); N("d", "event.Draw"); N("dot", "user.Lib.Dot"); N("sq", "user.Lib.Sq", { defaults: { v: "10" } }); N("say", "user.Lib.Say", { defaults: { value_: '"hi"', result_: "3" } });
    F("d", "exit", "dot", "enter"); F("sq", "result", "dot", "x"); F("dot", "exit", "say", "enter");
  });
  const out = G.generate(g, catU, { embed: false });
  same(body(out.code).split("\n").filter((l) => l.includes("Lib.")).join("\n"), `        Lib.Dot(Lib.Sq(10), 0);
        Lib.Say("hi", 3);`, "generated calls: Class.Method(args), a result node wired into an argument");
  ok(out.errors.length === 0, "no errors", out.errors);
  const lost = G.generate(g, G.buildCatalog(help, []), { embed: false });
  ok(lost.errors.some((e) => /nieznany typ/.test(e.message)), "a node whose method disappeared from the code is reported (unknown node type)", lost.errors);
}


// ---------------------------------------------------------------- the graph's own methods, called from code
{
  const g = build((N, F, gr) => {
    gr.className = "Graph";
    gr.variables = [{ name: "n", type: "int", init: "0" }];
    N("m1", "event.Method", { name: "Tick" }, 0, 0); N("m2", "event.Method", { name: "Alpha" }, 0, 100); N("d", "event.Draw", {}, 0, 200);
    N("s", "var.set", { name: "n" }); N("add", "op.add", { defaults: { b: "1" } }); N("g", "var.get", { name: "n" });
    F("m1", "exit", "s", "enter"); F("g", "value", "add", "a"); F("add", "result", "s", "value");
    N("w", "api.Console.WriteLine/1", { defaults: { value: '"a"' } }); F("m2", "exit", "w", "enter");
  });
  const out = gen(g);
  same(out.code.split("\n").slice(1).join("\n"), `class Graph {
    static int n = 0;

    static void Draw() {
    }

    // Do wywołania z kodu: Graph.Alpha();
    static void Alpha() {
        Console.WriteLine("a");
    }

    // Do wywołania z kodu: Graph.Tick();
    static void Tick() {
        n += 1;
    }
}
`, "Metoda nodes become static methods: entry points first, then the others by name, with a hint how to call them");
  ok(out.errors.length === 0, "no errors", out.errors);
  const bad = (name, vars) => gen(build((N, F, gr) => { gr.variables = vars || []; N("m", "event.Method", { name }); }));
  ok(bad("Update").errors.some((e) => /wejście programu/.test(e.message)), "error: a Metoda may not be called Update");
  ok(bad("1x").errors.some((e) => /poprawnej nazwy/.test(e.message)) && bad("").errors.length === 1, "error: a Metoda needs a valid name");
  ok(bad("n", [{ name: "n", type: "int", init: "0" }]).errors.some((e) => /nazwą zmiennej/.test(e.message)), "error: a method named like a variable");
  const twice = gen(build((N, F) => { N("a", "event.Method", { name: "Go" }, 0, 0); N("b", "event.Method", { name: "Go" }, 0, 100); N("w1", "api.Audio.StopAll"); N("w2", "api.Console.WriteLine/0"); F("a", "exit", "w1", "enter"); F("b", "exit", "w2", "enter"); }));
  ok(twice.code.split("static void Go()").length === 2 && twice.code.includes("Audio.StopAll();\n        Console.WriteLine();"), "two nodes with the same method name run one after another in one method");
}


// ---------------------------------------------------------------- a class to make objects from (instance mode)
{
  const g = build((N, F, gr) => {
    gr.className = "Ball"; gr.instance = true;
    gr.variables = [{ name: "x", type: "float", init: "200" }];
    N("m", "event.Method", { name: "Tick" }, 0, 0); N("s", "var.set", { name: "x" }); N("add", "op.add", { defaults: { b: "1" } }); N("gx", "var.get", { name: "x" });
    F("m", "exit", "s", "enter"); F("gx", "value", "add", "a"); F("add", "result", "s", "value");
    N("u", "event.Update", {}, 0, 100); N("w", "api.Audio.StopAll"); F("u", "exit", "w", "enter");
  });
  const out = gen(g);
  same(out.code.split("\n").slice(1).join("\n"), `class Ball {
    float x = 200;

    // Do wywołania z kodu: var o = new Ball(); o.Tick();
    void Tick() {
        x += 1;
    }
}
`, "instance mode: fields and methods without static; Update does not belong in such a class");
  ok(out.errors.some((e) => /Main, Update i Draw są statyczne/.test(e.message)), "…and the Update node is reported as an error", out.errors);
  g.instance = false;
  ok(gen(g).code.includes("static float x = 200;") && gen(g).code.includes("static void Tick()"), "switching it off brings back static");
}

// ---------------------------------------------------------------- the graph travels inside the file
{
  const g = G.sampleBall();
  const code = G.generate(g, catalog).code;
  const lines = code.split("\n");
  const gl = lines.filter((l) => l.startsWith("// @graph "));
  ok(gl.length === 1 && lines[lines.length - 2] === gl[0], "one '// @graph' comment line, the last line of the file");
  ok(JSON.stringify(G.extractGraph(code)) === JSON.stringify(g), "extractGraph returns the same graph");
  ok(G.extractGraph("class A {}\n") === null && G.extractGraph("// @graph {oops") === null, "no graph / broken graph -> null");
  const odd = G.emptyGraph(); odd.nodes.push({ id: "n1", type: "lit", x: 0, y: 0, data: { value: "\"a\u2028b */ c\"" } });
  ok(JSON.stringify(G.extractGraph(G.generate(odd, catalog).code)) === JSON.stringify(odd), "special characters (U+2028, */) survive the round trip");
}

// ---------------------------------------------------------------- the real compiler
const exe = path.join(root, process.platform === "win32" ? "minisharp.exe" : "minisharp");
if (!fs.existsSync(exe)) console.log("SKIP real-compiler tests: build the PC version first (make -f Makefile.pc)");
else {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "msgraph_"));
  const run = (args) => spawnSync(exe, args, { encoding: "utf8" });
  const write = (name, code) => { const f = path.join(tmp, name); fs.writeFileSync(f, code); return f; };
  const compiles = (name, code) => { const res = run([write(name, code), "--check"]); ok(res.status === 0, `real compiler accepts ${name}`, res.stdout + res.stderr); };
  const image = (name, code, press) => { const f = write(name + ".cs", code), p = path.join(tmp, name + ".ppm"); const res = run([f, "--frames", "40", "--press", press, "--ppm", p]); ok(res.status === 0 && fs.existsSync(p), `${name}: runs 40 frames`, res.stdout + res.stderr); return fs.existsSync(p) ? fs.readFileSync(p) : Buffer.alloc(0); };

  // the same picture from the graph and from hand-written code
  const handBall = BALL;
  const a = image("ball_graph", G.generate(G.sampleBall(), catalog).code, "5:Right 8:Down 12:Left");
  const b = image("ball_hand", handBall, "5:Right 8:Down 12:Left");
  ok(a.length > 1000 && a.equals(b), "ball: the picture from the graph equals the hand-written one (byte for byte)");
  const c = image("ball_graph2", G.generate(G.sampleBall(), catalog).code, "1:Up 2:Up 3:Up 20:Left");
  const d = image("ball_hand2", handBall, "1:Up 2:Up 3:Up 20:Left");
  ok(c.equals(d) && !c.equals(a), "…and with other buttons (and the picture really changes with the input)");

  const loops = build((N, F) => {
    N("u", "event.Update"); N("d", "event.Draw"); N("cl", "api.Gfx.Clear"); N("fi", "for", { defaults: { last: "10" } }); N("fj", "for", { defaults: { last: "5" } }); N("r", "api.Gfx.Rect", { defaults: { w: "24", h: "24" } });
    N("ix", "op.mul", { defaults: { b: "30" } }); N("ix2", "op.add", { defaults: { b: "10" } }); N("jx", "op.mul", { defaults: { b: "30" } }); N("jx2", "op.add", { defaults: { b: "10" } });
    N("rgb", "api.Gfx.Rgb", { defaults: { b: "200" } }); N("ri", "op.mul", { defaults: { b: "25" } }); N("gj", "op.mul", { defaults: { b: "50" } });
    F("d", "exit", "cl", "enter"); F("cl", "exit", "fi", "enter"); F("fi", "body", "fj", "enter"); F("fj", "body", "r", "enter");
    F("fi", "index", "ix", "a"); F("ix", "result", "ix2", "a"); F("ix2", "result", "r", "x");
    F("fj", "index", "jx", "a"); F("jx", "result", "jx2", "a"); F("jx2", "result", "r", "y");
    F("fi", "index", "ri", "a"); F("fj", "index", "gj", "a"); F("ri", "result", "rgb", "r"); F("gj", "result", "rgb", "g"); F("rgb", "result", "r", "color");
  });
  const handLoops = `class Game {
    static void Update() {
    }

    static void Draw() {
        Gfx.Clear(0);
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 5; j++) {
                Gfx.Rect(i * 30 + 10, j * 30 + 10, 24, 24, Gfx.Rgb(i * 25, j * 50, 200));
            }
        }
    }
}
`;
  const e = image("loops_graph", G.generate(loops, catalog).code, "1:A"), f = image("loops_hand", handLoops, "1:A");
  ok(e.length > 1000 && e.equals(f), "nested loops: the picture from the graph equals the hand-written one");

  // everything the generator can emit must compile
  const kitchen = build((N, F, gr) => {
    gr.variables = [{ name: "score", type: "int", init: "0" }, { name: "t", type: "float", init: "0" }];
    N("m", "event.Main"); N("load", "api.Gfx.LoadImage", { defaults: { path: '"x.png"' } }); N("snd", "api.new.WaitForSeconds", { defaults: { seconds: "1.5f" } });
    N("sc", "api.Coroutines.StartCoroutine"); F("m", "exit", "load", "enter"); F("load", "exit", "sc", "enter");
    N("u", "event.Update"); N("w", "while"); N("sg", "var.get", { name: "score" }); N("lt", "cmp.lt", { defaults: { b: "3" } }); N("i", "if"); N("ab", "api.Mathf.Abs");
    F("u", "exit", "w", "enter"); F("sg", "value", "lt", "a"); F("lt", "result", "w", "cond"); F("w", "body", "i", "enter"); F("lt", "result", "i", "cond");
    N("ss", "var.set", { name: "score" }); N("add", "op.add", { defaults: { b: "1" } }); N("sg2", "var.get", { name: "score" });
    F("i", "false", "ss", "enter"); F("sg2", "value", "add", "a"); F("add", "result", "ss", "value");
    N("t5", "api.Gfx.Text/5", { defaults: { text: '"hi"', scale: "1.5f" } }); N("t4", "api.Gfx.Text/4"); F("i", "true", "t5", "enter"); F("t5", "exit", "t4", "enter");
    N("dr", "event.Draw"); N("sc2", "api.Gfx.Screen", { defaults: { screen: "Screen.Bottom" } }); F("dr", "exit", "sc2", "enter");
  });
  const kout = G.generate(kitchen, catalog);
  compiles("kitchen.cs", kout.code);
  for (const f2 of ["sampleBall"]) compiles(f2 + ".cs", G.generate(G[f2](), catalog).code);

  // a graph that calls the project's own static methods: same picture as the hand-written program
  {
    const lib = `class Lib {
    static float Sq(float v) { return v * v; }
    static void Dot(float x, float y) { Gfx.Circle(x, y, 5, Color.White); }
}
`;
    const un2 = G.userNodes([{ name: "Lib.cs", text: lib }]);
    const cat2 = G.buildCatalog(help, un2);
    const gg = build((N, F) => {
      N("u", "event.Update"); N("d", "event.Draw"); N("cl", "api.Gfx.Clear"); N("a", "user.Lib.Dot", { defaults: { y: "20" } }); N("b", "user.Lib.Dot", { defaults: { x: "150" } });
      N("s1", "user.Lib.Sq", { defaults: { v: "10" } }); N("s2", "user.Lib.Sq", { defaults: { v: "7" } });
      F("d", "exit", "cl", "enter"); F("cl", "exit", "a", "enter"); F("a", "exit", "b", "enter"); F("s1", "result", "a", "x"); F("s2", "result", "b", "y");
    });
    const genCode = G.generate(gg, cat2).code;
    const hand = `class Game {
    static void Update() {
    }

    static void Draw() {
        Gfx.Clear(0);
        Lib.Dot(Lib.Sq(10), 20);
        Lib.Dot(150, Lib.Sq(7));
    }
}
`;
    const proj = (name, files) => { const dir = path.join(tmp, name); fs.mkdirSync(dir); for (const [n, t] of Object.entries(files)) fs.writeFileSync(path.join(dir, n), t); return dir; };
    const shot = (name, files) => { const dir = proj(name, files), p = path.join(tmp, name + ".ppm"); const res = run([dir, "--frames", "5", "--ppm", p]); ok(res.status === 0 && fs.existsSync(p), `${name}: the project runs`, res.stdout + res.stderr); return fs.existsSync(p) ? fs.readFileSync(p) : Buffer.alloc(0); };
    const x = shot("own_graph", { "Lib.cs": lib, "Game.cs": genCode }), y = shot("own_hand", { "Lib.cs": lib, "Game.cs": hand });
    ok(x.length > 1000 && x.equals(y), "own nodes: the picture from the graph equals the hand-written one (calls into Lib.cs)");
  }

  // the game stays in code and calls the graph's methods: Update() { Graph.Tick(); }  Draw() { Graph.Paint(); }
  {
    const gm = G.sampleBall();
    gm.className = "Graph";
    for (const n of gm.nodes) {
      if (n.type === "event.Update") { n.type = "event.Method"; n.data = { name: "Tick" }; }
      if (n.type === "event.Draw") { n.type = "event.Method"; n.data = { name: "Paint" }; }
    }
    const gameCs = `class Game {
    static void Update() {
        Graph.Tick();
    }

    static void Draw() {
        Graph.Paint();
    }
}
`;
    const mk2 = (name, files) => { const dir = path.join(tmp, name); fs.mkdirSync(dir); for (const [n, t] of Object.entries(files)) fs.writeFileSync(path.join(dir, n), t); return dir; };
    const shot2 = (name, files) => { const dir = mk2(name, files), p = path.join(tmp, name + ".ppm"); const res = run([dir, "--frames", "40", "--press", "5:Right 8:Down 12:Left", "--ppm", p]); ok(res.status === 0 && fs.existsSync(p), `${name}: the project runs`, res.stdout + res.stderr); return fs.existsSync(p) ? fs.readFileSync(p) : Buffer.alloc(0); };
    const withMethods = G.generate(gm, catalog).code;
    ok(withMethods.includes("static void Tick()") && withMethods.includes("static void Paint()") && !withMethods.includes("static void Update()"), "the ball graph with Metoda nodes has Tick and Paint, no Update");
    const x = shot2("methods_graph", { "Game.cs": gameCs, "Graph.cs": withMethods });
    const y = shot2("methods_hand", { "Game.cs": BALL });
    ok(x.length > 1000 && x.equals(y), "Game.cs calling Graph.Tick()/Graph.Paint(): the same picture as the hand-written ball");
  }

  // objects made from the graph's class: every object has its own variables
  {
    const gi = G.sampleBall();
    gi.className = "Ball"; gi.instance = true;
    for (const n of gi.nodes) {
      if (n.type === "event.Update") { n.type = "event.Method"; n.data = { name: "Tick" }; }
      if (n.type === "event.Draw") { n.type = "event.Method"; n.data = { name: "Paint" }; }
    }
    gi.nodes = gi.nodes.filter((n) => n.id !== "clear" && n.id !== "rgb");                       // Paint only draws the ball
    gi.edges = gi.edges.filter((e) => !["clear", "rgb"].includes(e.from) && !["clear", "rgb"].includes(e.to));
    gi.edges.push({ from: "draw", out: "exit", to: "circle", in: "enter" });
    const ballCs = G.generate(gi, catalog).code;
    const handBall = `class Ball {
    float x = 200;
    float y = 120;

    void Tick() {
        if (Input.Held(Btn.Left)) { x -= 3; }
        if (Input.Held(Btn.Right)) { x += 3; }
        if (Input.Held(Btn.Up)) { y -= 3; }
        if (Input.Held(Btn.Down)) { y += 3; }
        if (Input.Touching()) { x = Input.TouchX() * 400 / 320; y = Input.TouchY(); }
    }

    void Paint() {
        Gfx.Circle(x, y, 14, Color.Yellow);
    }
}
`;
    const gameCs = `class Game {
    static Ball a = new Ball();
    static Ball b = new Ball();

    static void Main() {
        b.x = 100;
        b.y = 60;
    }

    static void Update() {
        a.Tick();
        b.Tick();
    }

    static void Draw() {
        Gfx.Clear(Gfx.Rgb(20, 24, 40));
        a.Paint();
        b.Paint();
    }
}
`;
    const mk3 = (name, files) => { const dir = path.join(tmp, name); fs.mkdirSync(dir); for (const [n, t] of Object.entries(files)) fs.writeFileSync(path.join(dir, n), t); return dir; };
    const shot3 = (name, files) => { const dir = mk3(name, files), p = path.join(tmp, name + ".ppm"); const res = run([dir, "--frames", "40", "--press", "5:Right 8:Down 12:Left", "--ppm", p]); ok(res.status === 0 && fs.existsSync(p), `${name}: the project runs`, res.stdout + res.stderr); return fs.existsSync(p) ? fs.readFileSync(p) : Buffer.alloc(0); };
    ok(ballCs.includes("    float x = 200;") && ballCs.includes("    void Tick() {") && !ballCs.includes("static"), "the Ball graph is a plain class without static");
    const x = shot3("objects_graph", { "Game.cs": gameCs, "Ball.cs": ballCs }), y = shot3("objects_hand", { "Game.cs": gameCs, "Ball.cs": handBall });
    ok(x.length > 1000 && x.equals(y), "two objects made from the graph's class: the same picture as with the hand-written class");
    const single = shot3("objects_one", { "Game.cs": gameCs.replace("        b.Tick();\n", "").replace("        b.Paint();\n", ""), "Ball.cs": handBall });
    ok(!x.equals(single), "…and the second object really is a separate ball (the picture differs from one ball)");
  }

  // hooks: Game.cs has its own Update/Draw and the graph has its own too; the program calls both (files by name)
  {
    const gameCs = `class Game {
    static void Update() {
    }

    static void Draw() {
        Gfx.Rect(0, 0, 30, 30, Color.Red);
    }
}
`;
    const handGraph = BALL.replace("class Game {", "class Graph {");
    const gg = G.sampleBall();
    gg.className = "Graph";
    const genGraph = G.generate(gg, catalog).code;
    const mk4 = (name, files) => { const dir = path.join(tmp, name); fs.mkdirSync(dir); for (const [n, t] of Object.entries(files)) fs.writeFileSync(path.join(dir, n), t); return dir; };
    const shot4 = (name, files) => { const dir = mk4(name, files), p = path.join(tmp, name + ".ppm"); const res = run([dir, "--frames", "40", "--press", "5:Right 8:Down 12:Left", "--ppm", p]); ok(res.status === 0 && fs.existsSync(p), `${name}: the project runs`, res.stdout + res.stderr); return fs.existsSync(p) ? fs.readFileSync(p) : Buffer.alloc(0); };
    ok(genGraph.includes("static void Update()") && genGraph.includes("static void Draw()"), "the graph keeps its own Update and Draw events");
    const x = shot4("hooks_graph", { "Game.cs": gameCs, "Graph.cs": genGraph }), y = shot4("hooks_hand", { "Game.cs": gameCs, "Graph.cs": handGraph });
    ok(x.length > 1000 && x.equals(y), "Game.cs and the graph both with Update/Draw: the same picture as two hand-written classes");
  }
  fs.rmSync(tmp, { recursive: true, force: true });
}

console.log(`\ngraph.js: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
