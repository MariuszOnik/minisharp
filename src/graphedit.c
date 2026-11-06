/* MiniSharp - editing through the graph (see graph.h). Every action becomes a patch of the code text:
   the source stays the only truth, the graph is built again from the new code, and node positions are
   carried over by where each node's code is in the text. */
#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

/* layout units of graph.c (new nodes are put next to their neighbours with the same gaps) */
#define HEAD 13.0f
#define ROW 10.0f
#define GAP_X 14.0f
#define GAP_Y 8.0f
#define INDENT 28.0f

static int fail(char* err, int errlen, const char* f, ...) {
    va_list ap; va_start(ap, f);
    vsnprintf(err, (size_t)errlen, f, ap);
    va_end(ap);
    return 0;
}

static char* dup_n(const char* s, int n) {
    char* d = (char*)malloc((size_t)n + 1);
    if (d) { memcpy(d, s, (size_t)n); d[n] = 0; }
    return d;
}

void ms_patch_free(MsPatch* p) { free(p->text); p->text = NULL; }

char* ms_patch_apply(const char* src, const MsPatch* p) {
    size_t len = strlen(src), tl = strlen(p->text);
    char* out = (char*)malloc(len - (size_t)(p->end - p->start) + tl + 1);
    if (!out) return NULL;
    memcpy(out, src, (size_t)p->start);
    memcpy(out + p->start, p->text, tl);
    memcpy(out + p->start + tl, src + p->end, len - (size_t)p->end + 1);
    return out;
}

static void set_patch(MsPatch* out, Graph* g, const char* s0, const char* s1, MsBuf* text) {
    out->start = (int)(s0 - g->src); out->end = (int)(s1 - g->src);
    out->text = text->p ? text->p : dup_n("", 0);
    text->p = NULL;
}

/* ---------- checking what the user typed ---------- */
static ProgramAST* parse(Arena* A, const char* src, char* err, int errlen) {
    jmp_buf jb;
    ParseCtx c = { A, &jb, err, errlen };
    if (setjmp(jb)) return NULL;
    int n; Token* t = ms_lex(&c, src, &n);
    return ms_parse(&c, t, n);
}

/* the statements of 'class C { void M() { pre text post } }', or NULL */
static Stmt* wrapped_body(Arena* A, const char* pre, const char* text, const char* post) {
    MsBuf b = { 0 };
    ms_buf_add(&b, "class C { void M() { ", -1); ms_buf_add(&b, pre, -1); ms_buf_add(&b, text, -1);
    ms_buf_add(&b, post, -1); ms_buf_add(&b, " } }", -1);
    if (b.oom) return NULL;
    char* src = (char*)arena_alloc(A, (size_t)b.n + 1);     /* names in the AST point into it */
    memcpy(src, b.p, (size_t)b.n + 1);
    ms_buf_free(&b);
    char err[200];
    ProgramAST* pr = parse(A, src, err, sizeof err);
    if (!pr || pr->nclasses != 1 || pr->classes[0].nmethods != 1) return NULL;
    return pr->classes[0].methods[0].body;
}

static int is_atomic(Expr* e) {     /* can stand as an operand without parentheses */
    switch (e->k) {
        case E_INT: return e->ival >= 0;
        case E_FLOAT: return e->fval >= 0;
        case E_BOOL: case E_STRING: case E_NULL: case E_NAME: case E_THIS: case E_CHAR:
        case E_MEMBER: case E_CALL: case E_INDEX: case E_NEW: case E_NEWLIST: case E_NEWARR: return 1;
        case E_INCDEC: return !e->prefix;
        default: return 0;
    }
}

/* ---------- text helpers ---------- */
static const char* line_start(Graph* g, const char* p) { while (p > g->src && p[-1] != '\n') p--; return p; }

