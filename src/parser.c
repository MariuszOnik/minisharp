/* MiniSharp - arena, lexer and recursive-descent parser. */
#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* ================= arena ================= */
void* arena_alloc(Arena* a, size_t n) {
    n = (n + 7) & ~(size_t)7;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = n > 16384 ? n : 16384;
        ArenaBlock* b = (ArenaBlock*)malloc(sizeof(ArenaBlock) + cap);
        if (!b) return NULL;
        b->next = a->head; b->used = 0; b->cap = cap;
        a->head = b;
    }
    void* p = a->head->data + a->head->used;
    a->head->used += n;
    memset(p, 0, n);
    return p;
}

void arena_free(Arena* a) {
    ArenaBlock* b = a->head;
    while (b) { ArenaBlock* n = b->next; free(b); b = n; }
    a->head = NULL;
}

void* arena_grow(Arena* a, void* old, int n, int* cap, size_t sz) {
    if (n < *cap) return old;
    int nc = *cap ? *cap * 2 : 4;
    void* nw = arena_alloc(a, (size_t)nc * sz);
    if (old && n) memcpy(nw, old, (size_t)n * sz);
    *cap = nc;
    return nw;
}

static void fail(ParseCtx* c, int line, const char* fmt, ...) __attribute__((noreturn));
static void fail(ParseCtx* c, int line, const char* fmt, ...) {
    char msg[200];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    snprintf(c->err, c->errlen, "line %d: %s", line, msg);
    longjmp(*c->jb, 1);
}

/* ================= lexer ================= */
static const struct { const char* s; int k; } keywords[] = {
    {"class", TK_CLASS}, {"static", TK_STATIC}, {"void", TK_VOID}, {"int", TK_KINT},
    {"float", TK_KFLOAT}, {"double", TK_KFLOAT}, {"bool", TK_KBOOL}, {"string", TK_KSTRING},
    {"var", TK_VAR}, {"if", TK_IF}, {"else", TK_ELSE}, {"while", TK_WHILE}, {"for", TK_FOR},
    {"do", TK_DO}, {"return", TK_RETURN}, {"new", TK_NEW}, {"true", TK_TRUE}, {"false", TK_FALSE},
    {"null", TK_NULL}, {"break", TK_BREAK}, {"continue", TK_CONTINUE}, {"this", TK_THIS},
    {"public", TK_PUBLIC}, {"private", TK_PRIVATE}, {"using", TK_USING}, {"namespace", TK_NAMESPACE},
    {"const", TK_CONST}, {"readonly", TK_READONLY}, {"protected", TK_PRIVATE}, {"internal", TK_PRIVATE},
    {"char", TK_KCHAR}, {"foreach", TK_FOREACH}, {"in", TK_IN}, {"switch", TK_SWITCH}, {"case", TK_CASE},
    {"default", TK_DEFAULT}, {"out", TK_OUT},
    {"abstract", TK_ABSTRACT}, {"virtual", TK_VIRTUAL}, {"override", TK_OVERRIDE}, {"sealed", TK_SEALED},
    {"interface", TK_INTERFACE}, {"base", TK_BASE}, {"is", TK_IS}, {"as", TK_AS},
};

Token* ms_lex(ParseCtx* c, const char* src, int* ntok) {
    Token* toks = NULL; int n = 0, cap = 0;
    const char* p = src; int line = 1;
    for (;;) {
        for (;;) {
            if (*p == '\n') { line++; p++; }
            else if (*p == ' ' || *p == '\t' || *p == '\r') p++;
            else if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; }
            else if (p[0] == '/' && p[1] == '*') {
                p += 2;
                while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') line++; p++; }
                if (*p) p += 2;
            } else break;
        }
        toks = (Token*)arena_grow(c->A, toks, n, &cap, sizeof(Token));
        Token* t = &toks[n++];
        memset(t, 0, sizeof *t);
        t->start = p; t->line = line;
        char ch = *p;
        if (!ch) { t->kind = TK_EOF; break; }

        if (isalpha((unsigned char)ch) || ch == '_') {
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            t->len = (int)(p - t->start);
            t->kind = TK_IDENT;
            for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++)
                if ((int)strlen(keywords[i].s) == t->len && !memcmp(keywords[i].s, t->start, t->len)) {
                    t->kind = keywords[i].k; break;
                }
            continue;
        }
        if (isdigit((unsigned char)ch) || (ch == '.' && isdigit((unsigned char)p[1]))) {
            if (ch == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                uint32_t v = 0; int digits = 0;
                while (isxdigit((unsigned char)*p)) {
                    v = v * 16 + (uint32_t)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
                    p++; digits++;
                }
                if (!digits) fail(c, line, "bad hex number");
                t->kind = TK_INT; t->ival = (int32_t)v;
            } else {
                const char* s = p; int isf = 0;
                while (isdigit((unsigned char)*p)) p++;
                if (*p == '.' && isdigit((unsigned char)p[1])) { isf = 1; p++; while (isdigit((unsigned char)*p)) p++; }
                if (*p == 'e' || *p == 'E') {
                    const char* q = p + 1;
                    if (*q == '+' || *q == '-') q++;
                    if (isdigit((unsigned char)*q)) { isf = 1; p = q; while (isdigit((unsigned char)*p)) p++; }
                }
                char buf[64]; int L = (int)(p - s); if (L > 63) L = 63;
                memcpy(buf, s, L); buf[L] = 0;
                if (*p == 'f' || *p == 'F' || *p == 'd' || *p == 'D') { isf = 1; p++; }
                if (isf) { t->kind = TK_FLOAT; t->fval = strtof(buf, NULL); }
                else {
                    unsigned long long v = strtoull(buf, NULL, 10);
                    if (v > 2147483648ULL) fail(c, line, "integer literal too big");
                    t->kind = TK_INT; t->ival = (int32_t)(uint32_t)v;
                }
            }
            t->len = (int)(p - t->start);
            continue;
        }
        if (ch == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\n') fail(c, line, "unterminated string");
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            if (!*p) fail(c, line, "unterminated string");
            p++;
            t->kind = TK_STRING; t->len = (int)(p - t->start);
            continue;
        }
        if (ch == '\'') {
            p++;
            int v;
            if (*p == '\\') {
                p++;
                switch (*p) { case 'n': v = '\n'; break; case 't': v = '\t'; break; case 'r': v = '\r'; break; case '0': v = 0; break; default: v = (unsigned char)*p; }
            } else v = (unsigned char)*p;
            p++;
            if (*p != '\'') fail(c, line, "bad character literal");
            p++;
            t->kind = TK_CHAR; t->ival = v; t->len = (int)(p - t->start);
            continue;
        }
        #define TWO(a, b, k2, k1) case a: if (p[1] == b) { t->kind = k2; p += 2; } else { t->kind = k1; p++; } break;
        switch (ch) {
            case '(': t->kind = TK_LPAREN; p++; break;
            case ')': t->kind = TK_RPAREN; p++; break;
            case '{': t->kind = TK_LBRACE; p++; break;
            case '}': t->kind = TK_RBRACE; p++; break;
            case '[': t->kind = TK_LBRACK; p++; break;
            case ']': t->kind = TK_RBRACK; p++; break;
            case ';': t->kind = TK_SEMI; p++; break;
            case ',': t->kind = TK_COMMA; p++; break;
            case '.': t->kind = TK_DOT; p++; break;
            case '?':
                if (p[1] == '?') { t->kind = TK_QQ; p += 2; }
                else if (p[1] == '.' && !isdigit((unsigned char)p[2])) { t->kind = TK_QDOT; p += 2; }   /* a ?.5f : 1 stays a '?' */
                else { t->kind = TK_QUESTION; p++; }
                break;
            case ':': t->kind = TK_COLON; p++; break;
            case '+':
                if (p[1] == '+') { t->kind = TK_INC; p += 2; }
                else if (p[1] == '=') { t->kind = TK_PLUSEQ; p += 2; }
                else { t->kind = TK_PLUS; p++; }
                break;
            case '-':
                if (p[1] == '-') { t->kind = TK_DEC; p += 2; }
                else if (p[1] == '=') { t->kind = TK_MINUSEQ; p += 2; }
                else { t->kind = TK_MINUS; p++; }
                break;
            TWO('*', '=', TK_STAREQ, TK_STAR)
            TWO('/', '=', TK_SLASHEQ, TK_SLASH)
            TWO('%', '=', TK_PERCENTEQ, TK_PERCENT)
            TWO('!', '=', TK_NE, TK_BANG)
            case '=':
                if (p[1] == '=') { t->kind = TK_EQ; p += 2; }
                else if (p[1] == '>') { t->kind = TK_ARROW; p += 2; }
                else { t->kind = TK_ASSIGN; p++; }
                break;
            TWO('>', '=', TK_GE, TK_GT)         /* '>>' stays two tokens so List<List<int>> works */
            TWO('^', '=', TK_CARETEQ, TK_CARET)
            case '<':
                if (p[1] == '<') { if (p[2] == '=') { t->kind = TK_SHLEQ; p += 3; } else { t->kind = TK_SHL; p += 2; } }
                else if (p[1] == '=') { t->kind = TK_LE; p += 2; }
                else { t->kind = TK_LT; p++; }
                break;
            case '&':
                if (p[1] == '&') { t->kind = TK_AND; p += 2; } else if (p[1] == '=') { t->kind = TK_AMPEQ; p += 2; } else { t->kind = TK_AMP; p++; }
                break;
            case '|':
                if (p[1] == '|') { t->kind = TK_OR; p += 2; } else if (p[1] == '=') { t->kind = TK_PIPEEQ; p += 2; } else { t->kind = TK_PIPE; p++; }
                break;
            case '~': t->kind = TK_TILDE; p++; break;
            default: fail(c, line, "unexpected character '%c'", ch);
        }
        #undef TWO
        t->len = (int)(p - t->start);
    }
    *ntok = n;
    return toks;
}

