/* MiniSharp - graph view of one method (see graph.h and docs/graph-model.md). */
#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* snprintf into a fixed buffer; cutting long names is intended (titles and keys have fixed sizes) */
static int fmt(char* out, size_t n, const char* f, ...) {
    va_list ap; va_start(ap, f);
    int r = vsnprintf(out, n, f, ap);
    va_end(ap);
    return r < 0 ? 0 : r >= (int)n ? (int)n - 1 : r;
}

#define FOLD_MAX 24             /* an expression with longer source text gets its own node */
#define TEXT_MAX 28             /* folded text longer than this is cut on screen */
/* layout units are pixels at zoom 1 (font cell about 6 x 10) */
#define CH_W 6.0f
#define ROW 10.0f
#define HEAD 13.0f
#define PAD 4.0f
#define GAP_X 14.0f
#define GAP_Y 8.0f
#define INDENT 28.0f

/* ---------- storage ---------- */
static void* grow(Graph* g, void* p, int n, int* cap, size_t sz) {
    if (n < *cap) return p;
    int nc = *cap ? *cap * 2 : 32;
    void* q = realloc(p, (size_t)nc * sz);
    if (!q) { g->oom = 1; return p; }
    *cap = nc;
    return q;
}

static int add_node(Graph* g, int kind, Stmt* st, Expr* ex, const char* key, int from, int port) {
    GNode* n = (GNode*)grow(g, g->n, g->nn, &g->ncap, sizeof(GNode));
    if (g->oom) return -1;
    g->n = n;
    GNode* x = &g->n[g->nn];
    memset(x, 0, sizeof *x);
    x->kind = kind; x->st = st; x->ex = ex; x->from = from; x->fromPort = port;
    fmt(x->key, sizeof x->key, "%s", key);
    x->in0 = g->nin;
    return g->nn++;
}

static void add_input(Graph* g, int node, const char* label, Expr* ex, const char* s0, const char* s1) {
    GInput* in = (GInput*)grow(g, g->in, g->nin, &g->incap, sizeof(GInput));
    if (g->oom) return;
    g->in = in;
    GInput* x = &g->in[g->nin++];
    fmt(x->label, sizeof x->label, "%s", label);
    x->ex = ex;
    x->s0 = s0 ? s0 : ex ? ex->s0 : NULL;
    x->s1 = s1 ? s1 : ex ? ex->s1 : NULL;
    x->node = -1;
    g->n[node].nin++;
}

static const GLayoutItem* find_lay(Graph* g, const char* key, int fold) {
    for (int i = 0; i < g->nlay; i++)
        if ((fold ? g->lay[i].fold != 0 : g->lay[i].fold == 0) && !strcmp(g->lay[i].key, key)) return &g->lay[i];
    return NULL;
}

/* ---------- text helpers ---------- */
static void title_of_type(char* out, int outlen, TypeNode* tn) {
    MsBuf b = { 0 };
    ms_print_type(&b, tn);
    fmt(out, (size_t)outlen, "%s", b.p ? b.p : "?");
    ms_buf_free(&b);
}

static int is_leaf(Expr* e) {
    switch (e->k) {
        case E_INT: case E_FLOAT: case E_BOOL: case E_STRING: case E_NULL: case E_NAME: case E_THIS: case E_BASE: case E_CHAR: return 1;
        default: return 0;
    }
}
static int is_path(Expr* e) {           /* a, this, a.b.c */
    if (e->k == E_NAME || e->k == E_THIS || e->k == E_BASE) return 1;
    return e->k == E_MEMBER && !e->prefix && is_path(e->a);
}
static int is_class_name(Expr* e) {     /* Gfx, Mathf.Something: written with a capital letter, read as a class */
    if (e->k == E_NAME) return isupper((unsigned char)e->name[0]);
    return e->k == E_MEMBER && !e->prefix && isupper((unsigned char)e->name[0]) && is_class_name(e->a);
}
static int path_text(char* out, int outlen, Expr* e) {   /* "Game.bullets" */
    if (e->k == E_THIS) return fmt(out, (size_t)outlen, "this");
    if (e->k == E_BASE) return fmt(out, (size_t)outlen, "base");
    if (e->k == E_NAME) return fmt(out, (size_t)outlen, "%.*s", e->nlen, e->name);
    int n = path_text(out, outlen, e->a);
    if (n < outlen) n += fmt(out + n, (size_t)(outlen - n), ".%.*s", e->nlen, e->name);
    return n;
}
static int span_len(const char* s0, const char* s1) { return s0 && s1 ? (int)(s1 - s0) : 1000; }
static int multiline(const char* s0, const char* s1) { return s0 && s1 && memchr(s0, '\n', (size_t)(s1 - s0)) != NULL; }