static void indent_of(Graph* g, const char* p, char* out, int cap) {   /* white space at the start of p's line */
    const char* s = line_start(g, p);
    int n = 0;
    while ((s[n] == ' ' || s[n] == '\t') && n < cap - 1) { out[n] = s[n]; n++; }
    out[n] = 0;
}

/* text with every line after the first starting with ind */
static void add_indented(MsBuf* b, const char* text, const char* ind) {
    for (const char* p = text; *p; p++) {
        ms_buf_add(b, p, 1);
        if (*p == '\n' && p[1]) ms_buf_add(b, ind, -1);
    }
}

/* a trimmed copy; a missing ';' is added when that makes the statements parse */
static char* clean_stmts(const char* text, char* err, int errlen) {
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') text++;
    int n = (int)strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == '\t')) n--;
    if (!n) { fail(err, errlen, "nothing to insert"); return NULL; }
    char* s = (char*)malloc((size_t)n + 2);
    if (!s) return NULL;
    memcpy(s, text, (size_t)n); s[n] = 0;
    Arena A = { 0 };
    Stmt* b = wrapped_body(&A, "", s, "");
    if (!b && s[n - 1] != ';' && s[n - 1] != '}') {
        s[n] = ';'; s[n + 1] = 0;
        arena_free(&A);
        b = wrapped_body(&A, "", s, "");
    }
    int ok = b && b->n > 0;
    arena_free(&A);
    if (!ok) { free(s); fail(err, errlen, "this is not a statement: %.60s", text); return NULL; }
    return s;
}

/* ---------- where a node sits in the code ---------- */
typedef struct { int parent, port; Stmt* list; int single; } Container;

static Stmt* branch_of(Graph* g, int node, int port) {   /* the statement or block hanging on a port (NULL: no else yet) */
    GNode* n = &g->n[node];
    if (n->kind == GN_ENTRY) return g->m->body;
    Stmt* s = n->st;
    switch (s->k) {
        case S_IF: return port == 1 ? s->body : s->els;
        case S_WHILE: case S_DO: case S_FOR: case S_FOREACH: return s->body;
        case S_BLOCK: return s;
        case S_SWITCH: return port - 1 < s->n ? s->list[port - 1] : NULL;
        default: return NULL;
    }
}

static Container container_of(Graph* g, int node) {
    int h = node;
    while (g->n[h].fromPort == 0 && g->n[g->n[h].from].kind == GN_FLOW) h = g->n[h].from;
    Container c;
    c.parent = g->n[h].from; c.port = g->n[h].fromPort;
    c.list = branch_of(g, c.parent, c.port);
    c.single = c.list && c.list->k != S_BLOCK && c.list->k != S_CASE;
    return c;
}

/* ---------- actions ---------- */
int gr_set_input(Graph* g, int node, int input, const char* text, MsPatch* out, char* err, int errlen) {
    if (node < 0 || node >= g->nn || input < 0 || input >= g->n[node].nin) return fail(err, errlen, "no such input");
    GNode* n = &g->n[node];
    GInput* in = &g->in[n->in0 + input];
    if (!in->s0 || !in->s1) return fail(err, errlen, "this input is not in the code");
    while (*text == ' ') text++;
    if (!*text) return fail(err, errlen, "an input cannot be empty");
    Arena A = { 0 };
    MsBuf b = { 0 };
    if (!in->ex) {                                       /* the init part of 'for (init; ...)' */
        Stmt* s = wrapped_body(&A, "for (", text, "; ; ) { }");
        int ok = s && s->n == 1 && s->list[0]->k == S_FOR;
        arena_free(&A);
        if (!ok) return fail(err, errlen, "not a for loop start: %.60s", text);
        ms_buf_add(&b, text, -1);
    } else {
        Stmt* s = wrapped_body(&A, "var v = ", text, ";");
        int ok = s && s->n == 1 && s->list[0]->k == S_VAR && !s->list[0]->els;
        if (!ok) { arena_free(&A); return fail(err, errlen, "not a value: %.60s", text); }
        /* an operand slot (a + _, _.x, -_) needs parentheses around 'x + 1' to keep its meaning;
           arguments, values of '=' and indexes are full slots */
        Expr* owner = n->kind == GN_VALUE ? n->ex : n->kind == GN_FLOW && n->st->k == S_EXPR ? n->st->e : NULL;
        int operand = owner != NULL;
        if (owner) {
            for (int i = 0; i < owner->nargs; i++) if (owner->args[i] == in->ex) operand = 0;
            if ((owner->k == E_INDEX || owner->k == E_ASSIGN || owner->k == E_COND) && in->ex == owner->b) operand = 0;
            if (owner->k == E_NEW || owner->k == E_NEWLIST || owner->k == E_NEWARR) operand = 0;
        }
        int paren = operand && !is_atomic(s->list[0]->e);
        arena_free(&A);
        if (paren) ms_buf_add(&b, "(", 1);
        ms_buf_add(&b, text, -1);
        if (paren) ms_buf_add(&b, ")", 1);
    }
    set_patch(out, g, in->s0, in->s1, &b);
    return 1;
}

