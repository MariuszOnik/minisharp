// Checks of web/przewodnik.html (the guide): links work, nothing is loaded from outside, and every button name the guide
// puts in a box exists in the app, so the guide and the page cannot drift apart unnoticed.  Run:  node tests/web/guide.test.js
const fs = require("fs"), path = require("path");
const root = path.join(__dirname, "..", "..", "web");
const guide = fs.readFileSync(path.join(root, "przewodnik.html"), "utf8");
const app = ["index.html", "app.js", "grapheditor.js", "graph.js"].map((f) => fs.readFileSync(path.join(root, f), "utf8")).join("\n");
let pass = 0, fail = 0;
const ok = (c, m, x) => { c ? pass++ : fail++; console.log((c ? "PASS " : "FAIL ") + m + (c ? "" : "  -> " + JSON.stringify(x))); };

const ids = new Set([...guide.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
const links = [...guide.matchAll(/href="#([^"]+)"/g)].map((m) => m[1]);
ok(links.length > 10 && links.every((l) => ids.has(l)), `every in-page link has a target (${links.length} links)`, links.filter((l) => !ids.has(l)));
ok(![...guide.matchAll(/(?:src|href)="(https?:)?\/\/[^"]+"/g)].length, "nothing is loaded from other sites");
ok(!/%%/.test(guide) && !/\{\{/.test(guide), "no unfilled placeholders");
const imgs = [...guide.matchAll(/<img [^>]*>/g)].map((m) => m[0]);
ok(imgs.length >= 8 && imgs.every((t) => /\balt="[^"]{8,}"/.test(t) && /\bwidth="\d+"/.test(t) && /\bheight="\d+"/.test(t)), `${imgs.length} images, all with alt text and size`);
ok(guide.length < 400 * 1024, `the page is ${Math.round(guide.length / 1024)} KB (one file, works offline)`);

// button names in boxes must exist in the app (examples that depend on the project or the file are listed here)
const dynamic = new Set(["Mój projekt ▾", "Kulka.cs ▾", "⚠", "Uruchom", "Stop"]);
const names = [...new Set([...guide.matchAll(/<span class="ui">([^<]+)<\/span>/g)].map((m) => m[1].replace(/&gt;/g, ">").replace(/&amp;/g, "&").trim()))];
const missing = names.filter((n) => !dynamic.has(n) && !app.includes(n));
ok(names.length > 20 && !missing.length, `${names.length} button names from the guide exist in the app`, missing);
for (const n of ["Uruchom", "Stop"]) ok(app.includes(n), `"${n}" exists in the app`);

// the headings of the menu items the guide describes
for (const label of ["Zmienne grafu", "Dopasuj widok", "Uwagi", "Pokaż wygenerowany kod", "Zamień na zwykły kod", "Zmień na klasę z obiektami", "Plik z kodem", "Plik z grafem", "Metoda (wołana z kodu)"])
  ok(app.includes(label), `menu item "${label}" exists in the app`);
console.log(`\nguide: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