static int expandable(Expr* e) {
    return e && !is_leaf(e) && !is_path(e) && e->k != E_LAMBDA && e->k != E_PAIR && e->k != E_OUT;
}

/* ---------- building ---------- */
static void expand_inputs(Graph* g, int node);

/* Title and inputs of an expression shown as a node (statement node or value node). */
static void decompose(Graph* g, int node, Expr* e) {
    GNode* n = &g->n[node];
    char t[64];
    switch (e->k) {
        case E_CALL: {
            Expr* f = e->a;
            if (f->k == E_MEMBER && is_class_name(f->a)) { path_text(t, sizeof t, f); fmt(n->title, sizeof n->title, "%s", t); }
            else if (f->k == E_MEMBER) {
                fmt(n->title, sizeof n->title, "%s%.*s", f->prefix ? "?." : ".", f->nlen, f->name);
                add_input(g, node, "obj", f->a, NULL, NULL);
            } else if (f->k == E_NAME) fmt(n->title, sizeof n->title, "%.*s", f->nlen, f->name);
            else { fmt(n->title, sizeof n->title, "call"); add_input(g, node, "f", f, NULL, NULL); }
            for (int i = 0; i < e->nargs; i++) { char l[8]; fmt(l, sizeof l, "a%d", i + 1); add_input(g, node, l, e->args[i], NULL, NULL); }
            break;
        }
        case E_ASSIGN:
            if (is_path(e->a)) { path_text(t, sizeof t, e->a); fmt(n->title, sizeof n->title, "%s %s", t, ms_op_text(e->op)); }
            else { fmt(n->title, sizeof n->title, "%s", ms_op_text(e->op)); add_input(g, node, "target", e->a, NULL, NULL); }
            add_input(g, node, "value", e->b, NULL, NULL);
            break;
        case E_INCDEC:
            if (is_path(e->a)) {
                path_text(t, sizeof t, e->a);
                if (e->prefix) fmt(n->title, sizeof n->title, "%s%s", ms_op_text(e->op), t);
                else fmt(n->title, sizeof n->title, "%s%s", t, ms_op_text(e->op));
            } else { fmt(n->title, sizeof n->title, "%s%s", e->prefix ? "" : "x", ms_op_text(e->op)); add_input(g, node, "target", e->a, NULL, NULL); }
            break;
        case E_BINARY: case E_LOGIC:
            fmt(n->title, sizeof n->title, "%s", ms_op_text(e->op));
            add_input(g, node, "a", e->a, NULL, NULL); add_input(g, node, "b", e->b, NULL, NULL);
            break;
        case E_UNARY:
            fmt(n->title, sizeof n->title, "%s", ms_op_text(e->op));
            add_input(g, node, "a", e->a, NULL, NULL);
            break;
        case E_COND:
            fmt(n->title, sizeof n->title, "? :");
            add_input(g, node, "cond", e->a, NULL, NULL); add_input(g, node, "then", e->b, NULL, NULL); add_input(g, node, "else", e->c, NULL, NULL);
            break;
        case E_COALESCE:
            fmt(n->title, sizeof n->title, "??");
            add_input(g, node, "a", e->a, NULL, NULL); add_input(g, node, "b", e->b, NULL, NULL);
            break;
        case E_MEMBER:
            fmt(n->title, sizeof n->title, "%s%.*s", e->prefix ? "?." : ".", e->nlen, e->name);
            add_input(g, node, "obj", e->a, NULL, NULL);
            break;
        case E_INDEX:
            fmt(n->title, sizeof n->title, "[ ]");
            add_input(g, node, "obj", e->a, NULL, NULL); add_input(g, node, "index", e->b, NULL, NULL);
            break;
        case E_NEW: case E_NEWLIST: case E_NEWARR:
            title_of_type(t, sizeof t, e->tn);
            fmt(n->title, sizeof n->title, "new %s%s", t, e->k == E_NEWARR ? "[]" : "");
            if (e->a) add_input(g, node, e->k == E_NEWARR ? "size" : "capacity", e->a, NULL, NULL);
            for (int i = 0; i < e->nargs; i++) { char l[8]; fmt(l, sizeof l, "a%d", i + 1); add_input(g, node, l, e->args[i], NULL, NULL); }
            break;
        case E_CAST:
            if (e->tn) title_of_type(t, sizeof t, e->tn); else fmt(t, sizeof t, "%s", ms_op_text(e->op));
            fmt(n->title, sizeof n->title, "(%s)", t);
            add_input(g, node, "a", e->a, NULL, NULL);
            break;
        case E_IS: case E_AS:
            if (e->tn) title_of_type(t, sizeof t, e->tn); else fmt(t, sizeof t, "null");
            fmt(n->title, sizeof n->title, "%s %s%s", e->k == E_IS ? "is" : "as", e->op ? "not " : "", t);
            add_input(g, node, "a", e->a, NULL, NULL);
            break;
        default: {                       /* leaves, lambdas: the node is just its text */
            char s[64]; gr_input_text(g, &(GInput){ "", e, e->s0, e->s1, -1 }, s, 36);
            fmt(n->title, sizeof n->title, "%s", s);
            break;
        }
    }
}