/* '=> expr;' becomes a block with the new statements before or after */
static int expand_expr_body(Graph* g, const char* stmts, int before, MsPatch* out, char* err, int errlen) {
    MethodDecl* m = g->m;
    Stmt* one = m->body->list[0];
    Expr* e = one->e;
    const char* arrow = e->s0;
    while (arrow > m->s0 && !(arrow[0] == '=' && arrow[1] == '>')) arrow--;
    const char* semi = e->s1;
    while (*semi && *semi != ';') semi++;
    if (arrow <= m->s0 || *semi != ';') return fail(err, errlen, "cannot find '=> ...;'");
    char ind0[64], ind1[72];
    indent_of(g, m->s0, ind0, sizeof ind0);
    snprintf(ind1, sizeof ind1, "%s    ", ind0);
    MsBuf b = { 0 };
    ms_buf_add(&b, "{\n", -1);
    if (before) { ms_buf_add(&b, ind1, -1); add_indented(&b, stmts, ind1); ms_buf_add(&b, "\n", 1); }
    ms_buf_add(&b, ind1, -1);
    if (one->k == S_RETURN) ms_buf_add(&b, "return ", -1);
    ms_buf_add(&b, e->s0, (int)(e->s1 - e->s0));
    ms_buf_add(&b, ";\n", -1);
    if (!before) { ms_buf_add(&b, ind1, -1); add_indented(&b, stmts, ind1); ms_buf_add(&b, "\n", 1); }
    ms_buf_add(&b, ind0, -1); ms_buf_add(&b, "}", 1);
    set_patch(out, g, arrow, semi + 1, &b);
    return 1;
}

/* a single statement body 's' (no braces) becomes a block, with the new statements before or after it */
static void wrap_single(Graph* g, Stmt* s, const char* ownerLine, const char* stmts, int before, MsPatch* out) {
    char ind0[64], ind1[72];
    indent_of(g, ownerLine, ind0, sizeof ind0);
    snprintf(ind1, sizeof ind1, "%s    ", ind0);
    MsBuf b = { 0 };
    ms_buf_add(&b, "{\n", -1);
    if (before) { ms_buf_add(&b, ind1, -1); add_indented(&b, stmts, ind1); ms_buf_add(&b, "\n", 1); }
    if (s) { ms_buf_add(&b, ind1, -1); ms_buf_add(&b, s->s0, (int)(s->s1 - s->s0)); ms_buf_add(&b, "\n", 1); }
    if (!before) { ms_buf_add(&b, ind1, -1); add_indented(&b, stmts, ind1); ms_buf_add(&b, "\n", 1); }
    ms_buf_add(&b, ind0, -1); ms_buf_add(&b, "}", 1);
    set_patch(out, g, s->s0, s->s1, &b);
}

