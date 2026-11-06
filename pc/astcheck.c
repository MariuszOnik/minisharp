/* MiniSharp - checks the source spans of the AST (PC test tool, `minisharp --spans file.cs ...`).
   For every class, member, statement and expression the text [s0, s1) is parsed again on its own
   and must give the same tree (line numbers and spans are ignored). The graph editor relies on this:
   it edits code by replacing exactly these spans. */
#include "../src/ast.h"
#include "astcheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- structural equality ---------- */
static int eq_name(const char* a, int al, const char* b, int bl) { return al == bl && (al == 0 || !memcmp(a, b, (size_t)al)); }
static int eq_stmt(Stmt* a, Stmt* b);

static int eq_type(TypeNode* a, TypeNode* b) {
    if (!a || !b) return a == b;
    if (a->base != b->base || a->dims != b->dims || a->nargs != b->nargs) return 0;
    if (!eq_name(a->name, a->nlen, b->name, b->nlen)) return 0;
    for (int i = 0; i < a->nargs; i++) if (!eq_type(a->args[i], b->args[i])) return 0;
    return 1;
}

static int eq_params(Param* a, int na, Param* b, int nb) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++)
        if (!eq_type(a[i].tn, b[i].tn) || !eq_name(a[i].name, a[i].nlen, b[i].name, b[i].nlen)) return 0;
    return 1;
}

static int eq_expr(Expr* a, Expr* b) {
    if (!a || !b) return a == b;
    if (a->k != b->k || a->op != b->op || a->ival != b->ival || memcmp(&a->fval, &b->fval, sizeof a->fval)) return 0;
    if (a->prefix != b->prefix || a->nargs != b->nargs) return 0;
    if (!eq_name(a->name, a->nlen, b->name, b->nlen)) return 0;
    if (!eq_expr(a->a, b->a) || !eq_expr(a->b, b->b) || !eq_expr(a->c, b->c)) return 0;
    for (int i = 0; i < a->nargs; i++) if (!eq_expr(a->args[i], b->args[i])) return 0;
    return eq_type(a->tn, b->tn) && eq_params(a->params, a->nparams, b->params, b->nparams) && eq_stmt(a->body, b->body);
}

static int eq_stmt(Stmt* a, Stmt* b) {
    if (!a || !b) return a == b;
    if (a->k != b->k || a->n != b->n || a->nlabels != b->nlabels || a->isDefault != b->isDefault) return 0;
    if (!eq_name(a->name, a->nlen, b->name, b->nlen) || !eq_type(a->tn, b->tn)) return 0;
    if (!eq_expr(a->e, b->e) || !eq_expr(a->step, b->step)) return 0;
    if (!eq_stmt(a->body, b->body) || !eq_stmt(a->els, b->els) || !eq_stmt(a->init, b->init)) return 0;
    for (int i = 0; i < a->n; i++) if (!eq_stmt(a->list[i], b->list[i])) return 0;
    for (int i = 0; i < a->nlabels; i++) if (!eq_expr(a->labels[i], b->labels[i])) return 0;
    return 1;
}

static int eq_method(MethodDecl* a, MethodDecl* b) {
    return a->isStatic == b->isStatic && a->isCtor == b->isCtor && a->isIter == b->isIter && a->kind == b->kind &&
           a->exprBody == b->exprBody && eq_type(a->ret, b->ret) && eq_name(a->name, a->nlen, b->name, b->nlen) &&
           eq_params(a->params, a->nparams, b->params, b->nparams) && eq_stmt(a->body, b->body) && eq_expr(a->baseCall, b->baseCall);
}

static int eq_field(FieldDecl* a, FieldDecl* b) {
    return a->isStatic == b->isStatic && eq_type(a->tn, b->tn) && eq_name(a->name, a->nlen, b->name, b->nlen) && eq_expr(a->init, b->init);
}

static int eq_class(ClassDecl* a, ClassDecl* b) {
    if (!eq_name(a->name, a->nlen, b->name, b->nlen) || a->isAbstract != b->isAbstract || a->isInterface != b->isInterface) return 0;
    if (a->nbases != b->nbases || a->nfields != b->nfields || a->nmethods != b->nmethods) return 0;
    for (int i = 0; i < a->nbases; i++) if (!eq_type(a->bases[i], b->bases[i])) return 0;
    for (int i = 0; i < a->nfields; i++) if (!eq_field(&a->fields[i], &b->fields[i])) return 0;
    for (int i = 0; i < a->nmethods; i++) if (!eq_method(&a->methods[i], &b->methods[i])) return 0;
    return 1;
}

