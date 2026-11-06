/* MiniSharp - flow graphs (see fgraph.h). This is web/graph.js in C: keep the two in step
   (tests/fgraph checks that both give the same code, warnings and errors for every graph of the browser tests). */
#include "fgraph.h"
#include "printer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <setjmp.h>

/* ---------------------------------------------------------------- small helpers */
static char* sdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* d = (char*)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}
static char* sdupn(const char* s, int n) {
    char* d = (char*)malloc((size_t)n + 1);
    if (d) { memcpy(d, s, (size_t)n); d[n] = 0; }
    return d;
}
static char* sfmt(const char* f, ...) {
    va_list ap; va_start(ap, f);
    int n = vsnprintf(NULL, 0, f, ap);
    va_end(ap);
    char* s = (char*)malloc((size_t)n + 1);
    if (!s) return NULL;
    va_start(ap, f);
    vsnprintf(s, (size_t)n + 1, f, ap);
    va_end(ap);
    return s;
}
static int is_ident(const char* s) {                 /* /^[A-Za-z_]\w*$/ */
    if (!s || !(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (s++; *s; s++) if (!(isalnum((unsigned char)*s) || *s == '_')) return 0;
    return 1;
}
static int streq(const char* a, const char* b) { return a && b && !strcmp(a, b); }

/* String(v) of JavaScript, with "undefined" for a missing value (into a ring of buffers) */
static const char* S(const JVal* v) {
    static char ring[8][64];
    static int k;
    char* buf = ring[k++ & 7];
    if (!v) return "undefined";
    if (v->t == JS_NULL) return "null";
    const char* t = json_text(v, buf, 64);
    return t ? t : "null";
}
/* IDENT.test(v || "") */
static int ident_v(const JVal* v) { return json_truthy(v) && is_ident(S(v)); }

/* ---------------------------------------------------------------- the catalog */
static FPort port(const char* name, const char* type, const char* def, int flow) {
    FPort p = { sdup(name), sdup(type), sdup(def), flow };
    return p;
}
#define FLOW(n) port(n, NULL, NULL, 1)
#define VAL(n, t, d) port(n, t, d, 0)

static void free_def(FDef* d) {
    char** ss[] = { &d->id, &d->title, &d->category, &d->kind, &d->doc, &d->sig, &d->callee, &d->ret, &d->op, &d->symbol, &d->method, &d->file };
    for (size_t i = 0; i < sizeof ss / sizeof ss[0]; i++) { free(*ss[i]); *ss[i] = NULL; }
    for (int i = 0; i < d->nin; i++) { free(d->in[i].name); free(d->in[i].type); free(d->in[i].def); }
    for (int i = 0; i < d->nout; i++) { free(d->out[i].name); free(d->out[i].type); free(d->out[i].def); }
    for (int i = 0; i < d->nparams; i++) { free(d->params[i].type); free(d->params[i].name); free(d->params[i].port); }
    free(d->in); free(d->out); free(d->params);
    memset(d, 0, sizeof *d);
}

void fg_catalog_free(FCatalog* c) {
    for (int i = 0; i < c->n; i++) free_def(&c->d[i]);
    free(c->d);
    memset(c, 0, sizeof *c);
}

const FDef* fg_find(const FCatalog* c, const char* id) {
    if (!id) return NULL;
    for (int i = 0; i < c->n; i++) if (!strcmp(c->d[i].id, id)) return &c->d[i];
    return NULL;
}

/* types.set(id, d): a new id goes to the end, a known one is replaced in its place (takes d) */
static void cat_set(FCatalog* c, FDef* d) {
    for (int i = 0; i < c->n; i++) if (!strcmp(c->d[i].id, d->id)) { free_def(&c->d[i]); c->d[i] = *d; return; }
    if (c->n == c->cap) {
        int nc = c->cap ? c->cap * 2 : 64;
        FDef* p = (FDef*)realloc(c->d, (size_t)nc * sizeof(FDef));
        if (!p) { free_def(d); return; }
        c->d = p; c->cap = nc;
    }
    c->d[c->n++] = *d;
}

static void add_ports(FPort** arr, int* n, const FPort* ps, int k) {
    *arr = (FPort*)realloc(*arr, (size_t)(*n + k) * sizeof(FPort));
    memcpy(*arr + *n, ps, (size_t)k * sizeof(FPort));
    *n += k;
}

static FDef mk(const char* id, const char* title, const char* category, const char* kind) {
    FDef d; memset(&d, 0, sizeof d);
    d.id = sdup(id); d.title = sdup(title); d.category = sdup(category); d.kind = sdup(kind);
    return d;
}

static const char* default_for(const char* type) {
    if (!type) return "null";
    if (!strcmp(type, "int") || !strcmp(type, "float") || !strcmp(type, "double") || !strcmp(type, "long")) return "0";
    if (!strcmp(type, "bool")) return "false";
    if (!strcmp(type, "string") || !strcmp(type, "object")) return "\"\"";
    if (!strcmp(type, "char")) return "' '";
    return "null";
}

static char* trim_copy(const char* s, int n) {
    while (n > 0 && isspace((unsigned char)*s)) { s++; n--; }
    while (n > 0 && isspace((unsigned char)s[n - 1])) n--;
    return sdupn(s, n);
}

/* parseParams: "float x, List<int> xs" -> [{type, name}] (split at top-level commas, the last word is the name) */
static void parse_params(const char* text, FParam** out, int* n) {
    *out = NULL; *n = 0;
    int depth = 0;
    const char* start = text;
    for (const char* p = text; ; p++) {
        if (*p == '<' || *p == '(' || *p == '[') depth++;
        else if (*p == '>' || *p == ')' || *p == ']') depth--;
        if ((*p == ',' && depth == 0) || !*p) {
            char* piece = trim_copy(start, (int)(p - start));
            if (piece && (*p || piece[0])) {                     /* the last piece only when it is not empty */
                char* last = piece + strlen(piece);
                while (last > piece && !isspace((unsigned char)last[-1])) last--;
                char* typeEnd = last;
                while (typeEnd > piece && isspace((unsigned char)typeEnd[-1])) typeEnd--;
                /* the type is the words before the name, joined by single spaces */
                char type[160]; int tn = 0, sp = 0;
                for (char* q = piece; q < typeEnd && tn < (int)sizeof type - 1; q++) {
                    if (isspace((unsigned char)*q)) { sp = 1; continue; }
                    if (sp && tn) type[tn++] = ' ';
                    sp = 0;
                    type[tn++] = *q;
                }
                type[tn] = 0;
                *out = (FParam*)realloc(*out, (size_t)(*n + 1) * sizeof(FParam));
                (*out)[*n].type = sdup(type); (*out)[*n].name = sdup(last); (*out)[*n].port = NULL;
                (*n)++;
            }
            free(piece);
            if (!*p) break;
            start = p + 1;
        }
    }
}

static const struct { const char *id, *sym, *def, *title; int prec; } BINARY[] = {
    { "op.add", "+", "0", "Dodaj (+)", 70 }, { "op.sub", "-", "0", "Odejmij (-)", 70 },
    { "op.mul", "*", "1", "Pomnóż (*)", 80 }, { "op.div", "/", "1", "Podziel (/)", 80 }, { "op.mod", "%", "1", "Reszta (%)", 80 },
    { "cmp.lt", "<", "0", "Mniejsze (<)", 60 }, { "cmp.le", "<=", "0", "Mniejsze lub równe (<=)", 60 },
    { "cmp.gt", ">", "0", "Większe (>)", 60 }, { "cmp.ge", ">=", "0", "Większe lub równe (>=)", 60 },
    { "cmp.eq", "==", "0", "Równe (==)", 50 }, { "cmp.ne", "!=", "0", "Różne (!=)", 50 },
    { "logic.and", "&&", "false", "I (&&)", 40 }, { "logic.or", "||", "false", "Lub (||)", 30 },
};
#define NBINARY ((int)(sizeof BINARY / sizeof BINARY[0]))
static int binary_index(const char* op) {
    for (int i = 0; i < NBINARY; i++) if (streq(BINARY[i].id, op)) return i;
    return -1;
}

static void add_event(FCatalog* c, const char* id, const char* title, const char* method, int custom, const char* doc) {
    FDef d = mk(id, title, "Zdarzenia", "event");
    d.method = sdup(method); d.custom = custom; d.doc = sdup(doc);
    FPort o = FLOW("exit");
    add_ports(&d.out, &d.nout, &o, 1);
    cat_set(c, &d);
}

/* one function signature "ret Name(params)" -> ret, params; 0 if it does not look like one */
static int split_fn_sig(const char* sig, char** ret, char** name, char** params) {
    const char* p = sig;
    if (!*p || isspace((unsigned char)*p)) return 0;
    while (*p && !isspace((unsigned char)*p)) p++;
    const char* retEnd = p;
    if (!isspace((unsigned char)*p)) return 0;
    while (isspace((unsigned char)*p)) p++;
    const char* ns = p;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    if (p == ns || *p != '(') return 0;
    const char* ne = p;
    const char* close = NULL;
    for (const char* q = p + 1; *q; q++) {
        if (*q == '\n') return 0;
        if (*q == ')') { const char* r = q + 1; while (*r == ' ' || *r == '\t' || *r == '\r') r++; if (!*r) close = q; }
    }
    if (!close) return 0;
    *ret = sdupn(sig, (int)(retEnd - sig));
    *name = sdupn(ns, (int)(ne - ns));
    *params = sdupn(p + 1, (int)(close - p - 1));
    return 1;
}

typedef struct { char* key; int count; const FHelpEntry* e; char* ret; FParam* params; int nparams; } Fn;

void fg_catalog_build(FCatalog* c, const FHelpEntry* help, int nhelp, const FCatalog* user) {
    memset(c, 0, sizeof *c);
    add_event(c, "event.Main", "Start (Main)", "Main", 0,
              "Wykonuje się raz na starcie. Każda klasa i każdy graf w projekcie może mieć własny Start: wykonują się po kolei.");
    add_event(c, "event.Update", "Co klatkę (Update)", "Update", 0,
              "Wykonuje się co klatkę. Każda klasa i każdy graf może mieć własny Update: wszystkie się wykonują, w kolejności plików w projekcie (po nazwie).");
    add_event(c, "event.Draw", "Rysowanie (Draw)", "Draw", 0,
              "Wykonuje się co klatkę po Update, do rysowania. Każdy graf i klasa może mieć własny Draw; późniejszy plik rysuje na wierzchu.");
    add_event(c, "event.Method", "Metoda (wołana z kodu)", NULL, 1,
              "Własna metoda grafu, bez parametrów, do wołania z kodu albo z innego grafu jako Klasa.Nazwa(), np. Graph.Tick(). Zmienne grafu to pola tej samej klasy.");
    {
        FDef d = mk("if", "Jeśli (if)", "Sterowanie", "if");
        FPort in[] = { FLOW("enter"), VAL("cond", "bool", "false") }, out[] = { FLOW("true"), FLOW("false"), FLOW("exit") };
        add_ports(&d.in, &d.nin, in, 2); add_ports(&d.out, &d.nout, out, 3); cat_set(c, &d);
    }
    {
        FDef d = mk("for", "Powtórz (for)", "Sterowanie", "for");
        FPort in[] = { FLOW("enter"), VAL("first", "int", "0"), VAL("last", "int", "10"), VAL("step", "int", "1") };
        FPort out[] = { FLOW("body"), FLOW("exit"), VAL("index", "int", NULL) };
        add_ports(&d.in, &d.nin, in, 4); add_ports(&d.out, &d.nout, out, 3); cat_set(c, &d);
    }
    {
        FDef d = mk("while", "Dopóki (while)", "Sterowanie", "while");
        FPort in[] = { FLOW("enter"), VAL("cond", "bool", "false") }, out[] = { FLOW("body"), FLOW("exit") };
        add_ports(&d.in, &d.nin, in, 2); add_ports(&d.out, &d.nout, out, 2); cat_set(c, &d);
    }
    {
        FDef d = mk("var.set", "Ustaw zmienną", "Zmienne", "setvar");
        FPort in[] = { FLOW("enter"), VAL("value", "", "0") }, out[] = { FLOW("exit") };
        add_ports(&d.in, &d.nin, in, 2); add_ports(&d.out, &d.nout, out, 1); cat_set(c, &d);
    }
    {
        FDef d = mk("var.get", "Pobierz zmienną", "Zmienne", "getvar");
        FPort out[] = { VAL("value", "", NULL) };
        add_ports(&d.out, &d.nout, out, 1); cat_set(c, &d);
    }
    {
        FDef d = mk("lit", "Wartość", "Zmienne", "lit");
        FPort out[] = { VAL("value", "", NULL) };
        add_ports(&d.out, &d.nout, out, 1); cat_set(c, &d);
    }
    for (int i = 0; i < NBINARY; i++) {
        int logic = !strncmp(BINARY[i].id, "logic.", 6), cmp = !strncmp(BINARY[i].id, "cmp.", 4);
        FDef d = mk(BINARY[i].id, BINARY[i].title, logic ? "Logika" : cmp ? "Porównania" : "Matematyka", "binary");
        d.op = sdup(BINARY[i].id);
        FPort in[] = { VAL("a", "", BINARY[i].def), VAL("b", "", BINARY[i].def) }, out[] = { VAL("result", cmp || logic ? "bool" : "", NULL) };
        add_ports(&d.in, &d.nin, in, 2); add_ports(&d.out, &d.nout, out, 1); cat_set(c, &d);
    }
    {
        FDef d = mk("logic.not", "Nie (!)", "Logika", "unary");
        d.symbol = sdup("!");
        FPort in[] = { VAL("a", "bool", "false") }, out[] = { VAL("result", "bool", NULL) };
        add_ports(&d.in, &d.nin, in, 1); add_ports(&d.out, &d.nout, out, 1); cat_set(c, &d);
    }
    {
        FDef d = mk("op.neg", "Przeciwna (-)", "Matematyka", "unary");
        d.symbol = sdup("-");
        FPort in[] = { VAL("a", "", "0") }, out[] = { VAL("result", "", NULL) };
        add_ports(&d.in, &d.nin, in, 1); add_ports(&d.out, &d.nout, out, 1); cat_set(c, &d);
    }

    /* the API: every constant, constructor and function of the Help */
    Fn* fns = NULL; int nfns = 0;
    for (int i = 0; i < nhelp; i++) {
        const FHelpEntry* e = &help[i];
        const char* sig = e->sig ? e->sig : "";
        char *ret, *name, *params;
        if (!strncmp(sig, "const ", 6)) {                         /* const TYPE NAME = ... */
            const char* t = sig + 6, *te = t;
            while (*te && !isspace((unsigned char)*te)) te++;
            const char* nm = te + 1, *ne = nm;
            while (isalnum((unsigned char)*ne) || *ne == '_') ne++;
            if (te > t && *te == ' ' && ne > nm && !strncmp(ne, " = ", 3)) {
                char* id = sfmt("api.%s.%s", e->group, e->name);
                char* title = sfmt("%s.%s", e->group, e->name);
                FDef d = mk(id, title, e->group, "const");
                free(id); free(title);
                d.callee = sdup(e->insert); d.sig = sdup(sig); d.doc = sdup(e->doc);
                char* type = sdupn(t, (int)(te - t));
                FPort out[] = { VAL("value", type, NULL) };
                free(type);
                add_ports(&d.out, &d.nout, out, 1);
                cat_set(c, &d);
                continue;
            }
        }
        if (!strncmp(sig, "new ", 4)) {                           /* new Name(params) */
            const char* nm = sig + 4, *ne = nm;
            while (isalnum((unsigned char)*ne) || *ne == '_') ne++;
            size_t L = strlen(sig);
            if (ne > nm && *ne == '(' && L > 0 && sig[L - 1] == ')' && !strchr(sig, '\n')) {
                char* cls = sdupn(nm, (int)(ne - nm));
                char* inner = sdupn(ne + 1, (int)(sig + L - 1 - (ne + 1)));
                char* id = sfmt("api.new.%s", cls), *title = sfmt("new %s", cls);
                FDef d = mk(id, title, e->group, "new");
                free(id); free(title);
                d.callee = sfmt("new %s", cls); d.sig = sdup(sig); d.doc = sdup(e->doc);
                parse_params(inner, &d.params, &d.nparams); d.hasParams = 1;
                for (int k = 0; k < d.nparams; k++) { FPort p = VAL(d.params[k].name, d.params[k].type, default_for(d.params[k].type)); add_ports(&d.in, &d.nin, &p, 1); }
                FPort out[] = { VAL("value", cls, NULL) };
                add_ports(&d.out, &d.nout, out, 1);
                free(cls); free(inner);
                cat_set(c, &d);
                continue;
            }
        }
        if (split_fn_sig(sig, &ret, &name, &params)) {
            FParam* ps; int np;
            parse_params(params, &ps, &np);
            free(params); free(name);
            char* key = sfmt("%s.%s", e->group, e->name);
            int known = 0;
            for (int k = 0; k < nfns; k++) if (!strcmp(fns[k].key, key) && fns[k].count == np) known = 1;
            if (known) {
                free(key); free(ret);
                for (int k = 0; k < np; k++) { free(ps[k].type); free(ps[k].name); }
                free(ps);
                continue;
            }
            fns = (Fn*)realloc(fns, (size_t)(nfns + 1) * sizeof(Fn));
            fns[nfns].key = key; fns[nfns].count = np; fns[nfns].e = e; fns[nfns].ret = ret; fns[nfns].params = ps; fns[nfns].nparams = np;
            nfns++;
        }
    }
    /* the functions, by key in order of first appearance; overloads with another parameter count get "/count" */
    for (int i = 0; i < nfns; i++) {
        int first = 1, many = 0;
        for (int k = 0; k < i; k++) if (!strcmp(fns[k].key, fns[i].key)) first = 0;
        if (!first) continue;
        for (int k = 0; k < nfns; k++) if (k != i && !strcmp(fns[k].key, fns[i].key)) many = 1;
        for (int k = i; k < nfns; k++) {
            if (strcmp(fns[k].key, fns[i].key)) continue;
            Fn* f = &fns[k];
            char* id = many ? sfmt("api.%s/%d", f->key, f->count) : sfmt("api.%s", f->key);
            char* title = many ? sfmt("%s (%d)", f->key, f->count) : sdup(f->key);
            FDef d = mk(id, title, f->e->group, "call");
            free(id); free(title);
            const char* ins = f->e->insert ? f->e->insert : "";
            size_t il = strlen(ins);
            d.callee = il && ins[il - 1] == '(' ? sdupn(ins, (int)il - 1) : sdup(ins);
            d.ret = sdup(f->ret); d.sig = sdup(f->e->sig); d.doc = sdup(f->e->doc);
            d.params = f->params; d.nparams = f->nparams; d.hasParams = 1;
            f->params = NULL;
            FPort enter = FLOW("enter");
            add_ports(&d.in, &d.nin, &enter, 1);
            for (int q = 0; q < d.nparams; q++) { FPort p = VAL(d.params[q].name, d.params[q].type, default_for(d.params[q].type)); add_ports(&d.in, &d.nin, &p, 1); }
            FPort ex = FLOW("exit");
            add_ports(&d.out, &d.nout, &ex, 1);
            if (strcmp(f->ret, "void")) { FPort r = VAL("result", f->ret, NULL); add_ports(&d.out, &d.nout, &r, 1); }
            cat_set(c, &d);
        }
    }
    for (int i = 0; i < nfns; i++) { free(fns[i].key); free(fns[i].ret); free(fns[i].params); }
    free(fns);
    /* the user's nodes (a copy of each new id) */
    if (user)
        for (int i = 0; i < user->n; i++) {
            const FDef* u = &user->d[i];
            if (fg_find(c, u->id)) continue;
            FDef d; memset(&d, 0, sizeof d);
            d.id = sdup(u->id); d.title = sdup(u->title); d.category = sdup(u->category); d.kind = sdup(u->kind);
            d.doc = sdup(u->doc); d.sig = sdup(u->sig); d.callee = sdup(u->callee); d.ret = sdup(u->ret); d.op = sdup(u->op);
            d.symbol = sdup(u->symbol); d.method = sdup(u->method); d.file = sdup(u->file);
            d.custom = u->custom; d.user = u->user; d.hasParams = u->hasParams;
            for (int k = 0; k < u->nin; k++) { FPort p = port(u->in[k].name, u->in[k].type, u->in[k].def, u->in[k].flow); add_ports(&d.in, &d.nin, &p, 1); }
            for (int k = 0; k < u->nout; k++) { FPort p = port(u->out[k].name, u->out[k].type, u->out[k].def, u->out[k].flow); add_ports(&d.out, &d.nout, &p, 1); }
            if (u->nparams) {
                d.params = (FParam*)calloc((size_t)u->nparams, sizeof(FParam));
                for (int k = 0; k < u->nparams; k++) { d.params[k].type = sdup(u->params[k].type); d.params[k].name = sdup(u->params[k].name); d.params[k].port = sdup(u->params[k].port); }
                d.nparams = u->nparams;
            }
            cat_set(c, &d);
        }
}

/* ---------------------------------------------------------------- the user's nodes from code */
/* the comment lines right above position `at` of src, without the slashes and xml tags (docAbove of complete.js) */
static int doc_above(const char* src, const char* at, char lines[][160], int max) {
    const char* ls = at;
    while (ls > src && ls[-1] != '\n') ls--;
    int n = 0;
    char tmp[32][160]; int nt = 0;
    const char* end = ls;
    while (end > src && nt < 32) {
        const char* prevEnd = end - 1;
        const char* s = prevEnd;
        while (s > src && s[-1] != '\n') s--;
        const char* a = s, *b = prevEnd;
        while (a < b && isspace((unsigned char)*a)) a++;
        while (b > a && isspace((unsigned char)b[-1])) b--;
        if (b - a < 2 || a[0] != '/' || a[1] != '/') break;
        a += 2;
        if (a < b && *a == '/') a++;
        if (a < b && isspace((unsigned char)*a)) a++;
        char line[160]; int k = 0;
        for (const char* p = a; p < b && k < (int)sizeof line - 1; p++) {
            if (*p == '<') {                                 /* drop <tag> and </tag> */
                const char* q = p + 1;
                if (*q == '/') q++;
                const char* w = q;
                while (isalnum((unsigned char)*q) || *q == '_') q++;
                if (q > w && *q == '>') { p = q; continue; }
            }
            line[k++] = *p;
        }
        line[k] = 0;
        char* t = line; while (isspace((unsigned char)*t)) t++;
        int L = (int)strlen(t); while (L > 0 && isspace((unsigned char)t[L - 1])) t[--L] = 0;
        snprintf(tmp[nt++], 160, "%s", t);
        end = s;
    }
    for (int i = nt - 1; i >= 0 && n < max; i--) if (tmp[i][0]) snprintf(lines[n++], 160, "%s", tmp[i]);
    return n;
}

void fg_user_nodes(FCatalog* out, const char* fileName, const char* src) {
    for (const char* p = src; (p = strstr(p, "// @graph ")) != NULL; p++)
        if (p == src || p[-1] == '\n') return;                     /* a generated file: its classes only hold events */
    Arena A = { 0 };
    char err[200];
    jmp_buf jb;
    ParseCtx pc = { &A, &jb, err, sizeof err };
    ProgramAST* volatile pr = NULL;                         /* volatile: set between setjmp and a possible longjmp */
    if (!setjmp(jb)) { int n; Token* t = ms_lex(&pc, src, &n); pr = ms_parse(&pc, t, n); }
    if (!pr) { arena_free(&A); return; }
    for (int c = 0; c < pr->nclasses; c++) {
        ClassDecl* cd = &pr->classes[c];
        if (cd->isInterface) continue;
        for (int m = 0; m < cd->nmethods; m++) {
            MethodDecl* md = &cd->methods[m];
            if (!md->isStatic || md->isCtor || !md->s0) continue;
            char mname[80]; snprintf(mname, sizeof mname, "%.*s", md->nlen, md->name);
            if (!strcmp(mname, "Main") || !strcmp(mname, "Update") || !strcmp(mname, "Draw")) continue;
            char cname[80]; snprintf(cname, sizeof cname, "%.*s", cd->nlen, cd->name);
            char* title = sfmt("%s.%s", cname, mname);
            int hide = 0;
            char lines[24][160];
            int nl = doc_above(src, md->s0, lines, 24);
            MsBuf doc = { 0 };
            for (int i = 0; i < nl; i++) {
                const char* l = lines[i];
                if (!strncmp(l, "@node", 5) && !(isalnum((unsigned char)l[5]) || l[5] == '_')) {
                    const char* t = l + 5;
                    while (isspace((unsigned char)*t)) t++;
                    char* tt = trim_copy(t, (int)strlen(t));
                    if (tt[0] && tolower((unsigned char)tt[0]) == 'h' && tolower((unsigned char)tt[1]) == 'i' && tolower((unsigned char)tt[2]) == 'd' && tolower((unsigned char)tt[3]) == 'e' && !tt[4]) hide = 1;   /* /^hide$/i */
                    else if (tt[0]) { free(title); title = sdup(tt); }
                    free(tt);
                    continue;
                }
                if (doc.n) ms_buf_add(&doc, " ", 1);
                ms_buf_add(&doc, l, -1);
            }
            if (hide) { free(title); ms_buf_free(&doc); continue; }
            MsBuf rt = { 0 }; ms_print_type(&rt, md->ret);
            MsBuf ps = { 0 };
            FDef d; memset(&d, 0, sizeof d);
            d.id = sfmt("user.%s.%s", cname, mname); d.title = title; d.category = sfmt("Kod: %s", cname);
            d.kind = sdup("call"); d.user = 1; d.file = sdup(fileName); d.callee = sfmt("%s.%s", cname, mname);
            d.ret = sdup(rt.p ? rt.p : "void"); d.doc = sdup(doc.p ? doc.p : ""); d.hasParams = 1;
            FPort enter = FLOW("enter");
            add_ports(&d.in, &d.nin, &enter, 1);
            d.params = md->nparams ? (FParam*)calloc((size_t)md->nparams, sizeof(FParam)) : NULL;
            for (int k = 0; k < md->nparams; k++) {
                MsBuf t = { 0 }; ms_print_type(&t, md->params[k].tn);
                char pn[64]; snprintf(pn, sizeof pn, "%.*s", md->params[k].nlen, md->params[k].name);
                int reserved = !strcmp(pn, "enter") || !strcmp(pn, "exit") || !strcmp(pn, "result") || !strcmp(pn, "value");
                d.params[k].type = sdup(t.p ? t.p : ""); d.params[k].name = sdup(pn);
                d.params[k].port = reserved ? sfmt("%s_", pn) : sdup(pn);
                FPort p = VAL(d.params[k].port, d.params[k].type, default_for(d.params[k].type));
                add_ports(&d.in, &d.nin, &p, 1);
                if (k) ms_buf_add(&ps, ", ", 2);
                ms_buf_add(&ps, d.params[k].type, -1); ms_buf_add(&ps, " ", 1); ms_buf_add(&ps, pn, -1);
                ms_buf_free(&t);
            }
            d.nparams = md->nparams;
            d.sig = sfmt("%s %s(%s)", d.ret, mname, ps.p ? ps.p : "");
            FPort ex = FLOW("exit");
            add_ports(&d.out, &d.nout, &ex, 1);
            if (strcmp(d.ret, "void")) { FPort r = VAL("result", d.ret, NULL); add_ports(&d.out, &d.nout, &r, 1); }
            ms_buf_free(&rt); ms_buf_free(&ps); ms_buf_free(&doc);
            if (out->n == out->cap) {                               /* a list: overloads keep both (the catalog keeps the first) */
                int nc = out->cap ? out->cap * 2 : 16;
                out->d = (FDef*)realloc(out->d, (size_t)nc * sizeof(FDef));
                out->cap = nc;
            }
            out->d[out->n++] = d;
        }
    }
    arena_free(&A);
}

static char* jdup(const JVal* o, const char* key) {
    const JVal* v = json_get(o, key);
    if (!v || v->t == JS_NULL) return NULL;
    char buf[64];
    const char* t = json_text(v, buf, sizeof buf);
    return sdup(t);
}

static void ports_from_json(const JVal* list, FPort** out, int* n) {
    for (int i = 0; list && list->t == JS_ARR && i < list->n; i++) {
        const JVal* p = list->items[i];
        FPort q = { jdup(p, "name"), jdup(p, "type"), jdup(p, "def"), json_truthy(json_get(p, "flow")) };
        add_ports(out, n, &q, 1);
    }
}

void fg_defs_from_json(FCatalog* out, const JVal* list) {
    for (int i = 0; list && list->t == JS_ARR && i < list->n; i++) {
        const JVal* o = list->items[i];
        FDef d; memset(&d, 0, sizeof d);
        d.id = jdup(o, "id"); d.title = jdup(o, "title"); d.category = jdup(o, "category"); d.kind = jdup(o, "kind");
        d.doc = jdup(o, "doc"); d.sig = jdup(o, "sig"); d.callee = jdup(o, "callee"); d.ret = jdup(o, "ret");
        d.op = jdup(o, "op"); d.symbol = jdup(o, "symbol"); d.method = jdup(o, "method"); d.file = jdup(o, "file");
        d.custom = json_truthy(json_get(o, "custom")); d.user = json_truthy(json_get(o, "user"));
        if (!d.id) { free_def(&d); continue; }
        if (!d.title) d.title = sdup(d.id);
        if (!d.category) d.category = sdup("");
        if (!d.kind) d.kind = sdup("");
        ports_from_json(json_get(o, "inputs"), &d.in, &d.nin);
        ports_from_json(json_get(o, "outputs"), &d.out, &d.nout);
        const JVal* ps = json_get(o, "params");
        if (ps && ps->t == JS_ARR) {
            d.hasParams = 1;
            d.params = ps->n ? (FParam*)calloc((size_t)ps->n, sizeof(FParam)) : NULL;
            for (int k = 0; k < ps->n; k++) { d.params[k].type = jdup(ps->items[k], "type"); d.params[k].name = jdup(ps->items[k], "name"); d.params[k].port = jdup(ps->items[k], "port"); }
            d.nparams = ps->n;
        }
        if (out->n == out->cap) {
            int nc = out->cap ? out->cap * 2 : 16;
            out->d = (FDef*)realloc(out->d, (size_t)nc * sizeof(FDef));
            out->cap = nc;
        }
        out->d[out->n++] = d;
    }
}

/* ---------------------------------------------------------------- the palette */
static void lower(char* s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

int fg_search(const FCatalog* c, const char* query, int* out, int limit) {
    if (limit <= 0) limit = 50;
    char q[128];
    char* t = trim_copy(query ? query : "", (int)strlen(query ? query : ""));
    snprintf(q, sizeof q, "%s", t ? t : ""); free(t);
    lower(q);
    int n = 0;
    if (!q[0]) {
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < c->n && n < limit; i++) if (!!c->d[i].user == !pass) out[n++] = i;
        return n;
    }
    char words[8][64]; int nw = 0;
    for (char* w = strtok(q, " \t"); w && nw < 8; w = strtok(NULL, " \t")) snprintf(words[nw++], 64, "%s", w);
    snprintf(q, sizeof q, "%s", words[0]);                         /* strtok cut q: rebuild "w1 w2" */
    for (int i = 1; i < nw; i++) { strncat(q, " ", sizeof q - strlen(q) - 1); strncat(q, words[i], sizeof q - strlen(q) - 1); }
    typedef struct { int s, len, i; } Sc;
    Sc* sc = (Sc*)malloc((size_t)(c->n ? c->n : 1) * sizeof(Sc));
    int ns = 0;
    for (int i = 0; i < c->n; i++) {
        const FDef* d = &c->d[i];
        char title[128], hay[1024];
        snprintf(title, sizeof title, "%s", d->title); lower(title);
        snprintf(hay, sizeof hay, "%s %s %s %s", title, d->category, d->id, d->doc ? d->doc : ""); lower(hay);
        int all = 1, inTitle = 1;
        for (int w = 0; w < nw; w++) { if (!strstr(hay, words[w])) all = 0; if (!strstr(title, words[w])) inTitle = 0; }
        if (!all) continue;
        int s = 4;
        if (!strcmp(title, q)) s = 0;
        else if (!strncmp(title, q, strlen(q))) s = 1;
        else {
            int part = 0;
            for (char* p = title; *p; p++)
                if ((p == title || p[-1] == '.' || p[-1] == ' ' || p[-1] == '(') && !strncmp(p, q, strlen(q))) part = 1;
            s = part ? 2 : inTitle ? 3 : 4;
        }
        sc[ns].s = s; sc[ns].len = (int)strlen(d->title); sc[ns].i = i; ns++;
    }
    for (int a = 1; a < ns; a++) {                               /* stable insertion sort */
        Sc x = sc[a]; int b = a - 1;
        for (; b >= 0; b--) {
            const Sc* y = &sc[b];
            int cmp = y->s - x.s;
            if (!cmp) cmp = (c->d[x.i].user ? 1 : 0) - (c->d[y->i].user ? 1 : 0);
            if (!cmp) cmp = y->len - x.len;
            if (!cmp) cmp = strcmp(c->d[y->i].title, c->d[x.i].title);
            if (cmp <= 0) break;
            sc[b + 1] = sc[b];
        }
        sc[b + 1] = x;
    }
    for (int i = 0; i < ns && n < limit; i++) out[n++] = sc[i].i;
    free(sc);
    return n;
}

/* ---------------------------------------------------------------- the generator */
typedef struct { char* code; int prec; } X;                  /* an expression and its precedence */
typedef struct { const char *from, *out, *to, *in; } Edge;
typedef struct { char* key; char* name; int scope; } Local;
typedef struct { char** s; int n, cap; } StrSet;

static int set_has(const StrSet* s, const char* x) { for (int i = 0; i < s->n; i++) if (!strcmp(s->s[i], x)) return 1; return 0; }
static void set_add(StrSet* s, const char* x) {
    if (set_has(s, x)) return;
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->s = (char**)realloc(s->s, (size_t)s->cap * sizeof(char*)); }
    s->s[s->n++] = sdup(x);
}
static void set_del(StrSet* s, const char* x) {
    for (int i = 0; i < s->n; i++) if (!strcmp(s->s[i], x)) { free(s->s[i]); memmove(s->s + i, s->s + i + 1, (size_t)(s->n - i - 1) * sizeof(char*)); s->n--; return; }
}
static void set_clear(StrSet* s) { for (int i = 0; i < s->n; i++) free(s->s[i]); free(s->s); memset(s, 0, sizeof *s); }

typedef struct {
    const JVal* graph;
    const FCatalog* cat;
    const JVal** nodes; int nn;                 /* graph.nodes */
    char** nodeIds;                             /* String(n.id) */
    Edge* edges; int ne;
    const JVal* vars;                           /* graph.variables (array or NULL) */
    char className[80];
    int inst;
    const char* stat;
    FMsg* w; int nw, wcap;
    FMsg* e; int nerr, ecap;
    /* per method */
    MsBuf lines; int nlines;
    Local* locals; int nloc, loccap;
    StrSet used, onPath, emitted;
    int* scopes; int nscopes, scopecap, scopeCount;
} Gen;

static void msg(FMsg** arr, int* n, int* cap, const char* node, char* text) {
    if (*n == *cap) { *cap = *cap ? *cap * 2 : 16; *arr = (FMsg*)realloc(*arr, (size_t)*cap * sizeof(FMsg)); }
    (*arr)[*n].node = node && node[0] ? sdup(node) : NULL;
    (*arr)[*n].message = text;
    (*n)++;
}
static void warn(Gen* g, const char* node, char* text) { msg(&g->w, &g->nw, &g->wcap, node, text); }
static void fail(Gen* g, const char* node, char* text) { msg(&g->e, &g->nerr, &g->ecap, node, text); }

static const JVal* node_by_id(Gen* g, const char* id) {          /* the last node with this id wins (as in a Map) */
    if (!id) return NULL;
    for (int i = g->nn - 1; i >= 0; i--) if (!strcmp(g->nodeIds[i], id)) return g->nodes[i];
    return NULL;
}
static const char* id_of(Gen* g, const JVal* n) {
    for (int i = 0; i < g->nn; i++) if (g->nodes[i] == n) return g->nodeIds[i];
    return "undefined";
}
static const FDef* def_of(Gen* g, const JVal* n) {
    const JVal* t = json_get(n, "type");
    return t && t->t == JS_STR ? fg_find(g->cat, t->s) : NULL;
}
static const FPort* port_of(const FDef* d, int in, const char* name) {
    int n = in ? d->nin : d->nout;
    const FPort* ps = in ? d->in : d->out;
    for (int i = 0; i < n; i++) if (streq(ps[i].name, name)) return &ps[i];
    return NULL;
}
static const Edge* first_in(Gen* g, const char* id, const char* port) {
    for (int i = 0; i < g->ne; i++) if (!strcmp(g->edges[i].to, id) && !strcmp(g->edges[i].in, port)) return &g->edges[i];
    return NULL;
}
static const Edge* first_out(Gen* g, const char* id, const char* port) {
    for (int i = 0; i < g->ne; i++) if (!strcmp(g->edges[i].from, id) && !strcmp(g->edges[i].out, port)) return &g->edges[i];
    return NULL;
}
static const char* flow_next(Gen* g, const char* id, const char* port) {
    const Edge* e = first_out(g, id, port);
    return e && e->to[0] ? e->to : NULL;
}
static int var_declared(Gen* g, const char* name) {
    for (int i = 0; g->vars && g->vars->t == JS_ARR && i < g->vars->n; i++) {
        const JVal* n = json_get(g->vars->items[i], "name");
        if (n && n->t == JS_STR && !strcmp(n->s, name)) return 1;
    }
    return 0;
}

static void put(Gen* g, int depth, const char* text) {
    if (g->nlines++) ms_buf_add(&g->lines, "\n", 1);
    for (int i = 0; i < depth; i++) ms_buf_add(&g->lines, "    ", 4);
    ms_buf_add(&g->lines, text, -1);
}

static char* fresh(Gen* g, const char* base0) {
    char base[96]; int n = 0;
    for (const char* p = base0 ? base0 : "v"; *p && n < (int)sizeof base - 1; p++)
        if (isalnum((unsigned char)*p) || *p == '_') base[n++] = *p;
    base[n] = 0;
    if (!n) strcpy(base, "v");
    else base[0] = (char)tolower((unsigned char)base[0]);
    char name[112];
    snprintf(name, sizeof name, "%s", base);
    for (int k = 2; set_has(&g->used, name); k++) snprintf(name, sizeof name, "%s%d", base, k);
    set_add(&g->used, name);
    return sdup(name);
}

static Local* local_get(Gen* g, const char* id, const char* port) {
    char key[200]; snprintf(key, sizeof key, "%s/%s", id, port);
    for (int i = 0; i < g->nloc; i++) if (!strcmp(g->locals[i].key, key)) return &g->locals[i];
    return NULL;
}
static void local_set(Gen* g, const char* id, const char* port, const char* name, int scope) {
    Local* l = local_get(g, id, port);
    if (l) { free(l->name); l->name = sdup(name); l->scope = scope; return; }
    if (g->nloc == g->loccap) { g->loccap = g->loccap ? g->loccap * 2 : 16; g->locals = (Local*)realloc(g->locals, (size_t)g->loccap * sizeof(Local)); }
    g->locals[g->nloc].key = sfmt("%s/%s", id, port);
    g->locals[g->nloc].name = sdup(name);
    g->locals[g->nloc].scope = scope;
    g->nloc++;
}
static int in_scope(Gen* g, int s) { for (int i = 0; i < g->nscopes; i++) if (g->scopes[i] == s) return 1; return 0; }
static int top_scope(Gen* g) { return g->scopes[g->nscopes - 1]; }
static void push_scope(Gen* g) {
    if (g->nscopes == g->scopecap) { g->scopecap = g->scopecap ? g->scopecap * 2 : 16; g->scopes = (int*)realloc(g->scopes, (size_t)g->scopecap * sizeof(int)); }
    g->scopes[g->nscopes++] = ++g->scopeCount;
}

/* precedence of a raw expression typed into a node: 100 simple, 90 a leading minus, 0 anything with operators */
static int raw_prec(const char* raw) {
    int depth = 0; char quote = 0;
    for (int i = 0; raw[i]; i++) {
        char c = raw[i];
        if (quote) { if (c == '\\') { if (raw[i + 1]) i++; } else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '(' || c == '[') { depth++; continue; }
        if (c == ')' || c == ']') { depth--; continue; }
        if (depth == 0 && strchr(" \t\n\r\v\f+-*/%<>=!&|?:,", c) && !(c == '-' && i == 0)) return 0;
    }
    return raw[0] == '-' ? 90 : 100;
}
static X mkx(char* code, int prec) { X x = { code, prec }; return x; }
static X bad(void) { return mkx(sdup("0"), 100); }
static char* wrap_left(X x, int p) { return x.prec < p ? sfmt("(%s)", x.code) : sdup(x.code); }
static char* wrap_right(X x, int p) { return x.prec <= p ? sfmt("(%s)", x.code) : sdup(x.code); }

static char* raw_default(const JVal* n, const FDef* d, const char* portName) {
    const FPort* p = port_of(d, 1, portName);
    const JVal* data = json_get(n, "data");
    const JVal* defs = json_truthy(data) ? json_get(data, "defaults") : NULL;
    const JVal* given = json_truthy(defs) ? json_get(defs, portName) : NULL;
    if (given && !(given->t == JS_STR && !given->s[0])) return sdup(S(given));
    return sdup(p && p->def ? p->def : "0");
}

static X expr_of(Gen* g, const char* id, const char* portName, StrSet* stack);

static X expr_input(Gen* g, const JVal* n, const FDef* d, const char* portName, StrSet* stack) {
    const Edge* e = first_in(g, id_of(g, n), portName);
    if (e) return expr_of(g, e->from, e->out, stack);
    char* raw = raw_default(n, d, portName);
    return mkx(raw, raw_prec(raw));
}

static char* call_args(Gen* g, const JVal* n, const FDef* d, StrSet* stack) {
    MsBuf b = { 0 };
    for (int i = 0; i < d->nparams; i++) {
        X a = expr_input(g, n, d, d->params[i].port ? d->params[i].port : d->params[i].name, stack);
        if (i) ms_buf_add(&b, ", ", 2);
        ms_buf_add(&b, a.code, -1);
        free(a.code);
    }
    return b.p ? b.p : sdup("");
}

static X expr_of(Gen* g, const char* id, const char* portName, StrSet* stack) {
    const JVal* n = node_by_id(g, id);
    if (!n) return bad();
    const FDef* d = def_of(g, n);
    if (!d) return bad();
    Local* loc = local_get(g, id, portName);
    if (loc) {
        if (!in_scope(g, loc->scope)) warn(g, id, sfmt("wartość z węzła „%s” jest użyta poza blokiem, w którym powstała", d->title));
        return mkx(sdup(loc->name), 100);
    }
    if (set_has(stack, id)) { fail(g, id, sfmt("cykl w połączeniach wartości (węzeł „%s”)", d->title)); return bad(); }
    set_add(stack, id);
    X r;
    const JVal* data = json_get(n, "data");
    if (streq(d->kind, "lit")) {
        const JVal* v = json_truthy(data) ? json_get(data, "value") : NULL;
        char* raw = sdup(v ? S(v) : "0");
        r = mkx(raw, raw_prec(raw));
    } else if (streq(d->kind, "getvar")) {
        const JVal* nm = json_truthy(data) ? json_get(data, "name") : NULL;
        if (!ident_v(nm)) { fail(g, id, sdup("„Pobierz zmienną” bez poprawnej nazwy")); r = bad(); }
        else {
            if (!var_declared(g, S(nm))) warn(g, id, sfmt("zmienna „%s” nie jest zadeklarowana w grafie", S(nm)));
            r = mkx(sdup(S(nm)), 100);
        }
    } else if (streq(d->kind, "const")) r = mkx(sdup(d->callee), 100);
    else if (streq(d->kind, "new")) { char* a = call_args(g, n, d, stack); r = mkx(sfmt("%s(%s)", d->callee, a), 100); free(a); }
    else if (streq(d->kind, "binary")) {
        int bi = binary_index(d->op);
        int p = bi >= 0 ? BINARY[bi].prec : 0;
        X a = expr_input(g, n, d, "a", stack), b = expr_input(g, n, d, "b", stack);
        char* l = wrap_left(a, p), *rr = wrap_right(b, p);
        r = mkx(sfmt("%s %s %s", l, bi >= 0 ? BINARY[bi].sym : "?", rr), p);
        free(l); free(rr); free(a.code); free(b.code);
    } else if (streq(d->kind, "unary")) {
        X a = expr_input(g, n, d, "a", stack);
        r = mkx(a.prec < 90 ? sfmt("%s(%s)", d->symbol, a.code) : sfmt("%s%s", d->symbol, a.code), 90);
        free(a.code);
    } else if (streq(d->kind, "call")) {
        if (strcmp(portName, "result")) { fail(g, id, sfmt("„%s” nie ma wartości „%s”", d->title, portName)); r = bad(); }
        else {
            if (first_in(g, id, "enter")) warn(g, id, sfmt("wartość z „%s” jest użyta, zanim przepływ do niego dotarł", d->title));
            char* a = call_args(g, n, d, stack);
            r = mkx(sfmt("%s(%s)", d->callee, a), 100);
            free(a);
        }
    } else if (streq(d->kind, "for")) { warn(g, id, sdup("indeks pętli For użyty poza pętlą")); r = mkx(sdup("i"), 100); }
    else { fail(g, id, sfmt("węzeł „%s” nie ma wartości do użycia w wyrażeniu", d->title)); r = bad(); }
    set_del(stack, id);
    return r;
}

static char* negate(X x) { return x.prec >= 90 ? sfmt("!%s", x.code) : sfmt("!(%s)", x.code); }

static const char* emit_node(Gen* g, const char* id, int depth);

static void chain_from(Gen* g, const char* id, const char* portName, int depth) {
    const char* cur = flow_next(g, id, portName);
    StrSet added = { 0 };
    while (cur) {
        if (set_has(&g->onPath, cur)) { warn(g, cur, sdup("pętla w przepływie: użyj węzła For albo While")); break; }
        set_add(&g->onPath, cur); set_add(&added, cur);
        cur = emit_node(g, cur, depth);
    }
    for (int i = 0; i < added.n; i++) set_del(&g->onPath, added.s[i]);
    set_clear(&added);
}
static void scoped_chain(Gen* g, const char* id, const char* portName, int depth) {
    push_scope(g);
    chain_from(g, id, portName, depth);
    g->nscopes--;
}

static const char* LOOP_NAMES[] = { "i", "j", "k", "l", "m", "n" };

static const char* emit_node(Gen* g, const char* id, int depth) {
    const JVal* n = node_by_id(g, id);
    if (!n) return NULL;
    const FDef* d = def_of(g, n);
    if (!d) return NULL;
    set_add(&g->emitted, id);
    StrSet stack = { 0 };
    const JVal* data = json_get(n, "data");
    const char* next = NULL;
    if (streq(d->kind, "call")) {
        char* a = call_args(g, n, d, &stack);
        char* call = sfmt("%s(%s)", d->callee, a);
        free(a);
        if (!streq(d->ret, "void") && first_out(g, id, "result")) {
            const char* dot = strrchr(d->callee, '.');
            char* name = fresh(g, dot ? dot + 1 : d->callee);
            char* line = sfmt("var %s = %s;", name, call);
            put(g, depth, line); free(line);
            local_set(g, id, "result", name, top_scope(g));
            free(name);
        } else { char* line = sfmt("%s;", call); put(g, depth, line); free(line); }
        free(call);
        next = flow_next(g, id, "exit");
    } else if (streq(d->kind, "setvar")) {
        const JVal* nm = json_truthy(data) ? json_get(data, "name") : NULL;
        if (!ident_v(nm)) { fail(g, id, sdup("„Ustaw zmienną” bez poprawnej nazwy")); next = flow_next(g, id, "exit"); goto done; }
        char name[96]; snprintf(name, sizeof name, "%s", S(nm));
        if (!var_declared(g, name)) warn(g, id, sfmt("zmienna „%s” nie jest zadeklarowana w grafie", name));
        /* x = x - 3  ->  x -= 3 */
        const Edge* ve = first_in(g, id, "value");
        const JVal* src = ve ? node_by_id(g, ve->from) : NULL;
        const FDef* sd = src ? def_of(g, src) : NULL;
        if (sd && streq(sd->kind, "binary") && (streq(sd->op, "op.add") || streq(sd->op, "op.sub") || streq(sd->op, "op.mul") || streq(sd->op, "op.div"))
            && !local_get(g, ve->from, ve->out)) {
            const char* srcId = id_of(g, src);
            const Edge* ae = first_in(g, srcId, "a");
            const JVal* an = ae ? node_by_id(g, ae->from) : NULL;
            const FDef* ad = an ? def_of(g, an) : NULL;
            const JVal* adata = an ? json_get(an, "data") : NULL;
            const JVal* aname = json_truthy(adata) ? json_get(adata, "name") : NULL;
            if (ad && streq(ad->kind, "getvar") && aname && aname->t == JS_STR && !strcmp(aname->s, name)) {
                StrSet st2 = { 0 }; set_add(&st2, srcId);
                X b = expr_input(g, src, sd, "b", &st2);
                set_clear(&st2);
                char* line = sfmt("%s %s= %s;", name, BINARY[binary_index(sd->op)].sym, b.code);
                put(g, depth, line); free(line); free(b.code);
                next = flow_next(g, id, "exit");
                goto done;
            }
        }
        X v = expr_input(g, n, d, "value", &stack);
        char* line = sfmt("%s = %s;", name, v.code);
        put(g, depth, line); free(line); free(v.code);
        next = flow_next(g, id, "exit");
    } else if (streq(d->kind, "if")) {
        X cond = expr_input(g, n, d, "cond", &stack);
        const char* t = flow_next(g, id, "true"), *f = flow_next(g, id, "false");
        if (t) {
            char* line = sfmt("if (%s) {", cond.code); put(g, depth, line); free(line);
            scoped_chain(g, id, "true", depth + 1);
            if (f) { put(g, depth, "} else {"); scoped_chain(g, id, "false", depth + 1); }
            put(g, depth, "}");
        } else if (f) {
            char* ng = negate(cond);
            char* line = sfmt("if (%s) {", ng); put(g, depth, line); free(line); free(ng);
            scoped_chain(g, id, "false", depth + 1);
            put(g, depth, "}");
        } else warn(g, id, sdup("„Jeśli” bez żadnego połączonego wyjścia"));
        free(cond.code);
        next = flow_next(g, id, "exit");
    } else if (streq(d->kind, "for")) {
        X first = expr_input(g, n, d, "first", &stack), last = expr_input(g, n, d, "last", &stack), step = expr_input(g, n, d, "step", &stack);
        const char* v = NULL;
        char* freshName = NULL;
        for (int i = 0; i < 6 && !v; i++) {
            int active = 0;
            for (int k = 0; k < g->nloc; k++) if (in_scope(g, g->locals[k].scope) && !strcmp(g->locals[k].name, LOOP_NAMES[i])) active = 1;
            if (!active) v = LOOP_NAMES[i];
        }
        if (!v) v = freshName = fresh(g, "i");
        int down = step.code[0] == '-';
        char* inc = !strcmp(step.code, "1") ? sfmt("%s++", v) : !strcmp(step.code, "-1") ? sfmt("%s--", v) : sfmt("%s += %s", v, step.code);
        char* lw = wrap_right(last, 60);
        char* line = sfmt("for (int %s = %s; %s %s %s; %s) {", v, first.code, v, down ? ">" : "<", lw, inc);
        put(g, depth, line);
        free(line); free(lw); free(inc);
        push_scope(g);
        local_set(g, id, "index", v, top_scope(g));
        chain_from(g, id, "body", depth + 1);
        g->nscopes--;
        put(g, depth, "}");
        free(first.code); free(last.code); free(step.code); free(freshName);
        next = flow_next(g, id, "exit");
    } else if (streq(d->kind, "while")) {
        X cond = expr_input(g, n, d, "cond", &stack);
        char* line = sfmt("while (%s) {", cond.code); put(g, depth, line); free(line); free(cond.code);
        scoped_chain(g, id, "body", depth + 1);
        put(g, depth, "}");
        next = flow_next(g, id, "exit");
    } else {
        warn(g, id, sfmt("węzeł „%s” nie może stać w przepływie", d->title));
        next = NULL;
    }
done:
    set_clear(&stack);
    return next;
}

/* problems that make a graph unusable: unknown node types, missing ports, flow wired to a value, ... */
static void validate(Gen* g) {
    StrSet ids = { 0 }, seenIn = { 0 }, seenOut = { 0 };
    for (int i = 0; i < g->nn; i++) {
        const char* id = g->nodeIds[i];
        if (set_has(&ids, id)) fail(g, id, sfmt("powtórzony identyfikator węzła %s", id));
        set_add(&ids, id);
        const JVal* t = json_get(g->nodes[i], "type");
        if (!(t && t->t == JS_STR && fg_find(g->cat, t->s))) fail(g, id, sfmt("nieznany typ węzła „%s”", S(t)));
    }
    for (int i = 0; i < g->ne; i++) {
        const Edge* e = &g->edges[i];
        const JVal* a = node_by_id(g, e->from), *b = node_by_id(g, e->to);
        if (!a || !b) { fail(g, a ? e->to : e->from, sdup("połączenie do nieistniejącego węzła")); continue; }
        const FDef* da = def_of(g, a), *db = def_of(g, b);
        if (!da || !db) continue;
        const FPort* po = port_of(da, 0, e->out), *pi = port_of(db, 1, e->in);
        if (!po) { fail(g, id_of(g, a), sfmt("węzeł %s nie ma wyjścia „%s”", da->title, e->out)); continue; }
        if (!pi) { fail(g, id_of(g, b), sfmt("węzeł %s nie ma wejścia „%s”", db->title, e->in)); continue; }
        if (!!po->flow != !!pi->flow) fail(g, id_of(g, b), sfmt("połączono przepływ z wartością (%s.%s → %s.%s)", da->title, e->out, db->title, e->in));
        char k[256];
        if (!pi->flow) {
            snprintf(k, sizeof k, "%s/%s", e->to, e->in);
            if (set_has(&seenIn, k)) fail(g, id_of(g, b), sfmt("wejście „%s” węzła %s ma kilka połączeń", e->in, db->title));
            set_add(&seenIn, k);
        }
        if (po->flow) {
            snprintf(k, sizeof k, "%s/%s", e->from, e->out);
            if (set_has(&seenOut, k)) fail(g, id_of(g, a), sfmt("wyjście „%s” węzła %s ma kilka połączeń", e->out, da->title));
            set_add(&seenOut, k);
        }
    }
    set_clear(&ids); set_clear(&seenIn); set_clear(&seenOut);
}

typedef struct { const JVal* node; double x, y; int order; } Ev;
typedef struct { char* name; char** ids; int n; } Method;

void fg_generate(const JVal* graph, const FCatalog* cat, int embed, FGenResult* out) {
    memset(out, 0, sizeof *out);
    Gen G; memset(&G, 0, sizeof G);
    Gen* g = &G;
    g->graph = graph; g->cat = cat;
    const JVal* nodes = json_get(graph, "nodes"), *edges = json_get(graph, "edges");
    g->vars = json_get(graph, "variables");
    if (g->vars && g->vars->t != JS_ARR) g->vars = NULL;
    g->nn = nodes && nodes->t == JS_ARR ? nodes->n : 0;
    g->nodes = (const JVal**)calloc((size_t)(g->nn ? g->nn : 1), sizeof(JVal*));
    g->nodeIds = (char**)calloc((size_t)(g->nn ? g->nn : 1), sizeof(char*));
    for (int i = 0; i < g->nn; i++) { g->nodes[i] = nodes->items[i]; g->nodeIds[i] = sdup(S(json_get(nodes->items[i], "id"))); }
    g->ne = edges && edges->t == JS_ARR ? edges->n : 0;
    g->edges = (Edge*)calloc((size_t)(g->ne ? g->ne : 1), sizeof(Edge));
    char** edgeStr = (char**)calloc((size_t)(g->ne ? g->ne : 1) * 4, sizeof(char*));
    for (int i = 0; i < g->ne; i++) {
        const JVal* e = edges->items[i];
        edgeStr[i * 4] = sdup(S(json_get(e, "from"))); edgeStr[i * 4 + 1] = sdup(S(json_get(e, "out")));
        edgeStr[i * 4 + 2] = sdup(S(json_get(e, "to"))); edgeStr[i * 4 + 3] = sdup(S(json_get(e, "in")));
        g->edges[i].from = edgeStr[i * 4]; g->edges[i].out = edgeStr[i * 4 + 1];
        g->edges[i].to = edgeStr[i * 4 + 2]; g->edges[i].in = edgeStr[i * 4 + 3];
    }

    validate(g);
    const JVal* cn = json_get(graph, "className");
    snprintf(g->className, sizeof g->className, "%s", ident_v(cn) ? S(cn) : "Game");
    g->inst = json_truthy(json_get(graph, "instance"));
    g->stat = g->inst ? "" : "static ";
    if (json_truthy(cn) && !is_ident(S(cn))) fail(g, NULL, sfmt("niepoprawna nazwa klasy „%s”", S(cn)));

    /* methods: every event node starts one; several nodes of the same event run one after another */
    Ev* evs = (Ev*)calloc((size_t)(g->nn ? g->nn : 1), sizeof(Ev));
    int nev = 0;
    for (int i = 0; i < g->nn; i++) {
        const FDef* d = def_of(g, g->nodes[i]);
        if (!d || !streq(d->kind, "event")) continue;
        evs[nev].node = g->nodes[i];
        evs[nev].x = json_number(json_get(g->nodes[i], "x"), 0);
        evs[nev].y = json_number(json_get(g->nodes[i], "y"), 0);
        if (evs[nev].x != evs[nev].x) evs[nev].x = 0;
        if (evs[nev].y != evs[nev].y) evs[nev].y = 0;
        evs[nev].order = nev;
        nev++;
    }
    for (int a = 1; a < nev; a++) {                              /* stable sort by y, then x */
        Ev x = evs[a]; int b = a - 1;
        for (; b >= 0 && (evs[b].y > x.y || (evs[b].y == x.y && evs[b].x > x.x)); b--) evs[b + 1] = evs[b];
        evs[b + 1] = x;
    }
    static const char* ENTRY[] = { "Main", "Update", "Draw" };
    Method* methods = NULL; int nm = 0;
    for (int i = 0; i < nev; i++) {
        const JVal* ev = evs[i].node;
        const char* evId = id_of(g, ev);
        const FDef* d = def_of(g, ev);
        char m[96];
        snprintf(m, sizeof m, "%s", d->method ? d->method : "undefined");
        if (g->inst && !d->custom) {
            fail(g, evId, sfmt("w klasie z obiektami nie działa „%s” (Main, Update i Draw są statyczne): użyj węzła „Metoda” i wywołaj ją z kodu", d->title));
            continue;
        }
        if (d->custom) {
            const JVal* data = json_get(ev, "data");
            const JVal* nmv = json_truthy(data) ? json_get(data, "name") : NULL;
            if (!ident_v(nmv)) { fail(g, evId, sdup("„Metoda” bez poprawnej nazwy")); continue; }
            snprintf(m, sizeof m, "%s", S(nmv));
            if (!strcmp(m, "Main") || !strcmp(m, "Update") || !strcmp(m, "Draw")) { fail(g, evId, sfmt("„%s” to wejście programu: użyj węzła Start, Update albo Draw", m)); continue; }
            if (var_declared(g, m)) { fail(g, evId, sfmt("„%s” jest też nazwą zmiennej grafu", m)); continue; }
        }
        int k;
        for (k = 0; k < nm; k++) if (!strcmp(methods[k].name, m)) break;
        if (k == nm) { methods = (Method*)realloc(methods, (size_t)(nm + 1) * sizeof(Method)); methods[nm].name = sdup(m); methods[nm].ids = NULL; methods[nm].n = 0; nm++; }
        methods[k].ids = (char**)realloc(methods[k].ids, (size_t)(methods[k].n + 1) * sizeof(char*));
        methods[k].ids[methods[k].n++] = sdup(evId);
    }
    /* order: Main, Update, Draw, then the graph's own methods by name */
    int* order = (int*)malloc((size_t)(nm ? nm : 1) * sizeof(int)); int no = 0;
    for (int e = 0; e < 3; e++) for (int k = 0; k < nm; k++) if (!strcmp(methods[k].name, ENTRY[e])) order[no++] = k;
    int firstOwn = no;
    for (int k = 0; k < nm; k++)
        if (strcmp(methods[k].name, "Main") && strcmp(methods[k].name, "Update") && strcmp(methods[k].name, "Draw")) order[no++] = k;
    for (int a = firstOwn + 1; a < no; a++) {
        int x = order[a], b = a - 1;
        for (; b >= firstOwn && strcmp(methods[order[b]].name, methods[x].name) > 0; b--) order[b + 1] = order[b];
        order[b + 1] = x;
    }
    MsBuf body = { 0 }; int bodyLines = 0;
    #define BODY(s) do { if (bodyLines++) ms_buf_add(&body, "\n", 1); ms_buf_add(&body, (s), -1); } while (0)
    for (int oi = 0; oi < no; oi++) {
        Method* M = &methods[order[oi]];
        ms_buf_free(&g->lines); g->nlines = 0;
        for (int i = 0; i < g->nloc; i++) { free(g->locals[i].key); free(g->locals[i].name); }
        g->nloc = 0;
        set_clear(&g->used); set_clear(&g->onPath);
        g->nscopes = 0; g->scopeCount = 0;
        if (!g->scopecap) { g->scopecap = 16; g->scopes = (int*)malloc(16 * sizeof(int)); }
        g->scopes[g->nscopes++] = 0;
        for (int i = 0; i < M->n; i++) { set_add(&g->emitted, M->ids[i]); chain_from(g, M->ids[i], "exit", 2); }
        if (bodyLines) BODY("");
        int entry = !strcmp(M->name, "Main") || !strcmp(M->name, "Update") || !strcmp(M->name, "Draw");
        if (!entry) {
            char* c = g->inst ? sfmt("    // Do wywołania z kodu: var o = new %s(); o.%s();", g->className, M->name)
                              : sfmt("    // Do wywołania z kodu: %s.%s();", g->className, M->name);
            BODY(c); free(c);
        }
        char* head = sfmt("    %svoid %s() {", g->stat, M->name);
        BODY(head); free(head);
        if (g->nlines) BODY(g->lines.p);
        BODY("    }");
    }
    if (!nev) warn(g, NULL, sdup("graf nie ma żadnego węzła zdarzenia (Start, Update, Draw albo Metoda), więc nic się nie wykona"));

    /* flow nodes that no event leads to */
    for (int i = 0; i < g->nn; i++) {
        const FDef* d = def_of(g, g->nodes[i]);
        if (!d || set_has(&g->emitted, g->nodeIds[i])) continue;
        int isFlow = streq(d->kind, "setvar") || streq(d->kind, "if") || streq(d->kind, "for") || streq(d->kind, "while") ||
                     (streq(d->kind, "call") && streq(d->ret, "void"));
        if (isFlow) warn(g, g->nodeIds[i], sfmt("węzeł „%s” nie jest połączony z żadnym zdarzeniem, więc nie trafi do kodu", d->title));
    }

    MsBuf fields = { 0 }; int nf = 0;
    for (int i = 0; g->vars && i < g->vars->n; i++) {
        const JVal* v = g->vars->items[i];
        const JVal* name = json_get(v, "name"), *type = json_get(v, "type"), *init = json_get(v, "init");
        if (!ident_v(name)) { fail(g, NULL, sfmt("niepoprawna nazwa zmiennej „%s”", S(name))); continue; }
        char* f;
        int hasInit = init && !(init->t == JS_STR && !init->s[0]);
        char ty[96]; snprintf(ty, sizeof ty, "%s", json_truthy(type) ? S(type) : "float");
        if (hasInit) { char iv[256]; snprintf(iv, sizeof iv, "%s", S(init)); f = sfmt("    %s%s %s = %s;", g->stat, ty, S(name), iv); }
        else f = sfmt("    %s%s %s;", g->stat, ty, S(name));
        if (nf++) ms_buf_add(&fields, "\n", 1);
        ms_buf_add(&fields, f, -1);
        free(f);
    }
    MsBuf code = { 0 };
    ms_buf_add(&code, "// Wygenerowano z grafu. Zmiany w tym pliku nie wrócą do grafu, edytuj go w edytorze grafów.\n", -1);
    char* cls = sfmt("class %s {\n", g->className); ms_buf_add(&code, cls, -1); free(cls);
    if (nf) { ms_buf_add(&code, fields.p, -1); ms_buf_add(&code, "\n\n", 2); }
    if (bodyLines) { ms_buf_add(&code, body.p, -1); ms_buf_add(&code, "\n", 1); }
    ms_buf_add(&code, "}\n", -1);
    if (embed) {
        char* line = fg_serialize(graph);
        ms_buf_add(&code, "\n", 1); ms_buf_add(&code, line ? line : "", -1); ms_buf_add(&code, "\n", 1);
        free(line);
    }
    out->code = code.p;
    out->warnings = g->w; out->nwarnings = g->nw;
    out->errors = g->e; out->nerrors = g->nerr;
    #undef BODY

    ms_buf_free(&body); ms_buf_free(&fields); ms_buf_free(&g->lines);
    for (int k = 0; k < nm; k++) { for (int i = 0; i < methods[k].n; i++) free(methods[k].ids[i]); free(methods[k].ids); free(methods[k].name); }
    free(methods); free(order); free(evs);
    for (int i = 0; i < g->nloc; i++) { free(g->locals[i].key); free(g->locals[i].name); }
    free(g->locals); free(g->scopes);
    set_clear(&g->used); set_clear(&g->onPath); set_clear(&g->emitted);
    for (int i = 0; i < g->nn; i++) free(g->nodeIds[i]);
    for (int i = 0; i < g->ne * 4; i++) free(edgeStr[i]);
    free(edgeStr); free(g->nodeIds); free(g->nodes); free(g->edges);
}

void fg_result_free(FGenResult* r) {
    free(r->code);
    for (int i = 0; i < r->nwarnings; i++) { free(r->warnings[i].node); free(r->warnings[i].message); }
    for (int i = 0; i < r->nerrors; i++) { free(r->errors[i].node); free(r->errors[i].message); }
    free(r->warnings); free(r->errors);
    memset(r, 0, sizeof *r);
}

/* ---------------------------------------------------------------- the graph inside the generated file */
char* fg_serialize(const JVal* graph) {
    char* j = json_write(graph);
    if (!j) return NULL;
    MsBuf b = { 0 };
    ms_buf_add(&b, "// @graph ", -1);
    for (const char* p = j; *p; p++) {                       /* U+2028 / U+2029 would end the comment line in some editors */
        if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 && ((unsigned char)p[2] == 0xA8 || (unsigned char)p[2] == 0xA9)) {
            ms_buf_add(&b, (unsigned char)p[2] == 0xA8 ? "\\u2028" : "\\u2029", -1);
            p += 2;
        } else ms_buf_add(&b, p, 1);
    }
    free(j);
    return b.p;
}

JVal* fg_extract(const char* code) {
    for (const char* p = code; (p = strstr(p, "// @graph ")) != NULL; p++) {
        if (p != code && p[-1] != '\n') continue;
        const char* s = p + 10, *e = s;
        while (*e && *e != '\n' && *e != '\r') e++;
        if (e == s) continue;
        char* line = sdupn(s, (int)(e - s));
        char err[100];
        JVal* g = json_parse(line, err, sizeof err);
        free(line);
        if (!g) return NULL;
        const JVal* n = json_get(g, "nodes"), *ed = json_get(g, "edges");
        if (n && n->t == JS_ARR && ed && ed->t == JS_ARR) return g;
        json_free(g);
        return NULL;
    }
    return NULL;
}