int gr_insert(Graph* g, int node, int port, const char* text, MsPatch* out, char* err, int errlen) {
    if (node < 0 || node >= g->nn || g->n[node].kind == GN_VALUE) return fail(err, errlen, "statements go on a flow node");
    GNode* n = &g->n[node];
    if (port < 0 || port >= n->nports) return fail(err, errlen, "no such port");
    if (!g->m->body) return fail(err, errlen, "this method has no body");
    char* stmts = clean_stmts(text, err, errlen);
    if (!stmts) return 0;
    int ok = 1;
    MsBuf b = { 0 };
    char ind[64], ind1[72];

    if (g->m->exprBody) ok = expand_expr_body(g, stmts, n->kind == GN_ENTRY, out, err, errlen);
    else if (port == 0 && n->kind == GN_FLOW) {             /* right after the node */
        Container c = container_of(g, node);
        if (c.single) wrap_single(g, n->st, g->n[c.parent].st->s0, stmts, 0, out);
        else {
            indent_of(g, n->st->s0, ind, sizeof ind);
            ms_buf_add(&b, "\n", 1); ms_buf_add(&b, ind, -1); add_indented(&b, stmts, ind);
            set_patch(out, g, n->st->s1, n->st->s1, &b);
        }
    } else {                                                 /* first on a branch (or in the method) */
        Stmt* br = branch_of(g, node, port == 0 ? 0 : port);
        const char* owner = n->kind == GN_ENTRY ? g->m->s0 : n->st->s0;
        indent_of(g, owner, ind, sizeof ind);
        snprintf(ind1, sizeof ind1, "%s    ", ind);
        if (!br) {                                           /* if without else: add one */
            Stmt* body = n->st->body;
            ms_buf_add(&b, " else {\n", -1); ms_buf_add(&b, ind1, -1); add_indented(&b, stmts, ind1);
            ms_buf_add(&b, "\n", 1); ms_buf_add(&b, ind, -1); ms_buf_add(&b, "}", 1);
            set_patch(out, g, body->s1, body->s1, &b);
        } else if (br->k == S_CASE || (br->k == S_BLOCK && br->s0 && br->s0[0] == '{')) {
            if (br->n > 0) {                                 /* before the first statement */
                Stmt* first = br->list[0];
                char fi[64]; indent_of(g, first->s0, fi, sizeof fi);
                add_indented(&b, stmts, fi); ms_buf_add(&b, "\n", 1); ms_buf_add(&b, fi, -1);
                set_patch(out, g, first->s0, first->s0, &b);
            } else if (br->k == S_CASE) ok = fail(err, errlen, "this case has no statements (it falls through to the next one)");
            else {                                           /* empty { } */
                char bi[64]; indent_of(g, br->s0, bi, sizeof bi);
                char bi1[72]; snprintf(bi1, sizeof bi1, "%s    ", bi);
                ms_buf_add(&b, "{\n", -1); ms_buf_add(&b, bi1, -1); add_indented(&b, stmts, bi1);
                ms_buf_add(&b, "\n", 1); ms_buf_add(&b, bi, -1); ms_buf_add(&b, "}", 1);
                set_patch(out, g, br->s0, br->s1, &b);
            }
        } else if (br->k == S_BLOCK) {                       /* the empty statement ';' */
            ms_buf_add(&b, "{\n", -1); ms_buf_add(&b, ind1, -1); add_indented(&b, stmts, ind1);
            ms_buf_add(&b, "\n", 1); ms_buf_add(&b, ind, -1); ms_buf_add(&b, "}", 1);
            set_patch(out, g, br->s0, br->s1, &b);
        } else wrap_single(g, br, owner, stmts, 1, out);
    }
    ms_buf_free(&b);
    free(stmts);
    return ok;
}

