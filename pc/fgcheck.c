/* MiniSharp - checks the C port of the graph generator (src/fgraph.c) against fixtures written by the browser
   tests (MS_DUMP_GRAPHS=dir node tests/web/graph.test.js): `minisharp --fgraph-check dir`.
   Also `minisharp --fgraph graph.json` prints the code made from a graph. */
#include "../src/fgraph.h"
#include "../source3ds/help_data.h"
#include "fgcheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define NHELP ((int)(sizeof ms_help / sizeof ms_help[0]))

static char* slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* s = (char*)malloc((size_t)n + 1);
    if (s) { n = (long)fread(s, 1, (size_t)n, f); s[n] = 0; }
    fclose(f);
    return s;
}

static const char* jstr(const JVal* o, const char* k) {
    const JVal* v = json_get(o, k);
    return v && v->t == JS_STR ? v->s : NULL;
}
static int same(const char* a, const char* b) { return (!a && !b) || (a && b && !strcmp(a, b)); }

static int fails;
static void bad(const char* file, const char* what, const char* got, const char* want) {
    fails++;
    printf("FAIL %s: %s\n  got:  %s\n  want: %s\n", file, what, got ? got : "(none)", want ? want : "(none)");
}

static int cmp_msgs(const char* file, const char* what, const FMsg* m, int n, const JVal* want) {
    int wn = want && want->t == JS_ARR ? want->n : 0, ok = 1;
    if (n != wn) {
        char g[32], w[32]; snprintf(g, sizeof g, "%d", n); snprintf(w, sizeof w, "%d", wn);
        bad(file, what, g, w);
        for (int i = 0; i < n; i++) printf("    got:  %s %s\n", m[i].node ? m[i].node : "-", m[i].message);
        for (int i = 0; i < wn; i++) printf("    want: %s %s\n", jstr(want->items[i], "node") ? jstr(want->items[i], "node") : "-", jstr(want->items[i], "message"));
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (!same(m[i].node, jstr(want->items[i], "node"))) { bad(file, what, m[i].node, jstr(want->items[i], "node")); ok = 0; }
        if (!same(m[i].message, jstr(want->items[i], "message"))) { bad(file, what, m[i].message, jstr(want->items[i], "message")); ok = 0; }
    }
    return ok;
}

static void cmp_ports(const char* file, const char* id, const char* what, const FPort* ps, int n, const JVal* want) {
    int wn = want && want->t == JS_ARR ? want->n : 0;
    char w[300];
    if (n != wn) { snprintf(w, sizeof w, "%s %s count %d", id, what, wn); char g[32]; snprintf(g, sizeof g, "%d", n); bad(file, w, g, w); return; }
    for (int i = 0; i < n; i++) {
        const JVal* p = want->items[i];
        snprintf(w, sizeof w, "%s %s[%d]", id, what, i);
        if (!same(ps[i].name, jstr(p, "name")) || !same(ps[i].type, jstr(p, "type")) || !same(ps[i].def, jstr(p, "def")) ||
            !!ps[i].flow != json_truthy(json_get(p, "flow"))) {
            char g[300], ww[300];
            snprintf(g, sizeof g, "%s:%s:%s:%d", ps[i].name, ps[i].type ? ps[i].type : "-", ps[i].def ? ps[i].def : "-", ps[i].flow);
            snprintf(ww, sizeof ww, "%s:%s:%s:%d", jstr(p, "name"), jstr(p, "type") ? jstr(p, "type") : "-", jstr(p, "def") ? jstr(p, "def") : "-", json_truthy(json_get(p, "flow")));
            bad(file, w, g, ww);
        }
    }
}

static void cmp_def(const char* file, const FDef* d, const JVal* w) {
    const char* id = d->id;
    const char* fields[][2] = { { "title", d->title }, { "category", d->category }, { "kind", d->kind }, { "callee", d->callee }, { "ret", d->ret } };
    for (int i = 0; i < 5; i++)
        if (!same(fields[i][1], jstr(w, fields[i][0]))) { char what[200]; snprintf(what, sizeof what, "%s.%s", id, fields[i][0]); bad(file, what, fields[i][1], jstr(w, fields[i][0])); }
    cmp_ports(file, id, "inputs", d->in, d->nin, json_get(w, "inputs"));
    cmp_ports(file, id, "outputs", d->out, d->nout, json_get(w, "outputs"));
    const JVal* ps = json_get(w, "params");
    int wn = ps && ps->t == JS_ARR ? ps->n : 0;
    if (wn != d->nparams) { char what[200]; snprintf(what, sizeof what, "%s params", id); bad(file, what, "count differs", ""); return; }
    for (int i = 0; i < wn; i++)
        if (!same(d->params[i].type, jstr(ps->items[i], "type")) || !same(d->params[i].name, jstr(ps->items[i], "name")) ||
            !same(d->params[i].port, jstr(ps->items[i], "port"))) {
            char what[200]; snprintf(what, sizeof what, "%s params[%d]", id, i);
            bad(file, what, d->params[i].name, jstr(ps->items[i], "name"));
        }
}