/* ---------- reparsing a span ---------- */
typedef struct { const char* file; const char* src; int checked, failed, nospan; } Ck;

static int line_of(Ck* k, const char* s) { int n = 1; for (const char* p = k->src; p < s; p++) if (*p == '\n') n++; return n; }

/* Parses pre + text + post as a whole program into arena A; NULL on a syntax error. */
static ProgramAST* parse_wrapped(Arena* A, const char* pre, const char* s0, const char* s1, const char* post) {
    size_t lp = strlen(pre), lt = (size_t)(s1 - s0), lq = strlen(post);
    char* buf = (char*)arena_alloc(A, lp + lt + lq + 1);
    memcpy(buf, pre, lp); memcpy(buf + lp, s0, lt); memcpy(buf + lp + lt, post, lq); buf[lp + lt + lq] = 0;
    char err[256]; jmp_buf jb;
    ParseCtx c = { A, &jb, err, sizeof err };
    if (setjmp(jb)) return NULL;
    int n; Token* t = ms_lex(&c, buf, &n);
    return ms_parse(&c, t, n);
}

static void report(Ck* k, const char* what, const char* s0, const char* s1) {
    k->failed++;
    int len = (int)(s1 - s0); if (len > 70) len = 70;
    char shown[80]; int j = 0;
    for (int i = 0; i < len; i++) shown[j++] = (s0[i] == '\n' || s0[i] == '\r' || s0[i] == '\t') ? ' ' : s0[i];
    shown[j] = 0;
    printf("%s:%d: %s span does not reparse to the same tree: %s\n", k->file, line_of(k, s0), what, shown);
}

/* the single statement of 'class C { void M() { ... } }' */
static Stmt* only_stmt(ProgramAST* pr) {
    if (!pr || pr->nclasses != 1 || pr->classes[0].nmethods != 1) return NULL;
    Stmt* b = pr->classes[0].methods[0].body;
    return b && b->n == 1 ? b->list[0] : NULL;
}

static int has_span(Ck* k, const char* s0, const char* s1) {
    if (!s0 || !s1) { k->nospan++; return 0; }
    return 1;
}

static int is_type_word(const char* s, const char* e) {
    static const char* w[] = { "int", "float", "double", "bool", "string", "char" };
    for (size_t i = 0; i < sizeof w / sizeof w[0]; i++) if (eq_name(s, (int)(e - s), w[i], (int)strlen(w[i]))) return 1;
    return 0;
}

static void check_stmt(Ck* k, Stmt* s, int isForInit);

static void check_expr(Ck* k, Expr* e) {
    if (!e) return;
    /* parts that are not expressions on their own: checked through their parent and children */
    int alone = e->k != E_PAIR && e->k != E_OUT && e->k != E_BASE && e->k != E_STACK &&
                !(e->k == E_NAME && e->s0 && is_type_word(e->s0, e->s1));    /* 'int' in int.Parse(s) */
    if (alone && has_span(k, e->s0, e->s1)) {
        Arena A = { 0 };
        Stmt* s = only_stmt(parse_wrapped(&A, "class C { void M() { var v = ", e->s0, e->s1, "; } }"));
        k->checked++;
        if (!s || s->k != S_VAR || s->els || !eq_expr(s->e, e)) report(k, "expression", e->s0, e->s1);
        arena_free(&A);
    }
    check_expr(k, e->a); check_expr(k, e->b); check_expr(k, e->c);
    for (int i = 0; i < e->nargs; i++) check_expr(k, e->args[i]);
    check_stmt(k, e->body, 0);
}

static void check_stmt(Ck* k, Stmt* s, int isForInit) {
    if (!s) return;
    if (s->k != S_CASE && has_span(k, s->s0, s->s1)) {
        Arena A = { 0 };
        Stmt* r = only_stmt(parse_wrapped(&A, "class C { void M() { ", s->s0, s->s1, isForInit ? "; } }" : " } }"));
        k->checked++;
        if (!eq_stmt(r, s)) report(k, "statement", s->s0, s->s1);
        arena_free(&A);
    }
    check_expr(k, s->e); check_expr(k, s->step);
    check_stmt(k, s->body, 0); check_stmt(k, s->init, 1);
    if (s->k != S_VAR) check_stmt(k, s->els, 0);       /* for S_VAR 'els' chains 'int a, b' (checked with the first one) */
    else for (Stmt* v = s->els; v; v = v->els) check_expr(k, v->e);
    for (int i = 0; i < s->n; i++) check_stmt(k, s->list[i], 0);
    for (int i = 0; i < s->nlabels; i++) check_expr(k, s->labels[i]);
}