int gr_delete(Graph* g, int node, MsPatch* out, char* err, int errlen) {
    if (node < 0 || node >= g->nn) return fail(err, errlen, "no such node");
    GNode* n = &g->n[node];
    if (n->kind == GN_ENTRY) return fail(err, errlen, "the method itself is deleted in the code");
    if (n->kind == GN_VALUE) return fail(err, errlen, "a value is replaced by typing a new one into its input");
    if (!n->st->s0) return fail(err, errlen, "a '=> ...' method has just this one statement");
    MsBuf b = { 0 };
    Container c = container_of(g, node);
    if (c.single) {                                          /* 'if (c) x();' -> 'if (c) { }' keeps the meaning of what follows */
        ms_buf_add(&b, "{ }", -1);
        set_patch(out, g, n->st->s0, n->st->s1, &b);
        return 1;
    }
    const char* s0 = n->st->s0, *s1 = n->st->s1;
    const char* ls = line_start(g, s0);
    const char* p = ls;
    while (p < s0 && (*p == ' ' || *p == '\t')) p++;
    const char* q = s1;
    while (*q == ' ' || *q == '\t') q++;
    if (p == s0 && (*q == '\n' || (*q == '\r' && q[1] == '\n') || !*q)) {   /* alone on its line: the whole line goes */
        s0 = ls;
        s1 = *q == '\r' ? q + 2 : *q ? q + 1 : q;
    } else s1 = q;
    set_patch(out, g, s0, s1, &b);
    return 1;
}

/* ---------- the @layout comment ---------- */
static char* with_layout_line(const char* src, const char* name, const char* line) {
    const char* blk = NULL, *q = src;
    while ((q = strstr(q, "/* @layout")) != NULL) { blk = q; q++; }
    MsBuf b = { 0 };
    size_t nl = strlen(name);
    if (!blk || !strstr(blk, "*/")) {
        ms_buf_add(&b, src, -1);
        size_t len = strlen(src);
        if (len && src[len - 1] != '\n') ms_buf_add(&b, "\n", 1);
        ms_buf_add(&b, "/* @layout\n", -1); ms_buf_add(&b, line, -1); ms_buf_add(&b, "\n*/\n", -1);
        return b.p;
    }
    const char* end = strstr(blk, "*/");
    const char* p = blk;
    while (p < end) {                                        /* this method's line, if there is one */
        const char* eol = p;
        while (eol < end && *eol != '\n') eol++;
        const char* t = p;
        while (t < eol && (*t == ' ' || *t == '\t')) t++;
        if ((size_t)(eol - t) > nl && !memcmp(t, name, nl) && (t[nl] == ' ' || t[nl] == '\t' || t[nl] == '\r')) {
            const char* e2 = eol > t && eol[-1] == '\r' ? eol - 1 : eol;
            ms_buf_add(&b, src, (int)(t - src)); ms_buf_add(&b, line, -1); ms_buf_add(&b, e2, -1);
            return b.p;
        }
        p = eol + 1;
    }
    const char* at = end;                                    /* a new line just before the closing */
    while (at > blk && (at[-1] == ' ' || at[-1] == '\t')) at--;
    ms_buf_add(&b, src, (int)(at - src));
    if (at > src && at[-1] != '\n') ms_buf_add(&b, "\n", 1);
    ms_buf_add(&b, line, -1); ms_buf_add(&b, "\n", 1); ms_buf_add(&b, at, -1);
    return b.p;
}

char* gr_save_layout(Graph* g) {
    MsBuf line = { 0 };
    gr_layout_line(g, &line);
    char* out = line.p ? with_layout_line(g->src, g->name, line.p) : NULL;
    ms_buf_free(&line);
    return out;
}

char* gr_move(Graph* g, int node, float x, float y) {
    if (node < 0 || node >= g->nn) return NULL;
    float dx = x - g->n[node].x, dy = y - g->n[node].y;
    g->n[node].x = x; g->n[node].y = y;
    /* the value nodes feeding it come along (they are made after it, so one pass in order is enough) */
    char* moved = (char*)calloc((size_t)g->nn, 1);
    if (moved) {
        moved[node] = 1;
        for (int i = node + 1; i < g->nn; i++)
            if (g->n[i].kind == GN_VALUE && moved[g->n[i].from]) { g->n[i].x += dx; g->n[i].y += dy; moved[i] = 1; }
        free(moved);
    }
    return gr_save_layout(g);
}