/* ================= parser ================= */
typedef struct { ParseCtx* c; Token* t; int pos, n; int sawYield, inInterface, nlambdas; } P;

static Token* peek(P* p) { return &p->t[p->pos]; }
static Token* peekn(P* p, int k) { int i = p->pos + k; return &p->t[i < p->n ? i : p->n - 1]; }
static Token* next(P* p) { Token* t = &p->t[p->pos]; if (t->kind != TK_EOF) p->pos++; return t; }
static int at(P* p, int k) { return peek(p)->kind == k; }
static int accept(P* p, int k) { if (at(p, k)) { p->pos++; return 1; } return 0; }

static const char* tokname(int k) {
    switch (k) {
        case TK_EOF: return "end of file"; case TK_IDENT: return "name";
        case TK_LPAREN: return "'('"; case TK_RPAREN: return "')'";
        case TK_LBRACE: return "'{'"; case TK_RBRACE: return "'}'";
        case TK_LBRACK: return "'['"; case TK_RBRACK: return "']'";
        case TK_SEMI: return "';'"; case TK_COMMA: return "','";
        case TK_COLON: return "':'"; case TK_ASSIGN: return "'='";
        case TK_GT: return "'>'"; case TK_IN: return "'in'";
        default: return "token";
    }
}

static Token* expect(P* p, int k) {
    if (!at(p, k)) {
        Token* t = peek(p);
        if (t->kind == TK_EOF) fail(p->c, t->line, "expected %s but reached end of file", tokname(k));
        fail(p->c, t->line, "expected %s, found '%.*s'", tokname(k), t->len, t->start);
    }
    return next(p);
}

static void* nw(P* p, size_t sz) { return arena_alloc(p->c->A, sz); }
static Expr* mkexpr(P* p, int k, int line) { Expr* e = (Expr*)nw(p, sizeof(Expr)); e->k = k; e->line = line; return e; }
static Stmt* mkstmt(P* p, int k, int line) { Stmt* s = (Stmt*)nw(p, sizeof(Stmt)); s->k = k; s->line = line; return s; }

/* ---------- source spans: every node remembers the text [s0, s1) it was parsed from ---------- */
static const char* prev_end(P* p) { Token* t = &p->t[p->pos > 0 ? p->pos - 1 : 0]; return t->start + t->len; }
static Expr* espan(P* p, Expr* e, int tok) { e->s0 = p->t[tok].start; e->s1 = prev_end(p); return e; }
static Stmt* sspan(P* p, Stmt* s, int tok) { s->s0 = p->t[tok].start; s->s1 = prev_end(p); return s; }
/* A wrapper gives the node it returns the span of all tokens its parse function read. The outermost wrapper wins,
   so '(a + b)' keeps its parentheses; that text parses to the same tree, which is all the span promises. */
static Expr* parse_expr_(P* p);
static Expr* parse_ternary_(P* p);
static Expr* parse_coalesce_(P* p);
static Expr* parse_binary_(P* p, int minprec);
static Expr* parse_unary_(P* p);
static Expr* parse_arg_(P* p);
static Stmt* parse_stmt_(P* p);
static Stmt* parse_block_(P* p);
static Stmt* parse_simple_(P* p);
static Expr* parse_expr(P* p) { int st = p->pos; return espan(p, parse_expr_(p), st); }
static Expr* parse_ternary(P* p) { int st = p->pos; return espan(p, parse_ternary_(p), st); }
static Expr* parse_coalesce(P* p) { int st = p->pos; return espan(p, parse_coalesce_(p), st); }
static Expr* parse_binary(P* p, int minprec) { int st = p->pos; return espan(p, parse_binary_(p, minprec), st); }
static Expr* parse_unary(P* p) { int st = p->pos; return espan(p, parse_unary_(p), st); }
static Expr* parse_arg(P* p) { int st = p->pos; return espan(p, parse_arg_(p), st); }
static Stmt* parse_stmt(P* p) { int st = p->pos; return sspan(p, parse_stmt_(p), st); }
static Stmt* parse_block(P* p) { int st = p->pos; return sspan(p, parse_block_(p), st); }
static Stmt* parse_simple(P* p) { int st = p->pos; return sspan(p, parse_simple_(p), st); }