static void check_class(Ck* k, ClassDecl* cd) {
    char pre[160];
    snprintf(pre, sizeof pre, "%s %.*s { ", cd->isInterface ? "interface" : cd->isAbstract ? "abstract class" : "class", cd->nlen, cd->name);
    if (has_span(k, cd->s0, cd->s1)) {
        Arena A = { 0 };
        ProgramAST* pr = parse_wrapped(&A, "", cd->s0, cd->s1, "");
        k->checked++;
        if (!pr || pr->nclasses != 1 || !eq_class(&pr->classes[0], cd)) report(k, "class", cd->s0, cd->s1);
        arena_free(&A);
    }
    for (int i = 0; i < cd->nfields; i++) {
        FieldDecl* f = &cd->fields[i];
        if (has_span(k, f->s0, f->s1)) {
            Arena A = { 0 };
            ProgramAST* pr = parse_wrapped(&A, pre, f->s0, f->s1, " }");
            k->checked++;
            int ok = 0;     /* 'int a, b;' gives several fields from one span: find the same one */
            if (pr && pr->nclasses == 1)
                for (int j = 0; j < pr->classes[0].nfields; j++) ok |= eq_field(&pr->classes[0].fields[j], f);
            if (!ok) report(k, "field", f->s0, f->s1);
            arena_free(&A);
        }
        check_expr(k, f->init);
    }
    for (int i = 0; i < cd->nmethods; i++) {
        MethodDecl* m = &cd->methods[i];
        if (has_span(k, m->s0, m->s1)) {
            Arena A = { 0 };
            ProgramAST* pr = parse_wrapped(&A, pre, m->s0, m->s1, " }");
            k->checked++;
            if (!pr || pr->nclasses != 1 || pr->classes[0].nmethods != 1 || !eq_method(&pr->classes[0].methods[0], m))
                report(k, "method", m->s0, m->s1);
            arena_free(&A);
        }
        if (m->baseCall) for (int j = 0; j < m->baseCall->nargs; j++) check_expr(k, m->baseCall->args[j]);
        if (m->exprBody) check_expr(k, m->body->list[0]->e);      /* '=> expr': the block and statement are made up */
        else check_stmt(k, m->body, 0);
    }
}

int ms_check_spans(const char* file, const char* src, int* checked) {
    Ck k = { file, src, 0, 0, 0 };
    Arena A = { 0 };
    char err[256]; jmp_buf jb;
    ParseCtx c = { &A, &jb, err, sizeof err };
    if (setjmp(jb)) { printf("%s: %s\n", file, err); arena_free(&A); return 1; }
    int n; Token* t = ms_lex(&c, src, &n);
    ProgramAST* pr = ms_parse(&c, t, n);
    for (int i = 0; i < pr->nclasses; i++) check_class(&k, &pr->classes[i]);
    arena_free(&A);
    *checked += k.checked;
    if (k.failed) printf("%s: %d of %d spans wrong\n", file, k.failed, k.checked);
    return k.failed ? 1 : 0;
}

/* ---------- printer round trip ---------- */
#include "../src/printer.h"
static int check_graphs(const char* file, const char* src, ProgramAST* pr);

static ProgramAST* parse_text(Arena* A, const char* src, char* err, int errlen) {
    jmp_buf jb;
    ParseCtx c = { A, &jb, err, errlen };
    if (setjmp(jb)) return NULL;
    int n; Token* t = ms_lex(&c, src, &n);
    return ms_parse(&c, t, n);
}

int ms_print_file(const char* src, char* out_err, int errlen, char** out) {
    Arena A = { 0 };
    *out = NULL;
    ProgramAST* pr = parse_text(&A, src, out_err, errlen);
    if (!pr) { arena_free(&A); return 1; }
    MsBuf b = { 0 };
    ms_print_program(&b, pr);
    arena_free(&A);
    *out = b.p;
    return b.oom;
}