char* gr_fold(Graph* g, int node, int input, int open) {
    if (node < 0 || node >= g->nn || input < 0 || input >= g->n[node].nin) return NULL;
    char key[48];
    snprintf(key, sizeof key, "%.40s.a%d", g->n[node].key, input % 1000);
    int i;
    for (i = 0; i < g->nlay; i++) if (g->lay[i].fold && !strcmp(g->lay[i].key, key)) break;
    if (i == g->nlay) {
        GLayoutItem* l = (GLayoutItem*)realloc(g->lay, (size_t)(g->nlay + 1) * sizeof(GLayoutItem));
        if (!l) return NULL;
        g->lay = l; g->laycap = g->nlay + 1;
        memset(&g->lay[i], 0, sizeof g->lay[i]);
        snprintf(g->lay[i].key, sizeof g->lay[i].key, "%s", key);
        g->nlay++;
    }
    g->lay[i].fold = open ? 1 : 2;
    return gr_save_layout(g);
}

/* ---------- carrying the layout over a code change ---------- */
typedef struct { int start, end, delta; } Shift;

static long map_off(const Shift* sh, long off) {      /* old offset -> new offset, -1 inside the replaced text */
    if (off < sh->start) return off;
    if (off >= sh->end) return off + sh->delta;
    return -1;
}

static void node_span(GNode* n, const char** s0, const char** s1) {
    *s0 = *s1 = NULL;
    if (n->kind == GN_FLOW) { *s0 = n->st->s0; *s1 = n->st->s1; }
    else if (n->kind == GN_VALUE) { *s0 = n->ex->s0; *s1 = n->ex->s1; }
}

/* the old node that shows the same code (same kind, same text position after the shift) */
static int old_twin(Graph* old, const Shift* sh, Graph* nw, int i) {
    GNode* n = &nw->n[i];
    if (n->kind == GN_ENTRY) return 0;
    const char *a0, *a1;
    node_span(n, &a0, &a1);
    for (int k = 1; k < old->nn; k++) {
        GNode* o = &old->n[k];
        if (o->kind != n->kind || strcmp(o->tag, n->tag)) continue;
        const char *b0, *b1;
        node_span(o, &b0, &b1);
        if (!a0 || !b0) { if (!a0 && !b0 && !strcmp(o->key, n->key)) return k; continue; }   /* made-up '=> expr' statement */
        if (map_off(sh, b0 - old->src) == a0 - nw->src && map_off(sh, b1 - old->src) == a1 - nw->src) return k;
    }
    return -1;
}

static int add_item(GLayoutItem** items, int* n, int* cap, const GLayoutItem* it) {
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 32;
        GLayoutItem* p = (GLayoutItem*)realloc(*items, (size_t)nc * sizeof(GLayoutItem));
        if (!p) return 0;
        *items = p; *cap = nc;
    }
    (*items)[(*n)++] = *it;
    return 1;
}

