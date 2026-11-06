/* MiniSharp - AST printer (see printer.h). Style: 4-space indent, '{' on the same line,
   parentheses only where operator precedence needs them. Printing a parsed program and parsing
   the text again gives the same tree; `minisharp --spans` checks that on every test file. */
#include "printer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- buffer ---------- */
void ms_buf_free(MsBuf* b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

static void put(MsBuf* b, const char* s, int len) {
    if (b->oom) return;
    if (b->n + len + 1 > b->cap) {
        int cap = b->cap ? b->cap : 256;
        while (b->n + len + 1 > cap) cap *= 2;
        char* p = (char*)realloc(b->p, (size_t)cap);
        if (!p) { b->oom = 1; return; }
        b->p = p; b->cap = cap;
    }
    memcpy(b->p + b->n, s, (size_t)len);
    b->n += len;
    b->p[b->n] = 0;
}
void ms_buf_add(MsBuf* b, const char* s, int len) { put(b, s, len < 0 ? (int)strlen(s) : len); }
static void puts_(MsBuf* b, const char* s) { put(b, s, (int)strlen(s)); }
static void putc_(MsBuf* b, char c) { put(b, &c, 1); }
static void indent(MsBuf* b, int level) { for (int i = 0; i < level; i++) puts_(b, "    "); }

/* ---------- types ---------- */
static void print_type_base(MsBuf* b, TypeNode* tn) {    /* name and <args>, without [] */
    put(b, tn->name, tn->nlen);
    if (tn->nargs) {
        putc_(b, '<');
        for (int i = 0; i < tn->nargs; i++) { if (i) puts_(b, ", "); ms_print_type(b, tn->args[i]); }
        putc_(b, '>');
    }
}
static void print_dims(MsBuf* b, int dims) { for (int i = 0; i < dims; i++) puts_(b, "[]"); }
void ms_print_type(MsBuf* b, TypeNode* tn) { print_type_base(b, tn); print_dims(b, tn->dims); }

/* ---------- expressions ---------- */
/* precedence, lowest first: assignment / lambda, ?:, ??, binary operators (4..13), unary and casts, postfix and primary */
enum { P_ASSIGN = 1, P_COND = 2, P_COALESCE = 3, P_BINARY = 3, P_UNARY = 14, P_POSTFIX = 15 };

static int binprec(int op) {      /* same table as the parser */
    switch (op) {
        case TK_OR: return 1;
        case TK_AND: return 2;
        case TK_PIPE: return 3;
        case TK_CARET: return 4;
        case TK_AMP: return 5;
        case TK_EQ: case TK_NE: return 6;
        case TK_LT: case TK_LE: case TK_GT: case TK_GE: return 7;
        case TK_SHL: case TK_SHR: return 8;
        case TK_PLUS: case TK_MINUS: return 9;
        default: return 10;       /* * / % */
    }
}

static const char* opstr(int op) {
    switch (op) {
        case TK_OR: return "||"; case TK_AND: return "&&"; case TK_PIPE: return "|"; case TK_CARET: return "^"; case TK_AMP: return "&";
        case TK_EQ: return "=="; case TK_NE: return "!="; case TK_LT: return "<"; case TK_LE: return "<="; case TK_GT: return ">"; case TK_GE: return ">=";
        case TK_SHL: return "<<"; case TK_SHR: return ">>";
        case TK_PLUS: return "+"; case TK_MINUS: return "-"; case TK_STAR: return "*"; case TK_SLASH: return "/"; case TK_PERCENT: return "%";
        case TK_BANG: return "!"; case TK_TILDE: return "~"; case TK_INC: return "++"; case TK_DEC: return "--";
        case TK_ASSIGN: return "="; case TK_PLUSEQ: return "+="; case TK_MINUSEQ: return "-="; case TK_STAREQ: return "*="; case TK_SLASHEQ: return "/=";
        case TK_PERCENTEQ: return "%="; case TK_AMPEQ: return "&="; case TK_PIPEEQ: return "|="; case TK_CARETEQ: return "^=";
        case TK_SHLEQ: return "<<="; case TK_SHREQ: return ">>=";
        case TK_KINT: return "int"; case TK_KFLOAT: return "float"; case TK_KCHAR: return "char";
        default: return "?";
    }
}

static int negative_literal(Expr* e) {
    return (e->k == E_INT && e->ival < 0) || (e->k == E_FLOAT && signbit(e->fval));
}

static int prec(Expr* e) {
    switch (e->k) {
        case E_ASSIGN: case E_LAMBDA: return P_ASSIGN;
        case E_COND: return P_COND;
        case E_COALESCE: return P_COALESCE;
        case E_BINARY: case E_LOGIC: return P_BINARY + binprec(e->op);
        case E_IS: case E_AS: return P_BINARY + 7;
        case E_UNARY: case E_CAST: return P_UNARY;
        case E_INCDEC: return e->prefix ? P_UNARY : P_POSTFIX;
        case E_INT: case E_FLOAT: return negative_literal(e) ? P_UNARY : P_POSTFIX;   /* -5 is written with a unary minus */
        default: return P_POSTFIX;
    }
}

static void expr(MsBuf* b, Expr* e, int minprec, int level);
static void stmt(MsBuf* b, Stmt* s, int level);
static void block(MsBuf* b, Stmt* s, int level);

/* does the printed text of e start with '-' or '+' ? (then a unary operator or a named cast before it needs parentheses) */
static int starts_with_sign(Expr* e) {
    return negative_literal(e) || (e->k == E_UNARY && e->op == TK_MINUS) || (e->k == E_INCDEC && e->prefix);
}

static void print_float(MsBuf* b, float f) {
    char s[48];
    for (int digits = 1; digits <= 9; digits++) {          /* shortest text that reads back as the same float */
        snprintf(s, sizeof s, "%.*g", digits, (double)f);
        if (strtof(s, NULL) == f) break;
    }
    puts_(b, s);
    if (!strpbrk(s, ".e")) puts_(b, ".0");
    putc_(b, 'f');
}

static void print_char_escaped(MsBuf* b, int ch, char quote) {
    switch (ch) {
        case '\n': puts_(b, "\\n"); return;
        case '\t': puts_(b, "\\t"); return;
        case '\r': puts_(b, "\\r"); return;
        case 0: puts_(b, "\\0"); return;
        case '\\': puts_(b, "\\\\"); return;
    }
    if (ch == quote) { putc_(b, '\\'); putc_(b, quote); return; }
    putc_(b, (char)ch);
}

static void args(MsBuf* b, Expr** a, int n, int level) {
    for (int i = 0; i < n; i++) { if (i) puts_(b, ", "); expr(b, a[i], P_ASSIGN, level); }
}

static void expr(MsBuf* b, Expr* e, int minprec, int level) {
    int paren = prec(e) < minprec;
    if (paren) putc_(b, '(');
    char num[32];
    switch (e->k) {
        case E_INT: snprintf(num, sizeof num, "%ld", (long)e->ival); puts_(b, num); break;
        case E_FLOAT: print_float(b, e->fval); break;
        case E_BOOL: puts_(b, e->ival ? "true" : "false"); break;
        case E_NULL: puts_(b, "null"); break;
        case E_THIS: puts_(b, "this"); break;
        case E_BASE: puts_(b, "base"); break;
        case E_NAME: put(b, e->name, e->nlen); break;
        case E_CHAR: putc_(b, '\''); print_char_escaped(b, e->ival, '\''); putc_(b, '\''); break;
        case E_STRING:
            putc_(b, '"');
            for (int i = 0; i < e->nlen; i++) print_char_escaped(b, (unsigned char)e->name[i], '"');
            putc_(b, '"');
            break;
        case E_MEMBER:
            expr(b, e->a, P_POSTFIX, level);
            puts_(b, e->prefix ? "?." : ".");
            put(b, e->name, e->nlen);
            break;
        case E_CALL: expr(b, e->a, P_POSTFIX, level); putc_(b, '('); args(b, e->args, e->nargs, level); putc_(b, ')'); break;
        case E_INDEX: expr(b, e->a, P_POSTFIX, level); putc_(b, '['); expr(b, e->b, P_ASSIGN, level); putc_(b, ']'); break;
        case E_INCDEC:
            if (e->prefix) {
                puts_(b, opstr(e->op));
                expr(b, e->a, starts_with_sign(e->a) ? P_POSTFIX : P_UNARY, level);    /* ++(-x) is not --x */
            } else { expr(b, e->a, P_POSTFIX, level); puts_(b, opstr(e->op)); }
            break;
        case E_UNARY:
            puts_(b, opstr(e->op));
            expr(b, e->a, starts_with_sign(e->a) ? P_POSTFIX : P_UNARY, level);        /* -(-x) is not --x */
            break;
        case E_CAST:
            putc_(b, '(');
            if (e->tn) ms_print_type(b, e->tn); else puts_(b, opstr(e->op));
            putc_(b, ')');
            /* '(Enemy)-x' does not parse as a cast in C#, so a named cast keeps the operand in parentheses then */
            expr(b, e->a, e->tn && starts_with_sign(e->a) ? P_POSTFIX : P_UNARY, level);
            break;
        case E_BINARY: case E_LOGIC: {
            int p = prec(e);
            expr(b, e->a, p, level);
            putc_(b, ' '); puts_(b, opstr(e->op)); putc_(b, ' ');
            expr(b, e->b, p + 1, level);
            break;
        }
        case E_IS: case E_AS:
            expr(b, e->a, prec(e), level);
            puts_(b, e->k == E_IS ? " is " : " as ");
            if (e->op) puts_(b, "not ");
            if (e->tn) ms_print_type(b, e->tn); else puts_(b, "null");
            if (e->name) { putc_(b, ' '); put(b, e->name, e->nlen); }
            break;
        case E_COALESCE:
            expr(b, e->a, P_COALESCE + 1, level);
            puts_(b, " ?? ");
            expr(b, e->b, e->b->k == E_LAMBDA ? P_ASSIGN : P_COALESCE, level);
            break;
        case E_COND:
            expr(b, e->a, P_COALESCE, level);
            puts_(b, " ? "); expr(b, e->b, P_ASSIGN, level);
            puts_(b, " : "); expr(b, e->c, P_COND, level);
            break;
        case E_ASSIGN:
            expr(b, e->a, P_COND, level);
            putc_(b, ' '); puts_(b, opstr(e->op)); putc_(b, ' ');
            expr(b, e->b, P_ASSIGN, level);
            break;
        case E_NEW: puts_(b, "new "); ms_print_type(b, e->tn); putc_(b, '('); args(b, e->args, e->nargs, level); putc_(b, ')'); break;
        case E_NEWLIST:
            puts_(b, "new "); ms_print_type(b, e->tn);
            if (e->a || !e->args) { putc_(b, '('); if (e->a) expr(b, e->a, P_ASSIGN, level); putc_(b, ')'); }
            if (e->args) { puts_(b, " { "); args(b, e->args, e->nargs, level); puts_(b, " }"); }
            break;
        case E_NEWARR:
            puts_(b, "new "); print_type_base(b, e->tn);
            if (e->prefix) {
                puts_(b, "[]"); print_dims(b, e->tn->dims);
                puts_(b, e->nargs ? " { " : " {");
                args(b, e->args, e->nargs, level);
                puts_(b, " }");
            } else {
                putc_(b, '['); expr(b, e->a, P_ASSIGN, level); putc_(b, ']');
                print_dims(b, e->tn->dims);
            }
            break;
        case E_PAIR:
            if (e->prefix) { putc_(b, '['); expr(b, e->a, P_ASSIGN, level); puts_(b, "] = "); expr(b, e->b, P_ASSIGN, level); }
            else { puts_(b, "{ "); expr(b, e->a, P_ASSIGN, level); puts_(b, ", "); expr(b, e->b, P_ASSIGN, level); puts_(b, " }"); }
            break;
        case E_OUT:
            puts_(b, "out ");
            if (e->prefix) {
                if (e->tn) ms_print_type(b, e->tn); else puts_(b, "var");
                putc_(b, ' '); put(b, e->name, e->nlen);
            } else expr(b, e->a, P_UNARY, level);
            break;
        case E_LAMBDA:
            if (e->nparams == 1 && !e->params[0].tn) put(b, e->params[0].name, e->params[0].nlen);
            else {
                putc_(b, '(');
                for (int i = 0; i < e->nparams; i++) {
                    if (i) puts_(b, ", ");
                    if (e->params[i].tn) { ms_print_type(b, e->params[i].tn); putc_(b, ' '); }
                    put(b, e->params[i].name, e->params[i].nlen);
                }
                putc_(b, ')');
            }
            puts_(b, " => ");
            if (e->body) block(b, e->body, level); else expr(b, e->a, P_ASSIGN, level);
            break;
        default: puts_(b, "/* ? */"); break;
    }
    if (paren) putc_(b, ')');
}

void ms_print_expr(MsBuf* b, Expr* e) { expr(b, e, P_ASSIGN, 0); }
const char* ms_op_text(int op) { return opstr(op); }

/* ---------- statements ---------- */
static void block(MsBuf* b, Stmt* s, int level) {     /* '{' here, '}' on its own line */
    if (!s->n) { puts_(b, "{ }"); return; }
    puts_(b, "{\n");
    for (int i = 0; i < s->n; i++) { indent(b, level + 1); stmt(b, s->list[i], level + 1); putc_(b, '\n'); }
    indent(b, level); putc_(b, '}');
}

/* body of if / while / for: a block stays on this line, anything else goes on the next line, one level deeper */
static void body(MsBuf* b, Stmt* s, int level) {
    if (s->k == S_BLOCK) { putc_(b, ' '); block(b, s, level); }
    else { putc_(b, '\n'); indent(b, level + 1); stmt(b, s, level + 1); }
}

static void var_decl(MsBuf* b, Stmt* s, int level) {   /* without ';' (also used in for (...)) */
    if (s->tn) ms_print_type(b, s->tn); else puts_(b, "var");
    for (Stmt* v = s; v; v = v->els) {
        puts_(b, v == s ? " " : ", ");
        put(b, v->name, v->nlen);
        if (v->e) { puts_(b, " = "); expr(b, v->e, P_ASSIGN, level); }
    }
}

static void stmt(MsBuf* b, Stmt* s, int level) {
    switch (s->k) {
        case S_BLOCK: block(b, s, level); break;
        case S_VAR: var_decl(b, s, level); putc_(b, ';'); break;
        case S_EXPR: expr(b, s->e, P_ASSIGN, level); putc_(b, ';'); break;
        case S_IF:
            /* Parsed code never has an if without else as the body of an if with else (the else would move),
               so no braces need adding here. */
            puts_(b, "if ("); expr(b, s->e, P_ASSIGN, level); putc_(b, ')');
            body(b, s->body, level);
            if (s->els) {
                if (s->body->k == S_BLOCK) putc_(b, ' '); else { putc_(b, '\n'); indent(b, level); }
                puts_(b, "else");
                if (s->els->k == S_IF) { putc_(b, ' '); stmt(b, s->els, level); }
                else body(b, s->els, level);
            }
            break;
        case S_WHILE: puts_(b, "while ("); expr(b, s->e, P_ASSIGN, level); putc_(b, ')'); body(b, s->body, level); break;
        case S_DO:
            puts_(b, "do"); body(b, s->body, level);
            if (s->body->k == S_BLOCK) putc_(b, ' '); else { putc_(b, '\n'); indent(b, level); }
            puts_(b, "while ("); expr(b, s->e, P_ASSIGN, level); puts_(b, ");");
            break;
        case S_FOR:
            puts_(b, "for (");
            if (s->init) { if (s->init->k == S_VAR) var_decl(b, s->init, level); else expr(b, s->init->e, P_ASSIGN, level); }
            puts_(b, s->e ? "; " : ";");
            if (s->e) expr(b, s->e, P_ASSIGN, level);
            puts_(b, s->step ? "; " : ";");
            if (s->step) expr(b, s->step, P_ASSIGN, level);
            putc_(b, ')');
            body(b, s->body, level);
            break;
        case S_FOREACH:
            puts_(b, "foreach (");
            if (s->tn) ms_print_type(b, s->tn); else puts_(b, "var");
            putc_(b, ' '); put(b, s->name, s->nlen); puts_(b, " in ");
            expr(b, s->e, P_ASSIGN, level); putc_(b, ')');
            body(b, s->body, level);
            break;
        case S_SWITCH:
            puts_(b, "switch ("); expr(b, s->e, P_ASSIGN, level); puts_(b, ") {\n");
            for (int i = 0; i < s->n; i++) {
                Stmt* sec = s->list[i];
                for (int j = 0; j < sec->nlabels; j++) { indent(b, level + 1); puts_(b, "case "); expr(b, sec->labels[j], P_ASSIGN, level + 1); puts_(b, ":\n"); }
                if (sec->isDefault) { indent(b, level + 1); puts_(b, "default:\n"); }
                for (int j = 0; j < sec->n; j++) { indent(b, level + 2); stmt(b, sec->list[j], level + 2); putc_(b, '\n'); }
            }
            indent(b, level); putc_(b, '}');
            break;
        case S_RETURN:
            puts_(b, "return");
            if (s->e) { putc_(b, ' '); expr(b, s->e, P_ASSIGN, level); }
            putc_(b, ';');
            break;
        case S_BREAK: puts_(b, "break;"); break;
        case S_CONTINUE: puts_(b, "continue;"); break;
        case S_YIELD: puts_(b, "yield return "); expr(b, s->e, P_ASSIGN, level); putc_(b, ';'); break;
        case S_YIELDBREAK: puts_(b, "yield break;"); break;
        default: puts_(b, "/* ? */"); break;
    }
}

void ms_print_stmt(MsBuf* b, Stmt* s, int level) { stmt(b, s, level); }

/* ---------- classes ---------- */
static void params(MsBuf* b, Param* p, int n) {
    putc_(b, '(');
    for (int i = 0; i < n; i++) {
        if (i) puts_(b, ", ");
        ms_print_type(b, p[i].tn); putc_(b, ' '); put(b, p[i].name, p[i].nlen);
    }
    putc_(b, ')');
}

static void method(MsBuf* b, ClassDecl* cd, MethodDecl* m) {
    indent(b, 1);
    if (m->isStatic) puts_(b, "static ");
    if (!cd->isInterface) {
        if (m->kind == MK_VIRTUAL) puts_(b, "virtual ");
        else if (m->kind == MK_OVERRIDE) puts_(b, "override ");
        else if (m->kind == MK_ABSTRACT) puts_(b, "abstract ");
    }
    if (!m->isCtor) { ms_print_type(b, m->ret); putc_(b, ' '); }
    put(b, m->name, m->nlen);
    params(b, m->params, m->nparams);
    if (m->baseCall) { puts_(b, " : base("); args(b, m->baseCall->args, m->baseCall->nargs, 1); putc_(b, ')'); }
    if (!m->body) puts_(b, ";");
    else if (m->exprBody) { puts_(b, " => "); expr(b, m->body->list[0]->e, P_ASSIGN, 1); putc_(b, ';'); }
    else { putc_(b, ' '); block(b, m->body, 1); }
    putc_(b, '\n');
}

void ms_print_program(MsBuf* b, ProgramAST* prog) {
    for (int c = 0; c < prog->nclasses; c++) {
        ClassDecl* cd = &prog->classes[c];
        if (c) putc_(b, '\n');
        if (cd->isAbstract) puts_(b, "abstract ");
        puts_(b, cd->isInterface ? "interface " : "class ");
        put(b, cd->name, cd->nlen);
        for (int i = 0; i < cd->nbases; i++) { puts_(b, i ? ", " : " : "); ms_print_type(b, cd->bases[i]); }
        puts_(b, " {\n");
        for (int i = 0; i < cd->nfields; i++) {
            FieldDecl* f = &cd->fields[i];
            indent(b, 1);
            if (f->isStatic) puts_(b, "static ");
            ms_print_type(b, f->tn); putc_(b, ' '); put(b, f->name, f->nlen);
            if (f->init) { puts_(b, " = "); expr(b, f->init, P_ASSIGN, 1); }
            puts_(b, ";\n");
        }
        if (cd->nfields && cd->nmethods) putc_(b, '\n');
        for (int i = 0; i < cd->nmethods; i++) { if (i) putc_(b, '\n'); method(b, cd, &cd->methods[i]); }
        puts_(b, "}\n");
    }
}