static void check_catalog(const char* file, const JVal* list) {
    FCatalog c;
    fg_catalog_build(&c, (const FHelpEntry*)ms_help, NHELP, NULL);
    if (c.n != list->n) { char g[32], w[32]; snprintf(g, sizeof g, "%d", c.n); snprintf(w, sizeof w, "%d", list->n); bad(file, "catalog size", g, w); }
    for (int i = 0; i < c.n && i < list->n; i++) {
        if (!same(c.d[i].id, jstr(list->items[i], "id"))) { bad(file, "catalog order", c.d[i].id, jstr(list->items[i], "id")); break; }
        cmp_def(file, &c.d[i], list->items[i]);
    }
    fg_catalog_free(&c);
}

static void check_generate(const char* file, const JVal* fx) {
    FCatalog user = { 0 }, c;
    fg_defs_from_json(&user, json_get(fx, "user"));
    fg_catalog_build(&c, (const FHelpEntry*)ms_help, NHELP, &user);
    FGenResult r;
    fg_generate(json_get(fx, "graph"), &c, json_truthy(json_get(fx, "embed")), &r);
    if (!same(r.code, jstr(fx, "code"))) bad(file, "code", r.code, jstr(fx, "code"));
    cmp_msgs(file, "warnings", r.warnings, r.nwarnings, json_get(fx, "warnings"));
    cmp_msgs(file, "errors", r.errors, r.nerrors, json_get(fx, "errors"));
    fg_result_free(&r);
    fg_catalog_free(&c);
    fg_catalog_free(&user);
}

static void check_user_nodes(const char* file, const JVal* fx) {
    FCatalog defs = { 0 };
    const JVal* files = json_get(fx, "files");
    for (int i = 0; files && i < files->n; i++) fg_user_nodes(&defs, jstr(files->items[i], "name"), jstr(files->items[i], "text"));
    const JVal* want = json_get(fx, "defs");
    if (defs.n != want->n) { char g[32], w[32]; snprintf(g, sizeof g, "%d", defs.n); snprintf(w, sizeof w, "%d", want->n); bad(file, "user node count", g, w); }
    for (int i = 0; i < defs.n && i < want->n; i++) {
        if (!same(defs.d[i].id, jstr(want->items[i], "id"))) { bad(file, "user node", defs.d[i].id, jstr(want->items[i], "id")); continue; }
        cmp_def(file, &defs.d[i], want->items[i]);
        if (!same(defs.d[i].doc, jstr(want->items[i], "doc"))) bad(file, "user node doc", defs.d[i].doc, jstr(want->items[i], "doc"));
    }
    fg_catalog_free(&defs);
}

static int cmpname(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }

int ms_fgraph_check(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) { fprintf(stderr, "cannot open %s\n", dir); return 2; }
    char* names[2048]; int n = 0;
    struct dirent* e;
    while ((e = readdir(d)) && n < 2048) {
        size_t L = strlen(e->d_name);
        if (L > 5 && !strcmp(e->d_name + L - 5, ".json")) { names[n] = (char*)malloc(L + 1); strcpy(names[n++], e->d_name); }
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(char*), cmpname);
    int checked = 0;
    for (int i = 0; i < n; i++) {
        char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        char* text = slurp(path);
        char err[200];
        JVal* fx = text ? json_parse(text, err, sizeof err) : NULL;
        if (!fx) { bad(names[i], "fixture", "cannot read", text ? err : "file"); free(text); free(names[i]); continue; }
        if (!strcmp(names[i], "catalog.json")) check_catalog(names[i], fx);
        else if (names[i][0] == 'g') check_generate(names[i], fx);
        else if (names[i][0] == 'u') check_user_nodes(names[i], fx);
        checked++;
        json_free(fx); free(text); free(names[i]);
    }
    printf("fgraph: %d fixtures, %s\n", checked, fails ? "FAILED" : "all the same as the browser");
    return fails ? 1 : 0;
}

int ms_fgraph_print(const char* path) {
    char* text = slurp(path);
    if (!text) { fprintf(stderr, "cannot read %s\n", path); return 2; }
    char err[200];
    JVal* g = json_parse(text, err, sizeof err);
    if (!g) g = fg_extract(text);
    free(text);
    if (!g) { fprintf(stderr, "%s: not a graph (%s)\n", path, err); return 1; }
    FCatalog c;
    fg_catalog_build(&c, (const FHelpEntry*)ms_help, NHELP, NULL);
    FGenResult r;
    fg_generate(g, &c, 1, &r);
    fputs(r.code, stdout);
    for (int i = 0; i < r.nwarnings; i++) fprintf(stderr, "warning: %s\n", r.warnings[i].message);
    for (int i = 0; i < r.nerrors; i++) fprintf(stderr, "error: %s\n", r.errors[i].message);
    int bad = r.nerrors > 0;
    fg_result_free(&r); fg_catalog_free(&c); json_free(g);
    return bad;
}