/* nodes without a carried position: under their predecessor (everything lower moves down) or left of their consumer */
static void place_new(Graph* g) {
    for (int i = 1; i < g->nn; i++) {
        GNode* n = &g->n[i];
        if (n->flags & GF_PLACED) continue;
        GNode* f = &g->n[n->from];
        if (n->kind == GN_VALUE) {
            n->x = f->x - GAP_X - n->w;
            n->y = f->y + HEAD + ROW * (float)n->fromPort;
        } else {
            /* below everything that already hangs on the predecessor (its branches and their values) */
            float bottom = f->y + f->h;
            for (int k = 1; k < g->nn; k++) {
                if (k == i || !(g->n[k].flags & GF_PLACED)) continue;
                int a = k, last = k;               /* k hangs on a branch or an input of the predecessor? */
                while (a > 0 && a != n->from && a != i) { last = a; a = g->n[a].from; }
                /* after the node: below all its branches; first on branch p: below branches 1..p-1 only */
                GNode* via = &g->n[last != k ? last : k];
                int branch = via->kind == GN_VALUE || (via->fromPort >= 1 && (n->fromPort == 0 || via->fromPort < n->fromPort));
                if (a == n->from && branch)
                    if (g->n[k].y + g->n[k].h > bottom) bottom = g->n[k].y + g->n[k].h;
            }
            n->x = f->x + (n->fromPort == 0 ? 0 : INDENT);
            n->y = bottom + GAP_Y;
            for (int k = 0; k < g->nn; k++)
                if (k != i && (g->n[k].flags & GF_PLACED) && g->n[k].y >= n->y) g->n[k].y += n->h + GAP_Y;
        }
        n->flags |= GF_PLACED;
    }
}

char* gr_apply(Graph* g, const MsPatch* p, char* err, int errlen, char* newKey, int keycap) {
    if (newKey && keycap > 0) newKey[0] = 0;
    char* nsrc = ms_patch_apply(g->src, p);
    if (!nsrc) { fail(err, errlen, "out of memory"); return NULL; }
    Arena A = { 0 };
    ProgramAST* pr = parse(&A, nsrc, err, errlen);
    if (!pr) { free(nsrc); arena_free(&A); return NULL; }
    Shift sh = { p->start, p->end, (int)strlen(p->text) - (p->end - p->start) };
    const char* cls = g->cls->name; int clen = g->cls->nlen;
    const char* mn = g->m->name; int mlen = g->m->nlen;
    GLayoutItem none;
    Graph* g1 = gr_build_ex(pr, nsrc, cls, clen, mn, mlen, &none, 0);     /* only for the new keys */
    if (!g1) { arena_free(&A); return nsrc; }                              /* the method is gone: nothing to carry */
    GLayoutItem* items = NULL; int n = 0, cap = 0;
    for (int i = 0; i < g1->nn; i++) {
        int k = old_twin(g, &sh, g1, i);
        if (k < 0) {
            if (newKey && !newKey[0] && g1->n[i].kind == GN_FLOW) snprintf(newKey, (size_t)keycap, "%s", g1->n[i].key);
            continue;
        }
        GLayoutItem it; memset(&it, 0, sizeof it);
        snprintf(it.key, sizeof it.key, "%s", g1->n[i].key);
        snprintf(it.tag, sizeof it.tag, "%s", g1->n[i].tag);
        it.x = g->n[k].x; it.y = g->n[k].y;
        add_item(&items, &n, &cap, &it);
        /* open/closed inputs of the same node */
        for (int f = 0; f < g->nlay; f++) {
            if (!g->lay[f].fold) continue;
            size_t kl = strlen(g->n[k].key);
            const char* fk = g->lay[f].key;
            if (strncmp(fk, g->n[k].key, kl) || strncmp(fk + kl, ".a", 2)) continue;
            const char* tail = fk + kl + 2;
            if (!*tail || strchr(tail, '.')) continue;
            GLayoutItem fi; memset(&fi, 0, sizeof fi);
            snprintf(fi.key, sizeof fi.key, "%.36s.a%.6s", g1->n[i].key, tail);
            fi.fold = g->lay[f].fold;
            add_item(&items, &n, &cap, &fi);
        }
    }
    Graph* g2 = gr_build_ex(pr, nsrc, cls, clen, mn, mlen, items ? items : &none, n);
    free(items);
    gr_free(g1);
    char* out = NULL;
    if (g2) {
        place_new(g2);
        out = gr_save_layout(g2);
        gr_free(g2);
    }
    arena_free(&A);
    if (out) { free(nsrc); return out; }
    return nsrc;
}