static int is_type_kw(int k) { return k == TK_KINT || k == TK_KFLOAT || k == TK_KBOOL || k == TK_KSTRING || k == TK_KCHAR || k == TK_VOID; }

static TypeNode* parse_type(P* p);

/* <A, B, ...> after a generic type's name (the '<' is already read) */
static void parse_type_args(P* p, TypeNode* tn) {
    int cap = 0;
    do {
        tn->args = (TypeNode**)arena_grow(p->c->A, tn->args, tn->nargs, &cap, sizeof(TypeNode*));
        tn->args[tn->nargs++] = parse_type(p);
    } while (accept(p, TK_COMMA));
    expect(p, TK_GT);
    tn->arg = tn->args[0];
    tn->arg2 = tn->nargs > 1 ? tn->args[1] : NULL;
}

/* after a type name: <T> (generic argument) and [] pairs */
static void parse_type_suffix(P* p, TypeNode* tn) {
    if (tn->base == TK_IDENT && accept(p, TK_LT)) parse_type_args(p, tn);
    while (at(p, TK_LBRACK) && peekn(p, 1)->kind == TK_RBRACK) { p->pos += 2; tn->dims++; }
}

static TypeNode* parse_type(P* p) {
    Token* t = next(p);
    if (!is_type_kw(t->kind) && t->kind != TK_IDENT)
        fail(p->c, t->line, "expected a type, found '%.*s'", t->len, t->start);
    TypeNode* tn = (TypeNode*)nw(p, sizeof(TypeNode));
    tn->base = t->kind; tn->name = t->start; tn->nlen = t->len; tn->line = t->line;
    parse_type_suffix(p, tn);
    return tn;
}

/* index just after a type starting at token i, or -1 if no type starts there */
static int skip_type(P* p, int i) {
    int k = p->t[i].kind;
    if (!is_type_kw(k) && k != TK_IDENT) return -1;
    i++;
    if (k == TK_IDENT && p->t[i].kind == TK_LT) {
        i = skip_type(p, i + 1);
        while (i >= 0 && p->t[i].kind == TK_COMMA) i = skip_type(p, i + 1);
        if (i < 0 || p->t[i].kind != TK_GT) return -1;
        i++;
    }
    while (p->t[i].kind == TK_LBRACK && p->t[i + 1].kind == TK_RBRACK) i += 2;
    return i;
}

/* Does a type followed by an identifier start here? (used to detect declarations) */
static int looks_like_decl(P* p) {
    int i = p->pos;
    if (p->t[i].kind == TK_VAR) return p->t[i + 1].kind == TK_IDENT;
    i = skip_type(p, i);
    return i >= 0 && p->t[i].kind == TK_IDENT;
}

/* Current operator; '>' '>' written together is a shift, '>' '>=' together is '>>='. *ntok = tokens it uses */
static int optok(P* p, int* ntok) {
    Token* t = peek(p);
    *ntok = 1;
    if (t->kind == TK_GT) {
        Token* u = peekn(p, 1);
        if (u->start == t->start + 1 && (u->kind == TK_GT || u->kind == TK_GE)) { *ntok = 2; return u->kind == TK_GT ? TK_SHR : TK_SHREQ; }
    }
    return t->kind;
}

static Expr* parse_expr(P* p);
static int at_lambda(P* p);
static Expr* parse_lambda(P* p);

static Expr* parse_unary(P* p);
static int looks_like_decl(P* p);
static TypeNode* parse_type(P* p);

static Expr* parse_arg_(P* p) {
    Token* t = peek(p);
    if (!accept(p, TK_OUT)) return parse_expr(p);
    Expr* e = mkexpr(p, E_OUT, t->line);
    if (accept(p, TK_VAR) || looks_like_decl(p)) {
        if (p->t[p->pos - 1].kind != TK_VAR) e->tn = parse_type(p);
        Token* id = expect(p, TK_IDENT);
        e->prefix = 1; e->name = id->start; e->nlen = id->len;
    } else e->a = parse_unary(p);
    return e;
}

static Expr** parse_args(P* p, int* nargs) {
    Expr** args = NULL; int n = 0, cap = 0;
    expect(p, TK_LPAREN);
    if (!at(p, TK_RPAREN)) {
        do {
            args = (Expr**)arena_grow(p->c->A, args, n, &cap, sizeof(Expr*));
            args[n++] = parse_arg(p);
        } while (accept(p, TK_COMMA));
    }
    expect(p, TK_RPAREN);
    *nargs = n;
    return args;
}

static char* unescape(P* p, Token* t, int* outlen) {
    char* s = (char*)nw(p, (size_t)t->len + 1);
    int n = 0;
    for (int i = 1; i < t->len - 1; i++) {
        char ch = t->start[i];
        if (ch == '\\') {
            ch = t->start[++i];
            switch (ch) {
                case 'n': ch = '\n'; break; case 't': ch = '\t'; break; case 'r': ch = '\r'; break;
                case '0': ch = 0; break; case '\\': ch = '\\'; break; case '"': ch = '"'; break; case '\'': ch = '\''; break;
                default: fail(p->c, t->line, "unknown escape '\\%c'", ch);
            }
        }
        s[n++] = ch;
    }
    s[n] = 0;
    *outlen = n;
    return s;
}