static void expand_inputs(Graph* g, int node) {
    int in0 = g->n[node].in0, nin = g->n[node].nin;
    for (int i = 0; i < nin && !g->oom; i++) {
        GInput* in = &g->in[in0 + i];
        if (!expandable(in->ex)) continue;
        char key[48];
        fmt(key, sizeof key, "%s.a%d", g->n[node].key, i);
        const GLayoutItem* f = find_lay(g, key, 1);
        int open = f ? f->fold == 1 : (span_len(in->s0, in->s1) > FOLD_MAX || multiline(in->s0, in->s1));
        if (!open) continue;
        Expr* ex = in->ex;
        int v = add_node(g, GN_VALUE, NULL, ex, key, node, i);
        if (v < 0) return;
        g->in[in0 + i].node = v;
        fmt(g->n[v].tag, sizeof g->n[v].tag, "val");
        g->n[v].in0 = g->nin;
        decompose(g, v, ex);
        expand_inputs(g, v);
    }
}

static void build_chain(Graph* g, Stmt* s, const char* prefix, int from, int port);

static int build_stmt(Graph* g, Stmt* s, const char* key, int from, int port) {
    int ni = add_node(g, GN_FLOW, s, NULL, key, from, port);
    if (ni < 0) return -1;
    GNode* n = &g->n[ni];
    n->nports = 1;
    const char* tag = "stmt";
    char t[64], sub[56];
    switch (s->k) {
        case S_EXPR:
            tag = s->e->k == E_CALL ? "call" : s->e->k == E_ASSIGN ? "set" : s->e->k == E_INCDEC ? "inc" : "expr";
            decompose(g, ni, s->e);
            break;
        case S_VAR: {
            tag = "var";
            if (s->tn) title_of_type(t, sizeof t, s->tn); else fmt(t, sizeof t, "var");
            int len = fmt(g->n[ni].title, sizeof g->n[ni].title, "%s", t);
            for (Stmt* v = s; v; v = v->els) {
                if (len < (int)sizeof g->n[ni].title)
                    len += fmt(g->n[ni].title + len, sizeof g->n[ni].title - (size_t)len, "%s%.*s", v == s ? " " : ", ", v->nlen, v->name);
                if (v->e) { fmt(sub, sizeof sub, "%.*s", v->nlen, v->name); add_input(g, ni, sub, v->e, NULL, NULL); }
            }
            break;
        }
        case S_IF: tag = "if"; fmt(n->title, sizeof n->title, "if"); add_input(g, ni, "cond", s->e, NULL, NULL); g->n[ni].nports = 3; break;
        case S_WHILE: tag = "while"; fmt(n->title, sizeof n->title, "while"); add_input(g, ni, "cond", s->e, NULL, NULL); g->n[ni].nports = 2; break;
        case S_DO: tag = "do"; fmt(n->title, sizeof n->title, "do ... while"); add_input(g, ni, "cond", s->e, NULL, NULL); g->n[ni].nports = 2; break;
        case S_FOR:
            tag = "for"; fmt(n->title, sizeof n->title, "for");
            if (s->init) add_input(g, ni, "init", NULL, s->init->s0, s->init->s1);
            if (s->e) add_input(g, ni, "cond", s->e, NULL, NULL);
            if (s->step) add_input(g, ni, "step", s->step, NULL, NULL);
            g->n[ni].nports = 2;
            break;
        case S_FOREACH:
            tag = "each";
            if (s->tn) title_of_type(t, sizeof t, s->tn); else fmt(t, sizeof t, "var");
            fmt(n->title, sizeof n->title, "foreach %s %.*s", t, s->nlen, s->name);
            add_input(g, ni, "in", s->e, NULL, NULL);
            g->n[ni].nports = 2;
            break;
        case S_SWITCH: tag = "switch"; fmt(n->title, sizeof n->title, "switch"); add_input(g, ni, "value", s->e, NULL, NULL); g->n[ni].nports = 1 + s->n; break;
        case S_RETURN: tag = "ret"; fmt(n->title, sizeof n->title, "return"); if (s->e) add_input(g, ni, "value", s->e, NULL, NULL); break;
        case S_YIELD: tag = "yield"; fmt(n->title, sizeof n->title, "yield return"); add_input(g, ni, "value", s->e, NULL, NULL); break;
        case S_YIELDBREAK: tag = "ybreak"; fmt(n->title, sizeof n->title, "yield break"); break;
        case S_BREAK: tag = "break"; fmt(n->title, sizeof n->title, "break"); break;
        case S_CONTINUE: tag = "cont"; fmt(n->title, sizeof n->title, "continue"); break;
        case S_BLOCK: tag = "block"; fmt(n->title, sizeof n->title, "{ }"); g->n[ni].nports = 2; break;
        default: fmt(n->title, sizeof n->title, "?"); break;
    }
    if (g->oom) return -1;
    fmt(g->n[ni].tag, sizeof g->n[ni].tag, "%s", tag);
    expand_inputs(g, ni);
    /* branches */
    char pre[48];
    switch (s->k) {
        case S_IF:
            fmt(pre, sizeof pre, "%s.t", key); build_chain(g, s->body, pre, ni, 1);
            if (s->els) { fmt(pre, sizeof pre, "%s.f", key); build_chain(g, s->els, pre, ni, 2); }
            break;
        case S_WHILE: case S_DO: case S_FOR: case S_FOREACH:
            fmt(pre, sizeof pre, "%s.b", key); build_chain(g, s->body, pre, ni, 1);
            break;
        case S_BLOCK:
            fmt(pre, sizeof pre, "%s.b", key); build_chain(g, s, pre, ni, 1);
            break;
        case S_SWITCH:
            for (int i = 0; i < s->n; i++) {
                fmt(pre, sizeof pre, "%s.c%d_", key, i);
                Stmt tmp; memset(&tmp, 0, sizeof tmp);
                tmp.k = S_BLOCK; tmp.list = s->list[i]->list; tmp.n = s->list[i]->n;
                build_chain(g, &tmp, pre, ni, 1 + i);
            }
            break;
    }
    return ni;
}