int ms_check_print(const char* file, const char* src) {
    Arena A = { 0 }, B = { 0 };
    char err[256];
    ProgramAST* pr = parse_text(&A, src, err, sizeof err);
    if (!pr) { printf("%s: %s\n", file, err); arena_free(&A); return 1; }
    MsBuf p1 = { 0 }, p2 = { 0 };
    ms_print_program(&p1, pr);
    int bad = 0;
    ProgramAST* re = parse_text(&B, p1.p, err, sizeof err);
    if (!re) { printf("%s: printed code does not parse: %s\n", file, err); bad = 1; }
    else if (re->nclasses != pr->nclasses) { printf("%s: printed code has %d classes, not %d\n", file, re->nclasses, pr->nclasses); bad = 1; }
    else {
        for (int i = 0; i < pr->nclasses; i++)
            if (!eq_class(&re->classes[i], &pr->classes[i])) {
                printf("%s: printed class '%.*s' parses to a different tree\n", file, pr->classes[i].nlen, pr->classes[i].name);
                bad = 1;
            }
        ms_print_program(&p2, re);
        bad |= check_graphs(file, src, pr);
        if (!bad && (!p2.p || strcmp(p1.p, p2.p))) { printf("%s: printing the printed code changes it\n", file); bad = 1; }
    }
    ms_buf_free(&p1); ms_buf_free(&p2);
    arena_free(&A); arena_free(&B);
    return bad;
}

/* ---------- graphs ---------- */
#include "../src/graph.h"

/* statements that become flow nodes: everything in a chain (not for-init, not 'int a, b' parts, not lambda bodies) */
static int count_flow(Stmt* s) {
    if (!s) return 0;
    int n = 0;
    Stmt** list = s->k == S_BLOCK ? s->list : &s;
    int cnt = s->k == S_BLOCK ? s->n : 1;
    for (int i = 0; i < cnt; i++) {
        Stmt* t = list[i];
        n++;
        switch (t->k) {
            case S_IF: n += count_flow(t->body) + count_flow(t->els); break;
            case S_WHILE: case S_DO: case S_FOR: case S_FOREACH: n += count_flow(t->body); break;
            case S_BLOCK: n += count_flow(t); break;
            case S_SWITCH: for (int j = 0; j < t->n; j++) { Stmt b = { 0 }; b.k = S_BLOCK; b.list = t->list[j]->list; b.n = t->list[j]->n; n += count_flow(&b); } break;
        }
    }
    return n;
}

static int check_graphs(const char* file, const char* src, ProgramAST* pr) {
    int bad = 0;
    for (int c = 0; c < pr->nclasses; c++) {
        ClassDecl* cd = &pr->classes[c];
        for (int j = 0; j < cd->nmethods; j++) {
            MethodDecl* m = &cd->methods[j];
            Graph* g = gr_build(pr, src, cd->name, cd->nlen, m->name, m->nlen);
            if (!g) { printf("%s: no graph for %.*s.%.*s\n", file, cd->nlen, cd->name, m->nlen, m->name); bad = 1; continue; }
            if (g->m != m) { gr_free(g); continue; }        /* an overload with the same name: the graph shows the first one */
            int flow = 0;
            for (int i = 0; i < g->nn; i++) {
                if (g->n[i].kind != GN_FLOW) continue;
                flow++;
                for (int k = i + 1; k < g->nn; k++)
                    if (g->n[k].kind == GN_FLOW && g->n[k].st == g->n[i].st) { printf("%s: %s: statement shown twice\n", file, g->name); bad = 1; }
            }
            int want = m->body ? count_flow(m->body) : 0;
            if (flow != want) { printf("%s: %s: %d flow nodes for %d statements\n", file, g->name, flow, want); bad = 1; }
            gr_free(g);
        }
    }
    return bad;
}

/* `--graph file.cs [Class.Method]`: dump one graph or all of them; `--graph-layout file.cs`: the @layout comment */
int ms_graph_tool(const char* src, const char* which, int layoutOnly) {
    Arena A = { 0 };
    char err[256];
    ProgramAST* pr = parse_text(&A, src, err, sizeof err);
    if (!pr) { fprintf(stderr, "%s\n", err); arena_free(&A); return 1; }
    MsBuf out = { 0 };
    int found = 0;
    if (layoutOnly) ms_buf_add(&out, "/* @layout\n", -1);
    for (int c = 0; c < pr->nclasses; c++) {
        ClassDecl* cd = &pr->classes[c];
        for (int j = 0; j < cd->nmethods; j++) {
            MethodDecl* m = &cd->methods[j];
            char name[160];
            snprintf(name, sizeof name, "%.*s.%.*s", cd->nlen, cd->name, m->nlen, m->name);
            if (which && strcmp(which, name)) continue;
            Graph* g = gr_build(pr, src, cd->name, cd->nlen, m->name, m->nlen);
            if (!g || g->m != m) { gr_free(g); continue; }
            found++;
            if (layoutOnly) { gr_layout_line(g, &out); ms_buf_add(&out, "\n", 1); }
            else { if (found > 1) ms_buf_add(&out, "\n", 1); gr_dump(g, &out); }
            gr_free(g);
        }
    }
    if (layoutOnly) ms_buf_add(&out, "*/\n", -1);
    if (out.p) fputs(out.p, stdout);
    ms_buf_free(&out);
    arena_free(&A);
    if (!found) { fprintf(stderr, "no method %s\n", which ? which : "(none in the file)"); return 1; }
    return 0;
}