static Expr* parse_new(P* p, int line) {
    Token* t = next(p);
    if (!is_type_kw(t->kind) && t->kind != TK_IDENT) fail(p->c, t->line, "expected a type after 'new'");
    if (t->kind == TK_VOID) fail(p->c, t->line, "cannot create 'void'");
    TypeNode* tn = (TypeNode*)nw(p, sizeof(TypeNode));
    tn->base = t->kind; tn->name = t->start; tn->nlen = t->len; tn->line = t->line;
    if (t->kind == TK_IDENT && at(p, TK_LT)) {
        /* new List<T>(), new List<T>(capacity), new List<T> { a, b } */
        next(p);
        parse_type_args(p, tn);
        if (at(p, TK_LBRACK)) goto array;       /* new List<int>[4], new Func<int, int>[] { ... } */
        Expr* e = mkexpr(p, E_NEWLIST, line);
        e->tn = tn;
        if (at(p, TK_LPAREN)) {
            int n; Expr** a = parse_args(p, &n);
            if (n > 1) fail(p->c, line, "'new %.*s<...>' takes at most one argument (the capacity)", t->len, t->start);
            if (n == 1) e->a = a[0];
        } else if (!at(p, TK_LBRACE)) fail(p->c, line, "expected '(' or '{' after 'new %.*s<...>'", t->len, t->start);
        if (accept(p, TK_LBRACE)) {
            Expr** items = NULL; int n = 0, cap = 0;
            while (!at(p, TK_RBRACE)) {
                items = (Expr**)arena_grow(p->c->A, items, n, &cap, sizeof(Expr*));
                Token* it = peek(p);
                int ist = p->pos;
                if (accept(p, TK_LBRACE)) {                 /* { key, value } */
                    Expr* pr = mkexpr(p, E_PAIR, it->line);
                    pr->a = parse_expr(p); expect(p, TK_COMMA); pr->b = parse_expr(p);
                    expect(p, TK_RBRACE);
                    items[n++] = espan(p, pr, ist);
                } else if (accept(p, TK_LBRACK)) {          /* [key] = value */
                    Expr* pr = mkexpr(p, E_PAIR, it->line);
                    pr->a = parse_expr(p); expect(p, TK_RBRACK); expect(p, TK_ASSIGN); pr->b = parse_expr(p);
                    pr->prefix = 1;
                    items[n++] = espan(p, pr, ist);
                } else items[n++] = parse_expr(p);
                if (!accept(p, TK_COMMA)) break;
            }
            expect(p, TK_RBRACE);
            e->args = items; e->nargs = n;
        }
        return e;
    }
    if (at(p, TK_LPAREN)) {
        if (t->kind != TK_IDENT) fail(p->c, line, "cannot use 'new' with a built-in type like that");
        Expr* e = mkexpr(p, E_NEW, line);
        e->tn = tn;
        e->args = parse_args(p, &e->nargs);
        return e;
    }
    if (!at(p, TK_LBRACK)) fail(p->c, line, "expected '(' or '[' after 'new %.*s'", t->len, t->start);
array:;
    Expr* e = mkexpr(p, E_NEWARR, line);
    e->tn = tn;                        /* tn->dims = extra dimensions of the element type */
    next(p);
    if (accept(p, TK_RBRACK)) {
        while (at(p, TK_LBRACK) && peekn(p, 1)->kind == TK_RBRACK) { p->pos += 2; tn->dims++; }
        /* initializer list: new int[] { 1, 2, 3 } */
        e->prefix = 1;
        expect(p, TK_LBRACE);
        Expr** items = NULL; int n = 0, cap = 0;
        if (!at(p, TK_RBRACE)) {
            do {
                if (at(p, TK_RBRACE)) break;
                items = (Expr**)arena_grow(p->c->A, items, n, &cap, sizeof(Expr*));
                items[n++] = parse_expr(p);
            } while (accept(p, TK_COMMA));
        }
        expect(p, TK_RBRACE);
        e->args = items; e->nargs = n;
        return e;
    }
    e->a = parse_expr(p);
    expect(p, TK_RBRACK);
    while (at(p, TK_LBRACK) && peekn(p, 1)->kind == TK_RBRACK) { p->pos += 2; tn->dims++; }
    return e;
}

static Expr* parse_primary_(P* p);
static Expr* parse_primary(P* p) {
    int st = p->pos, paren = at(p, TK_LPAREN);
    Expr* e = parse_primary_(p);
    return paren ? e : espan(p, e, st);
}
static Expr* parse_primary_(P* p) {
    Token* t = next(p);
    Expr* e;
    switch (t->kind) {
        case TK_INT: e = mkexpr(p, E_INT, t->line); e->ival = t->ival; return e;
        case TK_FLOAT: e = mkexpr(p, E_FLOAT, t->line); e->fval = t->fval; return e;
        case TK_CHAR: e = mkexpr(p, E_CHAR, t->line); e->ival = t->ival; return e;
        case TK_TRUE: e = mkexpr(p, E_BOOL, t->line); e->ival = 1; return e;
        case TK_FALSE: e = mkexpr(p, E_BOOL, t->line); e->ival = 0; return e;
        case TK_NULL: return mkexpr(p, E_NULL, t->line);
        case TK_THIS: return mkexpr(p, E_THIS, t->line);
        case TK_BASE:
            if (!at(p, TK_DOT)) fail(p->c, t->line, "'base' must be followed by '.', e.g. base.Update()");
            return mkexpr(p, E_BASE, t->line);
        case TK_STRING: {
            e = mkexpr(p, E_STRING, t->line);
            int L; e->name = unescape(p, t, &L); e->nlen = L;
            return e;
        }
        case TK_IDENT: e = mkexpr(p, E_NAME, t->line); e->name = t->start; e->nlen = t->len; return e;
        case TK_NEW: return parse_new(p, t->line);
        case TK_LPAREN: e = parse_expr(p); expect(p, TK_RPAREN); return e;
        case TK_KINT: case TK_KFLOAT: case TK_KBOOL: case TK_KSTRING: case TK_KCHAR:
            /* int.Parse(s), char.IsDigit(c), string.IsNullOrEmpty(s): the type works like a built-in class */
            if (at(p, TK_DOT)) {
                e = mkexpr(p, E_NAME, t->line);
                if (t->len == 6 && !memcmp(t->start, "double", 6)) { e->name = "float"; e->nlen = 5; }
                else { e->name = t->start; e->nlen = t->len; }
                return e;
            }
            fail(p->c, t->line, "unexpected type name '%.*s' here", t->len, t->start);
        default:
            if (t->kind == TK_EOF) fail(p->c, t->line, "unexpected end of file in expression");
            fail(p->c, t->line, "unexpected '%.*s' in expression", t->len, t->start);
    }
    return NULL;
}

static Expr* parse_postfix(P* p) {
    int st = p->pos;
    Expr* e = parse_primary(p);
    for (;;) {
        Token* t = peek(p);
        if (t->kind == TK_DOT || t->kind == TK_QDOT) {
            next(p);
            Token* id = expect(p, TK_IDENT);
            Expr* m = mkexpr(p, E_MEMBER, id->line);
            m->a = e; m->name = id->start; m->nlen = id->len;
            m->prefix = t->kind == TK_QDOT;
            e = m;
        } else if (t->kind == TK_LPAREN) {
            Expr* c = mkexpr(p, E_CALL, t->line);
            c->a = e;
            c->args = parse_args(p, &c->nargs);
            e = c;
        } else if (t->kind == TK_LBRACK) {
            next(p);
            Expr* ix = mkexpr(p, E_INDEX, t->line);
            ix->a = e; ix->b = parse_expr(p);
            expect(p, TK_RBRACK);
            e = ix;
        } else if (t->kind == TK_INC || t->kind == TK_DEC) {
            next(p);
            Expr* u = mkexpr(p, E_INCDEC, t->line);
            u->op = t->kind; u->a = e; u->prefix = 0;
            e = u;
        } else break;
        espan(p, e, st);
    }
    return e;
}