/* the statements of a block (or one statement) as a chain hanging on port `port` of node `from` */
static void build_chain(Graph* g, Stmt* s, const char* prefix, int from, int port) {
    Stmt** list = s->k == S_BLOCK ? s->list : &s;
    int n = s->k == S_BLOCK ? s->n : 1;
    int prev = from, pport = port;
    for (int i = 0; i < n && !g->oom; i++) {
        char key[48];
        fmt(key, sizeof key, "%s%d", prefix, i);
        int ni = build_stmt(g, list[i], key, prev, pport);
        if (ni < 0) return;
        prev = ni; pport = 0;
    }
}

/* ---------- layout ---------- */
static int text_len(Graph* g, GInput* in) {
    char s[TEXT_MAX + 8];
    gr_input_text(g, in, s, TEXT_MAX);
    return (int)strlen(s);
}

static void measure(Graph* g, int ni) {
    GNode* n = &g->n[ni];
    int cols = (int)strlen(n->title);
    for (int i = 0; i < n->nin; i++) {
        GInput* in = &g->in[n->in0 + i];
        int c = (int)strlen(in->label) + 2 + (in->node >= 0 ? 0 : text_len(g, in));
        if (c > cols) cols = c;
    }
    if (n->kind == GN_FLOW && n->nports > 1) {
        int c = 0;
        for (int p = 1; p < n->nports; p++) { char l[40]; gr_port_label(g, ni, p, l, sizeof l); c += (int)strlen(l) + 1; }
        if (c > cols) cols = c;
    }
    n->w = PAD * 2 + CH_W * (float)cols;
    n->h = HEAD + ROW * (float)n->nin + (n->kind == GN_FLOW && n->nports > 1 ? ROW : 0) + PAD;
}