/* `--graph-edit file.cs script.txt`: applies graph actions one by one and prints the resulting source.
   Script lines (keys are node keys of the graph as it is at that moment, see --graph):
     method Class.Method      set KEY INPUT text      insert KEY PORT text (PORT: next true false body cN)
     delete KEY               move KEY X Y            open KEY INPUT / close KEY INPUT      dump
   A failed action prints "error: ..." and the script goes on. */
static char* edit_step(char* src, const char* method, const char* line, MsBuf* log) {
    Arena A = { 0 };
    char err[256] = "";
    ProgramAST* pr = parse_text(&A, src, err, sizeof err);
    if (!pr) { ms_buf_add(log, "error: the code does not parse\n", -1); arena_free(&A); return src; }
    const char* dot = strchr(method, '.');
    Graph* g = dot ? gr_build(pr, src, method, (int)(dot - method), dot + 1, (int)strlen(dot + 1)) : NULL;
    if (!g) { ms_buf_add(log, "error: no such method\n", -1); arena_free(&A); return src; }
    char cmd[16] = "", key[48] = "", a1[48] = "";
    int off = 0;
    sscanf(line, "%15s %47s %n", cmd, key, &off);
    const char* rest = off ? line + off : "";
    int node = gr_find(g, key);
    char* out = NULL;
    MsPatch p = { 0, 0, NULL };
    int ok = 0;
    if (!strcmp(cmd, "dump")) { gr_dump(g, log); gr_free(g); arena_free(&A); return src; }
    if (node < 0) snprintf(err, sizeof err, "no node %s", key);
    else if (!strcmp(cmd, "set") || !strcmp(cmd, "insert")) {
        int o2 = 0;
        sscanf(rest, "%47s %n", a1, &o2);
        const char* text = rest + o2;
        if (!strcmp(cmd, "set")) ok = gr_set_input(g, node, atoi(a1), text, &p, err, sizeof err);
        else {
            int port = !strcmp(a1, "next") ? 0 : !strcmp(a1, "true") || !strcmp(a1, "body") ? 1 : !strcmp(a1, "false") ? 2 : a1[0] == 'c' ? 1 + atoi(a1 + 1) : -1;
            ok = gr_insert(g, node, port, text, &p, err, sizeof err);
        }
        if (ok) out = gr_apply(g, &p, err, sizeof err, NULL, 0);
    } else if (!strcmp(cmd, "delete")) {
        ok = gr_delete(g, node, &p, err, sizeof err);
        if (ok) out = gr_apply(g, &p, err, sizeof err, NULL, 0);
    } else if (!strcmp(cmd, "move")) {
        float x = 0, y = 0;
        sscanf(rest, "%f %f", &x, &y);
        out = gr_move(g, node, x, y);
    } else if (!strcmp(cmd, "open") || !strcmp(cmd, "close")) out = gr_fold(g, node, atoi(rest), !strcmp(cmd, "open"));
    else snprintf(err, sizeof err, "unknown command %s", cmd);
    ms_patch_free(&p);
    gr_free(g);
    arena_free(&A);
    if (!out) {
        ms_buf_add(log, "error: ", -1); ms_buf_add(log, err[0] ? err : "failed", -1); ms_buf_add(log, "\n", 1);
        return src;
    }
    free(src);
    return out;
}

int ms_graph_edit_tool(const char* src0, const char* script) {
    char* src = (char*)malloc(strlen(src0) + 1);
    if (!src) return 1;
    strcpy(src, src0);
    char method[160] = "";
    MsBuf log = { 0 };
    const char* p = script;
    while (*p) {
        const char* e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        char line[512];
        if (len > 511) len = 511;
        memcpy(line, p, (size_t)len); line[len] = 0;
        if (len && line[len - 1] == '\r') line[--len] = 0;
        p = e ? e + 1 : p + len;
        if (!len || line[0] == '#') continue;
        ms_buf_add(&log, "> ", -1); ms_buf_add(&log, line, -1); ms_buf_add(&log, "\n", 1);
        if (!strncmp(line, "method ", 7)) { snprintf(method, sizeof method, "%.150s", line + 7); continue; }
        src = edit_step(src, method, line, &log);
    }
    if (log.p) fputs(log.p, stdout);
    fputs("----\n", stdout);
    fputs(src, stdout);
    ms_buf_free(&log);
    free(src);
    return 0;
}