static Expr* parse_unary_(P* p) {
    Token* t = peek(p);
    if (t->kind == TK_MINUS || t->kind == TK_BANG || t->kind == TK_PLUS || t->kind == TK_TILDE) {
        next(p);
        Expr* operand = parse_unary(p);
        if (t->kind == TK_PLUS) return operand;
        if (t->kind == TK_MINUS && operand->k == E_INT) { operand->ival = (int32_t)(0u - (uint32_t)operand->ival); return operand; }
        if (t->kind == TK_MINUS && operand->k == E_FLOAT) { operand->fval = -operand->fval; return operand; }
        Expr* e = mkexpr(p, E_UNARY, t->line);
        e->op = t->kind; e->a = operand;
        return e;
    }
    if (t->kind == TK_INC || t->kind == TK_DEC) {
        next(p);
        Expr* e = mkexpr(p, E_INCDEC, t->line);
        e->op = t->kind; e->a = parse_unary(p); e->prefix = 1;
        return e;
    }
    /* cast: (int)x, (float)x */
    if (t->kind == TK_LPAREN) {
        int k = peekn(p, 1)->kind;
        if ((k == TK_KINT || k == TK_KFLOAT || k == TK_KCHAR) && peekn(p, 2)->kind == TK_RPAREN) {
            p->pos += 3;
            Expr* e = mkexpr(p, E_CAST, t->line);
            e->op = k; e->a = parse_unary(p);
            return e;
        }
        /* (Enemy)x: a name in parentheses followed by something that starts an operand, as in C# */
        if (k == TK_IDENT && peekn(p, 2)->kind == TK_RPAREN) {
            int n = peekn(p, 3)->kind;
            if (n == TK_IDENT || n == TK_THIS || n == TK_BASE || n == TK_NEW || n == TK_LPAREN || n == TK_INT || n == TK_FLOAT ||
                n == TK_STRING || n == TK_CHAR || n == TK_TRUE || n == TK_FALSE || n == TK_NULL || n == TK_BANG || n == TK_TILDE) {
                p->pos++;
                Expr* e = mkexpr(p, E_CAST, t->line);
                e->tn = parse_type(p);
                expect(p, TK_RPAREN);
                e->a = parse_unary(p);
                return e;
            }
        }
    }
    return parse_postfix(p);
}

static int binprec(int k) {
    switch (k) {
        case TK_OR: return 1;
        case TK_AND: return 2;
        case TK_PIPE: return 3;
        case TK_CARET: return 4;
        case TK_AMP: return 5;
        case TK_EQ: case TK_NE: return 6;
        case TK_LT: case TK_LE: case TK_GT: case TK_GE: case TK_IS: case TK_AS: return 7;
        case TK_SHL: case TK_SHR: return 8;
        case TK_PLUS: case TK_MINUS: return 9;
        case TK_STAR: case TK_SLASH: case TK_PERCENT: return 10;
        default: return 0;
    }
}

static Expr* parse_binary_(P* p, int minprec) {
    int st = p->pos;
    Expr* left = parse_unary(p);
    for (;;) {
        Token* t = peek(p);
        int ntok, k = optok(p, &ntok);
        int prec = binprec(k);
        if (!prec || prec < minprec) break;
        p->pos += ntok;
        if (k == TK_IS || k == TK_AS) {          /* the right side is a type: a is Enemy, a is Enemy e, a is null, a is not null */
            Expr* e = mkexpr(p, k == TK_IS ? E_IS : E_AS, t->line);
            e->a = left;
            if (k == TK_IS && at(p, TK_IDENT) && peek(p)->len == 3 && !memcmp(peek(p)->start, "not", 3)) { next(p); e->op = 1; }
            if (k == TK_IS && accept(p, TK_NULL)) { left = espan(p, e, st); continue; }
            e->tn = parse_type(p);
            if (k == TK_IS && !e->op && at(p, TK_IDENT)) { Token* id = next(p); e->name = id->start; e->nlen = id->len; }
            left = espan(p, e, st);
            continue;
        }
        Expr* right = parse_binary(p, prec + 1);
        Expr* e = mkexpr(p, (k == TK_AND || k == TK_OR) ? E_LOGIC : E_BINARY, t->line);
        e->op = k; e->a = left; e->b = right;
        left = espan(p, e, st);
    }
    return left;
}

/* a ?? b (right to left, between || and ?:) */
static Expr* parse_coalesce_(P* p) {
    Expr* left = parse_binary(p, 1);
    if (!at(p, TK_QQ)) return left;
    Token* t = next(p);
    Expr* e = mkexpr(p, E_COALESCE, t->line);
    e->a = left;
    e->b = at_lambda(p) ? parse_lambda(p) : parse_coalesce(p);
    return e;
}

static Expr* parse_ternary_(P* p) {
    Expr* cond = parse_coalesce(p);
    if (at(p, TK_QUESTION)) {
        Token* t = next(p);
        Expr* e = mkexpr(p, E_COND, t->line);
        e->a = cond;
        e->b = parse_expr(p);
        expect(p, TK_COLON);
        e->c = parse_ternary(p);
        return e;
    }
    return cond;
}

static Stmt* parse_block(P* p);

/* x => ..., (x, y) => ..., (int x) => ..., () => ...; the body is an expression or a block */
static Expr* parse_lambda_(P* p);
static Expr* parse_lambda(P* p) { int st = p->pos; return espan(p, parse_lambda_(p), st); }
static Expr* parse_lambda_(P* p) {
    Token* t = peek(p);
    Expr* e = mkexpr(p, E_LAMBDA, t->line);
    int cap = 0;
    if (accept(p, TK_IDENT)) {
        e->params = (Param*)arena_grow(p->c->A, e->params, 0, &cap, sizeof(Param));
        e->params[0].name = t->start; e->params[0].nlen = t->len;
        e->nparams = 1;
    } else {
        expect(p, TK_LPAREN);
        if (!at(p, TK_RPAREN)) {
            do {
                e->params = (Param*)arena_grow(p->c->A, e->params, e->nparams, &cap, sizeof(Param));
                Param* pa = &e->params[e->nparams++];
                memset(pa, 0, sizeof *pa);
                if (looks_like_decl(p)) pa->tn = parse_type(p);
                Token* id = expect(p, TK_IDENT);
                pa->name = id->start; pa->nlen = id->len;
            } while (accept(p, TK_COMMA));
        }
        expect(p, TK_RPAREN);
    }
    expect(p, TK_ARROW);
    p->nlambdas++;
    if (at(p, TK_LBRACE)) {
        int saw = p->sawYield;
        p->sawYield = 0;
        e->body = parse_block(p);
        if (p->sawYield) fail(p->c, t->line, "a lambda cannot use 'yield'");
        p->sawYield = saw;
    } else e->a = parse_expr(p);
    return e;
}

/* does a lambda start here? */
static int at_lambda(P* p) {
    if (at(p, TK_IDENT)) return peekn(p, 1)->kind == TK_ARROW;
    if (!at(p, TK_LPAREN)) return 0;
    int d = 0;
    for (int i = p->pos; i < p->n; i++) {
        int k = p->t[i].kind;
        if (k == TK_LPAREN) d++;
        else if (k == TK_RPAREN && --d == 0) return i + 1 < p->n && p->t[i + 1].kind == TK_ARROW;
        else if (k == TK_SEMI || k == TK_LBRACE || k == TK_RBRACE || k == TK_EOF) return 0;
    }
    return 0;
}

static Expr* parse_expr_(P* p) {
    if (at_lambda(p)) return parse_lambda(p);
    Expr* left = parse_ternary(p);
    int ntok, k = optok(p, &ntok);
    if (k == TK_ASSIGN || k == TK_PLUSEQ || k == TK_MINUSEQ || k == TK_STAREQ || k == TK_SLASHEQ || k == TK_PERCENTEQ ||
        k == TK_AMPEQ || k == TK_PIPEEQ || k == TK_CARETEQ || k == TK_SHLEQ || k == TK_SHREQ) {
        Token* t = peek(p);
        p->pos += ntok;
        Expr* e = mkexpr(p, E_ASSIGN, t->line);
        e->op = k; e->a = left; e->b = parse_expr(p);
        return e;
    }
    return left;
}