static int child(Graph* g, int ni, int port) {          /* first flow node hanging on a port */
    for (int i = ni + 1; i < g->nn; i++)
        if (g->n[i].kind == GN_FLOW && g->n[i].from == ni && g->n[i].fromPort == port) return i;
    return -1;
}

static float values_width(Graph* g, int ni) {           /* how far the value nodes of ni reach to its left */
    float w = 0;
    GNode* n = &g->n[ni];
    for (int i = 0; i < n->nin; i++) {
        int c = g->in[n->in0 + i].node;
        if (c < 0) continue;
        float cw = GAP_X + g->n[c].w + values_width(g, c);
        if (cw > w) w = cw;
    }
    return w;
}

static float chain_left(Graph* g, int first) {
    float w = 0;
    for (int ni = first; ni >= 0; ni = child(g, ni, 0)) { float v = values_width(g, ni); if (v > w) w = v; }
    return w;
}

/* value nodes go left of their input, stacked downwards; returns the lowest y used */
static float place_values(Graph* g, int ni) {
    GNode* n = &g->n[ni];
    float bottom = n->y + n->h, y = n->y;
    for (int i = 0; i < n->nin; i++) {
        int c = g->in[n->in0 + i].node;
        if (c < 0) continue;
        g->n[c].x = n->x - GAP_X - g->n[c].w;
        g->n[c].y = y;
        float b = place_values(g, c);
        y = b + GAP_Y;
        if (b > bottom) bottom = b;
    }
    return bottom;
}

/* a chain flows downwards; branches are indented to the right like code (far enough for their value nodes) */
static float place_chain(Graph* g, int first, float x, float y) {
    float bottom = y;
    for (int ni = first; ni >= 0; ni = child(g, ni, 0)) {
        g->n[ni].x = x; g->n[ni].y = y;
        bottom = place_values(g, ni);
        for (int p = 1; p < g->n[ni].nports; p++) {
            int c = child(g, ni, p);
            if (c >= 0) bottom = place_chain(g, c, x + INDENT + chain_left(g, c), bottom + GAP_Y);
        }
        y = bottom + GAP_Y;
    }
    return bottom;
}

static void autolayout(Graph* g) {
    for (int i = 0; i < g->nn; i++) measure(g, i);
    g->n[0].x = 0; g->n[0].y = 0;
    int first = child(g, 0, 0);
    if (first >= 0) place_chain(g, first, 0, g->n[0].h + GAP_Y);
}

/* ---------- @layout comment ---------- */
/* Reads this method's line of the last '/ * @layout ... * /' comment in src. Items: key:tag:x,y  key=open  key=closed */
static void read_layout(Graph* g) {
    const char* p = NULL, *q = g->src;
    while ((q = strstr(q, "/* @layout")) != NULL) { p = q; q++; }
    if (!p) return;
    const char* end = strstr(p, "*/");
    if (!end) return;
    p += 10;
    size_t nl = strlen(g->name);
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        const char* eol = p;
        while (eol < end && *eol != '\n') eol++;
        if ((size_t)(eol - p) > nl && !memcmp(p, g->name, nl) && (p[nl] == ' ' || p[nl] == '\t')) {
            const char* t = p + nl;
            while (t < eol) {
                while (t < eol && (*t == ' ' || *t == '\t' || *t == '\r')) t++;
                const char* te = t;
                while (te < eol && *te != ' ' && *te != '\t' && *te != '\r') te++;
                if (te > t) {
                    char tok[96]; int len = (int)(te - t); if (len > 95) len = 95;
                    memcpy(tok, t, (size_t)len); tok[len] = 0;
                    GLayoutItem it; memset(&it, 0, sizeof it);
                    char* eq = strchr(tok, '=');
                    char* c1 = strchr(tok, ':');
                    int ok = 0;
                    if (eq) {
                        *eq = 0;
                        it.fold = !strcmp(eq + 1, "open") ? 1 : !strcmp(eq + 1, "closed") ? 2 : 0;
                        ok = it.fold != 0;
                    } else if (c1) {
                        char* c2 = strchr(c1 + 1, ':');
                        *c1 = 0;
                        if (c2) { *c2 = 0; fmt(it.tag, sizeof it.tag, "%s", c1 + 1); ok = sscanf(c2 + 1, "%f,%f", &it.x, &it.y) == 2; }
                    }
                    if (ok && strlen(tok) < sizeof it.key) {
                        fmt(it.key, sizeof it.key, "%s", tok);
                        GLayoutItem* l = (GLayoutItem*)grow(g, g->lay, g->nlay, &g->laycap, sizeof(GLayoutItem));
                        if (g->oom) return;
                        g->lay = l; g->lay[g->nlay++] = it;
                    }
                }
                t = te;
            }
        }
        p = eol;
    }
}

/* Saved positions win. A value node without one moves together with the node it feeds
   (nodes are made in order, so the consumer is already final when its values come). */
static void apply_layout(Graph* g) {
    if (!g->nlay) return;
    float* dx = (float*)calloc((size_t)g->nn * 2, sizeof(float));
    if (!dx) return;
    for (int i = 0; i < g->nn; i++) {
        GNode* n = &g->n[i];
        float ax = n->x, ay = n->y;
        const GLayoutItem* it = find_lay(g, n->key, 0);
        if (it && !strcmp(it->tag, n->tag)) { n->x = it->x; n->y = it->y; n->flags |= GF_PLACED; }
        else if (n->kind == GN_VALUE) { n->x += dx[n->from * 2]; n->y += dx[n->from * 2 + 1]; }
        dx[i * 2] = n->x - ax; dx[i * 2 + 1] = n->y - ay;
    }
    free(dx);
}

void gr_layout_line(Graph* g, MsBuf* out) {
    char s[160];
    ms_buf_add(out, g->name, -1);
    for (int i = 0; i < g->nn; i++) {
        GNode* n = &g->n[i];
        fmt(s, sizeof s, " %s:%s:%d,%d", n->key, n->tag, (int)(n->x + (n->x < 0 ? -0.5f : 0.5f)), (int)(n->y + (n->y < 0 ? -0.5f : 0.5f)));
        ms_buf_add(out, s, -1);
    }
    for (int i = 0; i < g->nlay; i++)       /* open/closed choices are kept as they are */
        if (g->lay[i].fold) {
            fmt(s, sizeof s, " %s=%s", g->lay[i].key, g->lay[i].fold == 1 ? "open" : "closed");
            ms_buf_add(out, s, -1);
        }
}

/* ---------- public ---------- */
Graph* gr_build(ProgramAST* prog, const char* src, const char* cls, int clen, const char* method, int mlen) {
    ClassDecl* cd = NULL; MethodDecl* m = NULL;
    for (int i = 0; i < prog->nclasses && !m; i++) {
        ClassDecl* c = &prog->classes[i];
        if (c->nlen != clen || memcmp(c->name, cls, (size_t)clen)) continue;
        for (int j = 0; j < c->nmethods; j++)
            if (c->methods[j].nlen == mlen && !memcmp(c->methods[j].name, method, (size_t)mlen)) { cd = c; m = &c->methods[j]; break; }
    }
    if (!m) return NULL;
    Graph* g = (Graph*)calloc(1, sizeof(Graph));
    if (!g) return NULL;
    g->src = src; g->cls = cd; g->m = m;
    fmt(g->name, sizeof g->name, "%.*s.%.*s", clen, cls, mlen, method);
    read_layout(g);
    int e = add_node(g, GN_ENTRY, NULL, NULL, "e", -1, 0);
    if (e >= 0) {
        GNode* n = &g->n[e];
        fmt(n->tag, sizeof n->tag, "entry");
        n->nports = 1;
        char rt[32] = "";
        if (!m->isCtor && m->ret) title_of_type(rt, sizeof rt, m->ret);
        int len = fmt(n->title, sizeof n->title, "%s%s%s%.*s(", m->isStatic ? "static " : "", rt, *rt ? " " : "", mlen, method);
        for (int i = 0; i < m->nparams && len < (int)sizeof n->title; i++)
            len += fmt(n->title + len, sizeof n->title - (size_t)len, "%s%.*s", i ? ", " : "", m->params[i].nlen, m->params[i].name);
        if (len < (int)sizeof n->title) fmt(n->title + len, sizeof n->title - (size_t)len, ")");
        if (m->body) build_chain(g, m->body, "", e, 0);
    }
    if (g->oom) { gr_free(g); return NULL; }
    autolayout(g);
    apply_layout(g);
    return g;
}