/* ---------- statements ---------- */
static Stmt* parse_stmt(P* p);

static Stmt* parse_block_(P* p) {
    Token* t = expect(p, TK_LBRACE);
    Stmt* b = mkstmt(p, S_BLOCK, t->line);
    int cap = 0;
    while (!at(p, TK_RBRACE)) {
        if (at(p, TK_EOF)) fail(p->c, t->line, "block opened here is never closed with '}'");
        b->list = (Stmt**)arena_grow(p->c->A, b->list, b->n, &cap, sizeof(Stmt*));
        b->list[b->n++] = parse_stmt(p);
    }
    next(p);
    return b;
}

static Stmt* parse_var_decl(P* p) {
    Token* t = peek(p);
    Stmt* s = mkstmt(p, S_VAR, t->line);
    if (accept(p, TK_VAR)) s->tn = NULL;
    else s->tn = parse_type(p);
    if (s->tn && s->tn->base == TK_VOID && s->tn->dims == 0) fail(p->c, t->line, "a variable cannot be 'void'");
    Token* id = expect(p, TK_IDENT);
    s->name = id->start; s->nlen = id->len;
    if (accept(p, TK_ASSIGN)) s->e = parse_expr(p);
    else if (!s->tn) fail(p->c, t->line, "'var' needs an initial value");
    /* int a = 1, b, c = 3;  -> chained through 'els' */
    Stmt* last = s;
    while (accept(p, TK_COMMA)) {
        if (!s->tn) fail(p->c, t->line, "'var' cannot declare several variables at once");
        Stmt* n = mkstmt(p, S_VAR, peek(p)->line);
        n->tn = s->tn;
        int nst = p->pos;
        Token* nid = expect(p, TK_IDENT);
        n->name = nid->start; n->nlen = nid->len;
        if (accept(p, TK_ASSIGN)) n->e = parse_expr(p);
        sspan(p, n, nst);
        last->els = n; last = n;
    }
    return s;
}

static Stmt* parse_simple_(P* p) {
    /* declaration or expression, without the trailing ';' */
    if (looks_like_decl(p)) return parse_var_decl(p);
    Token* t = peek(p);
    Stmt* s = mkstmt(p, S_EXPR, t->line);
    s->e = parse_expr(p);
    return s;
}

static Stmt* parse_stmt_(P* p) {
    Token* t = peek(p);
    Stmt* s;
    switch (t->kind) {
        case TK_LBRACE: return parse_block(p);
        case TK_CONST:          /* local const: treated as an ordinary variable */
            next(p);
            if (!looks_like_decl(p)) fail(p->c, t->line, "expected a declaration after 'const'");
            s = parse_var_decl(p);
            expect(p, TK_SEMI);
            return s;
        case TK_SEMI: next(p); return mkstmt(p, S_BLOCK, t->line);
        case TK_IF:
            next(p);
            s = mkstmt(p, S_IF, t->line);
            expect(p, TK_LPAREN); s->e = parse_expr(p); expect(p, TK_RPAREN);
            s->body = parse_stmt(p);
            if (accept(p, TK_ELSE)) s->els = parse_stmt(p);
            return s;
        case TK_WHILE:
            next(p);
            s = mkstmt(p, S_WHILE, t->line);
            expect(p, TK_LPAREN); s->e = parse_expr(p); expect(p, TK_RPAREN);
            s->body = parse_stmt(p);
            return s;
        case TK_DO:
            next(p);
            s = mkstmt(p, S_DO, t->line);
            s->body = parse_stmt(p);
            expect(p, TK_WHILE);
            expect(p, TK_LPAREN); s->e = parse_expr(p); expect(p, TK_RPAREN);
            expect(p, TK_SEMI);
            return s;
        case TK_FOR:
            next(p);
            s = mkstmt(p, S_FOR, t->line);
            expect(p, TK_LPAREN);
            if (!at(p, TK_SEMI)) s->init = parse_simple(p);
            expect(p, TK_SEMI);
            if (!at(p, TK_SEMI)) s->e = parse_expr(p);
            expect(p, TK_SEMI);
            if (!at(p, TK_RPAREN)) s->step = parse_expr(p);
            expect(p, TK_RPAREN);
            s->body = parse_stmt(p);
            return s;
        case TK_FOREACH: {
            next(p);
            s = mkstmt(p, S_FOREACH, t->line);
            expect(p, TK_LPAREN);
            if (!accept(p, TK_VAR)) s->tn = parse_type(p);
            Token* id = expect(p, TK_IDENT);
            s->name = id->start; s->nlen = id->len;
            expect(p, TK_IN);
            s->e = parse_expr(p);
            expect(p, TK_RPAREN);
            s->body = parse_stmt(p);
            return s;
        }
        case TK_SWITCH: {
            next(p);
            s = mkstmt(p, S_SWITCH, t->line);
            expect(p, TK_LPAREN); s->e = parse_expr(p); expect(p, TK_RPAREN);
            expect(p, TK_LBRACE);
            int scap = 0;
            while (!at(p, TK_RBRACE)) {
                if (!at(p, TK_CASE) && !at(p, TK_DEFAULT)) {
                    Token* u = peek(p);
                    if (u->kind == TK_EOF) fail(p->c, t->line, "'switch' opened here is never closed with '}'");
                    fail(p->c, u->line, "expected 'case' or 'default', found '%.*s'", u->len, u->start);
                }
                int secst = p->pos;
                Stmt* sec = mkstmt(p, S_CASE, peek(p)->line);
                int lcap = 0;
                /* stacked labels share one section: case 1: case 2: ... */
                while (at(p, TK_CASE) || at(p, TK_DEFAULT)) {
                    if (accept(p, TK_DEFAULT)) sec->isDefault = 1;
                    else {
                        next(p);
                        sec->labels = (Expr**)arena_grow(p->c->A, sec->labels, sec->nlabels, &lcap, sizeof(Expr*));
                        sec->labels[sec->nlabels++] = parse_expr(p);
                    }
                    expect(p, TK_COLON);
                }
                int bcap = 0;
                while (!at(p, TK_CASE) && !at(p, TK_DEFAULT) && !at(p, TK_RBRACE) && !at(p, TK_EOF)) {
                    sec->list = (Stmt**)arena_grow(p->c->A, sec->list, sec->n, &bcap, sizeof(Stmt*));
                    sec->list[sec->n++] = parse_stmt(p);
                }
                sspan(p, sec, secst);
                s->list = (Stmt**)arena_grow(p->c->A, s->list, s->n, &scap, sizeof(Stmt*));
                s->list[s->n++] = sec;
            }
            next(p);
            return s;
        }
        case TK_RETURN:
            next(p);
            s = mkstmt(p, S_RETURN, t->line);
            if (!at(p, TK_SEMI)) s->e = parse_expr(p);
            expect(p, TK_SEMI);
            return s;
        case TK_BREAK: next(p); expect(p, TK_SEMI); return mkstmt(p, S_BREAK, t->line);
        case TK_CONTINUE: next(p); expect(p, TK_SEMI); return mkstmt(p, S_CONTINUE, t->line);
        default:
            /* 'yield' is a contextual keyword, as in C#: only 'yield return' / 'yield break' */
            if (t->kind == TK_IDENT && t->len == 5 && !memcmp(t->start, "yield", 5) &&
                (peekn(p, 1)->kind == TK_RETURN || peekn(p, 1)->kind == TK_BREAK)) {
                next(p);
                p->sawYield = 1;
                if (accept(p, TK_BREAK)) { expect(p, TK_SEMI); return mkstmt(p, S_YIELDBREAK, t->line); }
                next(p);
                s = mkstmt(p, S_YIELD, t->line);
                s->e = parse_expr(p);
                expect(p, TK_SEMI);
                return s;
            }
            s = parse_simple(p);
            expect(p, TK_SEMI);
            return s;
    }
}

/* ---------- classes ---------- */
/* [Order(-10)] before a class (like DefaultExecutionOrder in Unity); [In] / [Out] before a field (ports of a graph
   node). Several in one [ ] or one after another. Returns FA_* flags; *order gets the number of Order. */
static int parse_attributes(P* p, int* order) {
    int attrs = 0;
    while (at(p, TK_LBRACK)) {
        next(p);
        do {
            Token* id = expect(p, TK_IDENT);
            if (id->len == 5 && !memcmp(id->start, "Order", 5)) {
                expect(p, TK_LPAREN);
                int neg = accept(p, TK_MINUS);
                Token* n = next(p);
                if (n->kind != TK_INT) fail(p->c, n->line, "[Order(...)] needs a whole number, e.g. [Order(-10)]");
                *order = neg ? -n->ival : n->ival;
                expect(p, TK_RPAREN);
                attrs |= FA_ORDER;
            } else if (id->len == 2 && !memcmp(id->start, "In", 2)) attrs |= FA_IN;
            else if (id->len == 3 && !memcmp(id->start, "Out", 3)) attrs |= FA_OUT;
            else fail(p->c, id->line, "attribute [%.*s] is not supported (only [Order(n)] before a class, [In] and [Out] before a field)", id->len, id->start);
        } while (accept(p, TK_COMMA));
        expect(p, TK_RBRACK);
    }
    return attrs;
}

static void parse_method_rest(P* p, MethodDecl* m) {
    expect(p, TK_LPAREN);
    int pcap = 0;
    if (!at(p, TK_RPAREN)) {
        do {
            m->params = (Param*)arena_grow(p->c->A, m->params, m->nparams, &pcap, sizeof(Param));
            Param* pa = &m->params[m->nparams++];
            pa->tn = parse_type(p);
            if (pa->tn->base == TK_VOID && pa->tn->dims == 0) fail(p->c, pa->tn->line, "a parameter cannot be 'void'");
            Token* pn = expect(p, TK_IDENT);
            pa->name = pn->start; pa->nlen = pn->len;
            if (at(p, TK_ASSIGN)) fail(p->c, pn->line, "default values for parameters are not supported yet; pass every argument");
        } while (accept(p, TK_COMMA));
    }
    expect(p, TK_RPAREN);
    if (m->isCtor && accept(p, TK_COLON)) {
        Token* b = next(p);
        if (b->kind == TK_THIS) fail(p->c, b->line, "': this(...)' is not supported (only one constructor per class for now)");
        if (b->kind != TK_BASE) fail(p->c, b->line, "expected 'base(...)' after ':'");
        Expr* call = mkexpr(p, E_CALL, b->line);
        call->args = parse_args(p, &call->nargs);
        m->baseCall = call;
    }
    if (accept(p, TK_SEMI)) {
        if (m->kind != MK_ABSTRACT) fail(p->c, m->line, "method '%.*s' needs a body { ... } (only abstract and interface methods end with ';')", m->nlen, m->name);
        return;
    }
    if (m->kind == MK_ABSTRACT) fail(p->c, m->line, "%s method '%.*s' cannot have a body; end it with ';'", p->inInterface ? "an interface" : "an abstract", m->nlen, m->name);
    p->sawYield = 0;
    if (at(p, TK_ARROW)) {                      /* int Area() => w * h;  void Hit() => hp--; */
        Token* t = next(p);
        Stmt* body = mkstmt(p, S_BLOCK, t->line);
        int isVoid = m->isCtor || (m->ret->base == TK_VOID && m->ret->dims == 0);
        Stmt* one = mkstmt(p, isVoid ? S_EXPR : S_RETURN, t->line);
        one->e = parse_expr(p);
        expect(p, TK_SEMI);
        body->list = (Stmt**)nw(p, sizeof(Stmt*));
        body->list[0] = one; body->n = 1;
        m->body = body;
        m->exprBody = 1;
        return;
    }
    m->body = parse_block(p);
    m->isIter = p->sawYield;
}
static void parse_class(P* p, ClassDecl* cd) {
    Token* id = expect(p, TK_IDENT);
    cd->name = id->start; cd->nlen = id->len; cd->line = id->line;
    if (at(p, TK_LT)) fail(p->c, id->line, "generic classes are not supported");
    if (accept(p, TK_COLON)) {
        int bcap = 0;
        do {
            cd->bases = (TypeNode**)arena_grow(p->c->A, cd->bases, cd->nbases, &bcap, sizeof(TypeNode*));
            TypeNode* b = parse_type(p);
            if (b->base != TK_IDENT || b->dims || b->arg) fail(p->c, b->line, "a class can only derive from a class or an interface");
            cd->bases[cd->nbases++] = b;
        } while (accept(p, TK_COMMA));
    }
    const char* what = cd->isInterface ? "interface" : "class";
    p->inInterface = cd->isInterface;
    expect(p, TK_LBRACE);
    int fcap = 0, mcap = 0;
    while (!at(p, TK_RBRACE)) {
        if (at(p, TK_EOF)) fail(p->c, cd->line, "%s '%.*s' is never closed with '}'", what, cd->nlen, cd->name);
        int isStatic = 0, kind = cd->isInterface ? MK_ABSTRACT : MK_NONE;
        int mst = p->pos;
        int morder = 0, mattrs = parse_attributes(p, &morder);
        if (mattrs & FA_ORDER) fail(p->c, p->t[mst].line, "[Order(n)] goes before a class, not before its members");
        Token* mod = peek(p);
        for (;;) {
            Token* m = peek(p);
            if (accept(p, TK_STATIC) || accept(p, TK_CONST)) isStatic = 1;   /* const fields are static */
            else if (accept(p, TK_PUBLIC) || accept(p, TK_PRIVATE) || accept(p, TK_READONLY) || accept(p, TK_SEALED)) {}
            else if (m->kind == TK_IDENT && m->len == 5 && !memcmp(m->start, "event", 5) && peekn(p, 1)->kind == TK_IDENT) next(p);   /* 'event Action x;' is a delegate field */
            else if (accept(p, TK_VIRTUAL) || accept(p, TK_OVERRIDE) || accept(p, TK_ABSTRACT)) {
                if (cd->isInterface) fail(p->c, m->line, "interface members cannot be '%.*s'", m->len, m->start);
                if (kind != MK_NONE) fail(p->c, m->line, "use only one of 'virtual', 'override' and 'abstract'");
                kind = m->kind == TK_VIRTUAL ? MK_VIRTUAL : m->kind == TK_OVERRIDE ? MK_OVERRIDE : MK_ABSTRACT;
            }
            else break;
        }
        if (kind == MK_ABSTRACT && !cd->isAbstract && !cd->isInterface)
            fail(p->c, mod->line, "class '%.*s' has an abstract method, so it must be 'abstract class'", cd->nlen, cd->name);
        if (kind != MK_NONE && !cd->isInterface && isStatic) fail(p->c, mod->line, "a static method cannot be virtual, override or abstract");
        if (cd->isInterface && isStatic) fail(p->c, mod->line, "interfaces cannot have static members here");
        Token* t = peek(p);
        /* constructor */
        if (t->kind == TK_IDENT && t->len == cd->nlen && !memcmp(t->start, cd->name, t->len) && peekn(p, 1)->kind == TK_LPAREN) {
            if (isStatic) fail(p->c, t->line, "static constructors are not supported");
            if (cd->isInterface) fail(p->c, t->line, "an interface cannot have a constructor");
            if (kind != MK_NONE) fail(p->c, t->line, "a constructor cannot be virtual, override or abstract");
            if (mattrs) fail(p->c, t->line, "[In] and [Out] go before a field (a port of a node), not before a constructor");
            next(p);
            cd->methods = (MethodDecl*)arena_grow(p->c->A, cd->methods, cd->nmethods, &mcap, sizeof(MethodDecl));
            MethodDecl* m = &cd->methods[cd->nmethods++];
            memset(m, 0, sizeof *m);
            m->line = t->line; m->isCtor = 1; m->name = t->start; m->nlen = t->len;
            parse_method_rest(p, m);
            m->s0 = p->t[mst].start; m->s1 = prev_end(p);
            continue;
        }
        if (t->kind == TK_CLASS || t->kind == TK_INTERFACE) fail(p->c, t->line, "nested classes are not supported");
        TypeNode* tn = parse_type(p);
        Token* name = expect(p, TK_IDENT);
        if (at(p, TK_LPAREN)) {
            if (mattrs) fail(p->c, name->line, "[In] and [Out] go before a field (a port of a node), not before a method");
            cd->methods = (MethodDecl*)arena_grow(p->c->A, cd->methods, cd->nmethods, &mcap, sizeof(MethodDecl));
            MethodDecl* m = &cd->methods[cd->nmethods++];
            memset(m, 0, sizeof *m);
            m->line = name->line; m->isStatic = isStatic; m->ret = tn; m->name = name->start; m->nlen = name->len; m->kind = kind;
            parse_method_rest(p, m);
            m->s0 = p->t[mst].start; m->s1 = prev_end(p);
            continue;
        }
        if (at(p, TK_LBRACE) || at(p, TK_ARROW))
            fail(p->c, name->line, "properties ({ get; set; } or =>) are not supported yet; use a field, or methods like Get%.*s()", name->len, name->start);
        if (cd->isInterface) fail(p->c, name->line, "interfaces can only have methods (no fields)");
        if (kind != MK_NONE) fail(p->c, name->line, "a field cannot be virtual, override or abstract");
        if (tn->base == TK_VOID && tn->dims == 0) fail(p->c, name->line, "a field cannot be 'void'");
        int ffirst = cd->nfields;
        for (;;) {
            cd->fields = (FieldDecl*)arena_grow(p->c->A, cd->fields, cd->nfields, &fcap, sizeof(FieldDecl));
            FieldDecl* f = &cd->fields[cd->nfields++];
            f->line = name->line; f->isStatic = isStatic; f->tn = tn; f->name = name->start; f->nlen = name->len; f->attrs = mattrs;
            f->init = accept(p, TK_ASSIGN) ? parse_expr(p) : NULL;
            if (!accept(p, TK_COMMA)) break;
            name = expect(p, TK_IDENT);
        }
        expect(p, TK_SEMI);
        for (int i = ffirst; i < cd->nfields; i++) { cd->fields[i].s0 = p->t[mst].start; cd->fields[i].s1 = prev_end(p); }  /* 'int a, b;' share one span */
    }
    next(p);
    p->inInterface = 0;
}