void gr_free(Graph* g) {
    if (!g) return;
    free(g->n); free(g->in); free(g->lay); free(g);
}

void gr_port_label(Graph* g, int node, int port, char* out, int outlen) {
    GNode* n = &g->n[node];
    if (port == 0) { fmt(out, (size_t)outlen, "next"); return; }
    if (n->kind == GN_FLOW && n->st->k == S_IF) { fmt(out, (size_t)outlen, port == 1 ? "true" : "false"); return; }
    if (n->kind == GN_FLOW && n->st->k == S_SWITCH && port - 1 < n->st->n) {
        Stmt* sec = n->st->list[port - 1];
        if (sec->nlabels) {
            char t[40]; GInput tmp = { "", sec->labels[0], sec->labels[0]->s0, sec->labels[0]->s1, -1 };
            gr_input_text(g, &tmp, t, 16);
            fmt(out, (size_t)outlen, "case %s%s", t, sec->nlabels > 1 || sec->isDefault ? ".." : "");
        } else fmt(out, (size_t)outlen, "default");
        return;
    }
    fmt(out, (size_t)outlen, "body");
}

void gr_input_text(Graph* g, GInput* in, char* out, int max) {
    (void)g;
    MsBuf b = { 0 };
    const char* s = in->s0; int len = s && in->s1 ? (int)(in->s1 - in->s0) : 0;
    if (!s && in->ex) { ms_print_expr(&b, in->ex); s = b.p; len = b.n; }
    int j = 0, space = 0;
    for (int i = 0; s && i < len && j < max; i++) {     /* one line: runs of white space become one space */
        char c = s[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') { space = j > 0; continue; }
        if (space) { out[j++] = ' '; space = 0; if (j >= max) break; }
        out[j++] = c;
    }
    if (j >= max && max > 3) { j = max - 3; memcpy(out + j, "...", 3); j += 3; }
    out[j] = 0;
    ms_buf_free(&b);
}

void gr_dump(Graph* g, MsBuf* out) {
    char s[200];
    fmt(s, sizeof s, "graph %s (%d nodes)\n", g->name, g->nn);
    ms_buf_add(out, s, -1);
    for (int i = 0; i < g->nn; i++) {
        GNode* n = &g->n[i];
        char from[64] = "";
        if (n->kind == GN_FLOW) {
            char pl[40]; gr_port_label(g, n->from, n->fromPort, pl, sizeof pl);
            fmt(from, sizeof from, " <- n%d.%s", n->from, pl);
        } else if (n->kind == GN_VALUE) fmt(from, sizeof from, " -> n%d.%s", n->from, g->in[g->n[n->from].in0 + n->fromPort].label);
        fmt(s, sizeof s, "n%d %s %s \"%s\"%s @%d,%d %dx%d%s\n", i, n->key, n->tag, n->title, from,
                 (int)n->x, (int)n->y, (int)n->w, (int)n->h, n->flags & GF_PLACED ? " placed" : "");
        ms_buf_add(out, s, -1);
        for (int k = 0; k < n->nin; k++) {
            GInput* in = &g->in[n->in0 + k];
            if (in->node >= 0) fmt(s, sizeof s, "    %s <- n%d\n", in->label, in->node);
            else { char t[64]; gr_input_text(g, in, t, 60); fmt(s, sizeof s, "    %s: %s\n", in->label, t); }
            ms_buf_add(out, s, -1);
        }
    }
}