ProgramAST* ms_parse(ParseCtx* c, Token* toks, int ntok) {
    P p = { c, toks, 0, ntok, 0, 0, 0 };
    ProgramAST* prog = (ProgramAST*)arena_alloc(c->A, sizeof(ProgramAST));
    int cap = 0, nsDepth = 0;
    while (!at(&p, TK_EOF)) {
        if (accept(&p, TK_USING)) {       /* 'using System;' etc. is accepted and ignored */
            while (!at(&p, TK_SEMI) && !at(&p, TK_EOF)) next(&p);
            expect(&p, TK_SEMI);
            continue;
        }
        if (accept(&p, TK_NAMESPACE)) {   /* 'namespace A.B;' or 'namespace A.B { ... }' is ignored */
            expect(&p, TK_IDENT);
            while (accept(&p, TK_DOT)) expect(&p, TK_IDENT);
            if (!accept(&p, TK_SEMI)) { expect(&p, TK_LBRACE); nsDepth++; }
            continue;
        }
        if (nsDepth > 0 && accept(&p, TK_RBRACE)) { nsDepth--; continue; }
        int isAbstract = 0, cst = p.pos, order = 0;
        int attrs = parse_attributes(&p, &order);
        if (attrs & (FA_IN | FA_OUT)) fail(c, toks[cst].line, "[In] and [Out] go before a field (a port of a node); before a class only [Order(n)]");
        for (;;) {
            if (accept(&p, TK_PUBLIC) || accept(&p, TK_PRIVATE) || accept(&p, TK_STATIC) || accept(&p, TK_SEALED)) {}
            else if (accept(&p, TK_ABSTRACT)) isAbstract = 1;
            else break;
        }
        Token* t = peek(&p);
        if (t->kind != TK_CLASS && t->kind != TK_INTERFACE) fail(c, t->line, "expected 'class' or 'interface' at top level, found '%.*s'", t->len, t->start);
        if (t->kind == TK_INTERFACE && isAbstract) fail(c, t->line, "an interface is already abstract");
        next(&p);
        prog->classes = (ClassDecl*)arena_grow(c->A, prog->classes, prog->nclasses, &cap, sizeof(ClassDecl));
        ClassDecl* cd = &prog->classes[prog->nclasses++];
        memset(cd, 0, sizeof *cd);
        cd->isAbstract = isAbstract; cd->isInterface = t->kind == TK_INTERFACE; cd->order = order;
        parse_class(&p, cd);
        cd->s0 = toks[cst].start; cd->s1 = prev_end(&p);
    }
    if (nsDepth > 0) fail(c, peek(&p)->line, "expected '}' to close the namespace but reached end of file");
    prog->nlambdas = p.nlambdas;
    return prog;
}
