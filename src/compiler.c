/* MiniSharp - type checker and bytecode generator.
 * Walks the AST once (after collecting class signatures) and emits bytecode for a stack VM. */
#include "ms.h"
#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ================= types ================= */
typedef struct Type Type;
struct Type {
    int k; int cls;
    Type* elem;                     /* T[] / List<T>: element; Dictionary and KeyValuePair: value type */
    Type* key;                      /* Dictionary and KeyValuePair: key type */
    Type* arr; Type* list;          /* cached T[] and List<T> */
    Type* kvp; Type* next;          /* Dictionary: its KeyValuePair type; next Dictionary type in C.dicts */
    uint8_t* isa;                   /* class / interface: isa[k] = 1 if it can be used as class k (itself, bases, interfaces) */
    Type** params; int np;          /* Func / Action: parameter types; elem = return type (void for Action) */
};

static Type tVoid = {.k = T_VOID}, tInt = {.k = T_INT}, tFloat = {.k = T_FLOAT}, tBool = {.k = T_BOOL}, tString = {.k = T_STRING},
            tNull = {.k = T_NULL}, tAny = {.k = T_ANY}, tChar = {.k = T_CHAR}, tCoro = {.k = T_CORO};

typedef struct { const char* name; int nlen; Type* type; int isStatic; int slot; FieldDecl* decl; } CField;
/* sel >= 0: virtual, abstract, override or interface method, called through the class's vtable[sel];
 * func < 0: abstract (no body) */
typedef struct { const char* name; int nlen; int isStatic, isIter; Type* ret; Type** params; int np; int func, sel, owner; MethodDecl* decl; } CMethod;
typedef struct {
    const char* name; int nlen; Type* type;
    CField* fields; int nf; int ninst;          /* own fields; ninst counts the inherited ones too */
    CMethod* methods; int nm;                   /* own methods */
    int ctorFunc; MethodDecl* ctor; Type** ctorParams; int ctorNp;
    ClassDecl* decl;
    int base;                                   /* base class or -1 */
    int* ifaces; int nifaces;                   /* interfaces listed after ':' (for an interface: the ones it extends) */
    int isInterface, isAbstract, state;         /* state: hierarchy walk (0 new, 1 visiting, 2 done) */
    int* vtable;                                /* [nsel]: function, -1 not there, -2 abstract */
} CClass;

/* depth: 0 for the method, 1 inside a lambda, 2 in a lambda inside that...
 * boxed: a lambda uses it, so the slot holds a cell (ObjCell) with the value */
typedef struct { const char* name; int nlen; Type* type; int slot; int depth, boxed; } Local;

/* the lambda being compiled: the cells it captures from the functions around it */
typedef struct LamCtx {
    int depth;
    struct { int li, from; } ups[64];   /* li: index in C.locals; from: upvalue of the parent lambda, or -1 = parent's local */
    int nup;
    struct LamCtx* parent;
} LamCtx;
typedef struct { const char* n; int l; } Name;
typedef struct { Name* v; int n, cap; } NameList;

typedef struct Loop {
    int isSwitch;                   /* 'break' leaves a switch too, 'continue' skips past it */
    int breaks[128], nb;
    int conts[128], nc;
    int contTarget;                 /* >= 0: continue jumps straight back here */
    struct Loop* prev;
} Loop;

typedef struct {
    ParseCtx pc; Arena A; jmp_buf jb;
    Program* P;
    CClass* classes; int nclasses;
    int cur, curStatic, isCtor, isIter;
    Type* retType;
    Func* fn; int line;
    Local locals[256]; int nlocals; int nextSlot;
    Loop* loop;
    int lastOp, lastLabel;
    Type* dicts;                    /* every Dictionary<K,V> type made so far */
    int* order;                     /* classes with every base before the classes that derive from it */
    CMethod** sels; int nsel, capsel;   /* the method that introduced each virtual selector */
    int toStringSel;                /* selector of 'override string ToString()', or -1 */
    Type* funcs;                    /* every Func / Action type made so far */
    int fdepth; LamCtx* lam;        /* lambda nesting while compiling a method */
    NameList caps;                  /* names that lambdas in this method use from outside themselves */
    int lastBoxed;                  /* the local declare_local just made is boxed */
    Type* stackType;                /* type of the E_STACK value (a?.b) */
    /* REPL: variables declared at the top of Repl.Main's body become static fields */
    Stmt* replBody;
    Local replVars[64]; int nreplVars;          /* slot = static slot */
    char* replDecls; int replDeclsCap;          /* "static int x; " for each of them */
    int replPrint;                              /* native Console.WriteLine(x) */
} C;

static void cfail(C* c, int line, const char* fmt, ...) __attribute__((noreturn));
static void cfail(C* c, int line, const char* fmt, ...) {
    char msg[220];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    snprintf(c->pc.err, c->pc.errlen, "line %d: %s", line, msg);
    longjmp(c->jb, 1);
}

static int nameeq(const char* a, int al, const char* b, int bl) { return al == bl && !memcmp(a, b, al); }
static int namecs(const char* a, int al, const char* s) { return nameeq(a, al, s, (int)strlen(s)); }

static Type* array_of(C* c, Type* t) {
    if (!t->arr) {
        Type* a = (Type*)arena_alloc(&c->A, sizeof(Type));
        a->k = T_ARRAY; a->elem = t;
        t->arr = a;
    }
    return t->arr;
}
static Type* dict_of(C* c, Type* k, Type* v) {
    for (Type* d = c->dicts; d; d = d->next) if (d->key == k && d->elem == v) return d;
    Type* d = (Type*)arena_alloc(&c->A, sizeof(Type));
    d->k = T_DICT; d->key = k; d->elem = v; d->next = c->dicts; c->dicts = d;
    Type* p = (Type*)arena_alloc(&c->A, sizeof(Type));
    p->k = T_KVP; p->key = k; p->elem = v;
    d->kvp = p;
    return d;
}
static Type* func_of(C* c, Type** ps, int np, Type* ret) {
    for (Type* f = c->funcs; f; f = f->next)
        if (f->np == np && f->elem == ret && (!np || !memcmp(f->params, ps, sizeof(Type*) * np))) return f;
    Type* f = (Type*)arena_alloc(&c->A, sizeof(Type));
    f->k = T_FUNC; f->elem = ret; f->np = np;
    f->params = (Type**)arena_alloc(&c->A, sizeof(Type*) * (np ? np : 1));
    if (np) memcpy(f->params, ps, sizeof(Type*) * np);
    f->next = c->funcs; c->funcs = f;
    return f;
}
static Type* list_of(C* c, Type* t) {
    if (!t->list) {
        Type* l = (Type*)arena_alloc(&c->A, sizeof(Type));
        l->k = T_LIST; l->elem = t;
        t->list = l;
    }
    return t->list;
}

static const char* tname(C* c, Type* t) {
    static char bufs[6][96]; static int bi = 0;
    char* b = bufs[bi = (bi + 1) % 6];
    switch (t->k) {
        case T_VOID: return "void"; case T_INT: return "int"; case T_FLOAT: return "float";
        case T_BOOL: return "bool"; case T_STRING: return "string"; case T_NULL: return "null";
        case T_ANY: return "any"; case T_CHAR: return "char"; case T_CORO: return "IEnumerator";
        case T_LIST: snprintf(b, 96, "List<%s>", tname(c, t->elem)); return b;
        case T_DICT: snprintf(b, 96, "Dictionary<%s, %s>", tname(c, t->key), tname(c, t->elem)); return b;
        case T_KVP: snprintf(b, 96, "KeyValuePair<%s, %s>", tname(c, t->key), tname(c, t->elem)); return b;
        case T_CLASS: snprintf(b, 96, "%.*s", c->classes[t->cls].nlen, c->classes[t->cls].name); return b;
        case T_ARRAY: snprintf(b, 96, "%s[]", tname(c, t->elem)); return b;
        case T_FUNC: {
            int isAction = t->elem->k == T_VOID, n = snprintf(b, 96, isAction ? "Action" : "Func");
            for (int i = 0; i < t->np + (isAction ? 0 : 1) && n < 90; i++)
                n += snprintf(b + n, 96 - n, "%s%s", i ? ", " : "<", tname(c, i < t->np ? t->params[i] : t->elem));
            if (t->np || !isAction) snprintf(b + n, 96 - n, ">");
            return b;
        }
    }
    return "?";
}

static int isint(Type* t) { return t->k == T_INT || t->k == T_CHAR; }
static int isnum(Type* t) { return isint(t) || t->k == T_FLOAT; }
static int isref(Type* t) { return t->k == T_STRING || t->k == T_CLASS || t->k == T_ARRAY || t->k == T_LIST || t->k == T_DICT || t->k == T_CORO || t->k == T_NULL || t->k == T_FUNC; }

static int assignable(Type* from, Type* to) {
    if (from == to) return 1;
    if (to->k == T_ANY) return from->k != T_VOID;
    if (from->k == T_INT && to->k == T_FLOAT) return 1;
    if (from->k == T_CHAR && (to->k == T_INT || to->k == T_FLOAT)) return 1;
    if (from->k == T_NULL && (to->k == T_STRING || to->k == T_CLASS || to->k == T_ARRAY || to->k == T_LIST || to->k == T_DICT || to->k == T_CORO || to->k == T_FUNC)) return 1;
    if (from->k == T_CLASS && to->k == T_CLASS) return from->isa[to->cls];     /* derived -> base, class -> interface */
    return 0;
}

static Type* code_type(C* c, int k) {
    switch (k) {
        case T_INT: return &tInt; case T_FLOAT: return &tFloat; case T_BOOL: return &tBool;
        case T_STRING: return &tString; case T_ANY: return &tAny; case T_CHAR: return &tChar;
        case T_STRARR: return array_of(c, &tString);
        default: return &tVoid;
    }
}

static Value default_value(Type* t) {
    Value v; v.t = V_NULL; v.as.i = 0;
    if (t->k == T_INT) v.t = V_INT;
    else if (t->k == T_FLOAT) { v.t = V_FLOAT; v.as.f = 0; }
    else if (t->k == T_BOOL) v.t = V_BOOL;
    else if (t->k == T_CHAR) v.t = V_CHAR;
    return v;
}
static int default_kind(Type* t) { return t->k == T_INT ? 1 : t->k == T_FLOAT ? 2 : t->k == T_BOOL ? 3 : t->k == T_CHAR ? 4 : 0; }

/* ================= emission ================= */
static void emit1(C* c, int32_t v) {
    Func* f = c->fn;
    if (f->len == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 64;
        f->code = (int32_t*)realloc(f->code, sizeof(int32_t) * f->cap);
        f->lines = (int*)realloc(f->lines, sizeof(int) * f->cap);
        if (!f->code || !f->lines) cfail(c, c->line, "out of memory");
    }
    f->code[f->len] = v; f->lines[f->len] = c->line; f->len++;
}
static void op(C* c, int o) { c->lastOp = c->fn->len; emit1(c, o); }
static void op1(C* c, int o, int a) { op(c, o); emit1(c, a); }
static void op2(C* c, int o, int a, int b) { op(c, o); emit1(c, a); emit1(c, b); }
static int jump(C* c, int o) { op(c, o); emit1(c, 0); return c->fn->len - 1; }
static void patch(C* c, int pos) { c->fn->code[pos] = c->fn->len - (pos + 1); c->lastLabel = c->fn->len; }
static void jump_back(C* c, int o, int target) { op(c, o); emit1(c, target - (c->fn->len + 1)); }
static int label(C* c) { c->lastLabel = c->fn->len; return c->fn->len; }

static void emit_pop(C* c) {
    Func* f = c->fn;
    if (c->lastOp >= 0 && c->lastOp == f->len - 2 && f->code[c->lastOp] == OP_STORE && c->lastLabel != f->len) {
        f->code[c->lastOp] = OP_STOREP;
        c->lastOp = -1;
        return;
    }
    op(c, OP_POP);
}

static void emit_fconst(C* c, float v) { int32_t bits; memcpy(&bits, &v, 4); op1(c, OP_FCONST, bits); }

static int add_string(C* c, const char* s, int len) {
    Program* P = c->P;
    for (int i = 0; i < P->nstrings; i++)
        if (P->strings[i]->len == len && !memcmp(P->strings[i]->chars, s, len)) return i;
    if (P->nstrings == P->capstrings) {
        P->capstrings = P->capstrings ? P->capstrings * 2 : 16;
        P->strings = (ObjString**)realloc(P->strings, sizeof(ObjString*) * P->capstrings);
    }
    ObjString* o = (ObjString*)malloc(sizeof(ObjString) + len + 1);
    o->h.type = O_STRING; o->h.marked = 0; o->h.pinned = 1; o->h.next = NULL;
    o->len = len; memcpy(o->chars, s, len); o->chars[len] = 0;
    P->strings[P->nstrings] = o;
    return P->nstrings++;
}

static void push_default(C* c, Type* t) {
    switch (t->k) {
        case T_INT: op1(c, OP_ICONST, 0); break;
        case T_FLOAT: emit_fconst(c, 0.0f); break;
        case T_BOOL: op(c, OP_FALSE); break;
        case T_CHAR: op1(c, OP_ICONST, 0); op(c, OP_I2C); break;
        default: op(c, OP_NULL); break;
    }
}

/* ================= lookups ================= */
/* fields and methods are looked up in the class, then its base classes */
static CField* find_field(C* c, int cls, const char* n, int l) {
    for (; cls >= 0; cls = c->classes[cls].base) {
        CClass* k = &c->classes[cls];
        for (int i = 0; i < k->nf; i++) if (nameeq(k->fields[i].name, k->fields[i].nlen, n, l)) return &k->fields[i];
    }
    return NULL;
}
static CMethod* find_own_method(C* c, int cls, const char* n, int l) {
    CClass* k = &c->classes[cls];
    for (int i = 0; i < k->nm; i++) if (nameeq(k->methods[i].name, k->methods[i].nlen, n, l)) return &k->methods[i];
    return NULL;
}
static CMethod* find_method(C* c, int cls, const char* n, int l) {
    for (int k = cls; k >= 0; k = c->classes[k].base) {
        CMethod* m = find_own_method(c, k, n, l);
        if (m) return m;
    }
    if (c->classes[cls].isInterface)            /* an interface also has the methods of the interfaces it extends */
        for (int i = 0; i < c->classes[cls].nifaces; i++) {
            CMethod* m = find_method(c, c->classes[cls].ifaces[i], n, l);
            if (m) return m;
        }
    return NULL;
}
static int find_class(C* c, const char* n, int l) {
    for (int i = 0; i < c->nclasses; i++) if (nameeq(c->classes[i].name, c->classes[i].nlen, n, l)) return i;
    return -1;
}
static int is_builtin_class(const char* n, int l) {
    for (int i = 0; i < ms_nnatives; i++) if (namecs(n, l, ms_natives[i].cls)) return 1;
    for (int i = 0; i < ms_nconsts; i++) if (namecs(n, l, ms_consts[i].cls)) return 1;
    return 0;
}
static int find_local(C* c, const char* n, int l) {
    for (int i = c->nlocals - 1; i >= 0; i--) if (nameeq(c->locals[i].name, c->locals[i].nlen, n, l)) return i;
    return -1;
}

enum { R_NONE, R_LOCAL, R_FIELD, R_STATIC, R_CLASS, R_BUILTIN, R_METHOD, R_CELL /* boxed local */, R_UPVAL /* captured by this lambda */ };
typedef struct { int k; int idx; Type* type; } Res;

/* index of the captured local li among the cells of lambda lc (adding it, and to the lambdas around it) */
static int get_upvalue(C* c, LamCtx* lc, int li) {
    for (int k = 0; k < lc->nup; k++) if (lc->ups[k].li == li) return k;
    if (lc->nup >= 64) cfail(c, c->line, "a lambda can use at most 64 variables from outside");
    int from = c->locals[li].depth < lc->depth - 1 ? get_upvalue(c, lc->parent, li) : -1;
    lc->ups[lc->nup].li = li; lc->ups[lc->nup].from = from;
    return lc->nup++;
}

static Res resolve(C* c, const char* n, int l) {
    Res r = { R_NONE, 0, NULL };
    int li = find_local(c, n, l);
    if (li >= 0) {
        Local* lo = &c->locals[li];
        r.type = lo->type;
        if (lo->depth == c->fdepth) { r.k = lo->boxed ? R_CELL : R_LOCAL; r.idx = lo->slot; return r; }
        if (!lo->boxed) cfail(c, c->line, "'%.*s' cannot be used inside a lambda; copy it to a variable first", l, n);
        r.k = R_UPVAL; r.idx = get_upvalue(c, c->lam, li);
        return r;
    }
    for (int i = 0; i < c->nreplVars; i++)
        if (nameeq(c->replVars[i].name, c->replVars[i].nlen, n, l)) { r.k = R_STATIC; r.idx = c->replVars[i].slot; r.type = c->replVars[i].type; return r; }
    CField* f = find_field(c, c->cur, n, l);
    if (f) { r.k = f->isStatic ? R_STATIC : R_FIELD; r.idx = f->slot; r.type = f->type; return r; }
    if (find_method(c, c->cur, n, l)) { r.k = R_METHOD; return r; }
    int ci = find_class(c, n, l);
    if (ci >= 0) { r.k = R_CLASS; r.idx = ci; return r; }
    if (is_builtin_class(n, l)) { r.k = R_BUILTIN; return r; }
    return r;
}

/* ================= locals ================= */
static int declare_local(C* c, const char* n, int l, Type* t, int line) {
    if (find_local(c, n, l) >= 0) cfail(c, line, "a variable named '%.*s' already exists here", l, n);
    if (c->nlocals >= 256) cfail(c, line, "too many local variables in one method");
    int slot = c->nextSlot++;
    if (c->nextSlot > c->fn->nlocals) c->fn->nlocals = c->nextSlot;
    Local* lo = &c->locals[c->nlocals++];
    lo->name = n; lo->nlen = l; lo->type = t; lo->slot = slot;
    lo->depth = c->fdepth;
    lo->boxed = 0;
    for (int i = 0; i < c->caps.n; i++) if (nameeq(c->caps.v[i].n, c->caps.v[i].l, n, l)) lo->boxed = 1;
    c->lastBoxed = lo->boxed;
    return slot;
}
/* the value on top of the stack is the first value of a new local (in a fresh cell if a lambda uses it) */
static void init_local(C* c, int slot, int boxed) {
    if (boxed) op(c, OP_BOX);
    op1(c, OP_STORE, slot);
    emit_pop(c);
}
/* parameters that lambdas use are moved into cells at the start of the function */
static void box_params(C* c) {
    for (int i = 0; i < c->nlocals; i++)
        if (c->locals[i].depth == c->fdepth && c->locals[i].boxed) {
            op1(c, OP_LOAD, c->locals[i].slot);
            init_local(c, c->locals[i].slot, 1);
        }
}
static int alloc_temp(C* c) {
    int slot = c->nextSlot++;
    if (c->nextSlot > c->fn->nlocals) c->fn->nlocals = c->nextSlot;
    return slot;
}

static Type* resolve_typenode(C* c, TypeNode* tn, int extraDims) {
    Type* t;
    switch (tn->base) {
        case TK_KINT: t = &tInt; break;
        case TK_KFLOAT: t = &tFloat; break;
        case TK_KBOOL: t = &tBool; break;
        case TK_KSTRING: t = &tString; break;
        case TK_KCHAR: t = &tChar; break;
        case TK_VOID: t = &tVoid; break;
        default: {
            if (namecs(tn->name, tn->nlen, "Dictionary") || namecs(tn->name, tn->nlen, "KeyValuePair")) {
                if (!tn->arg2 || tn->nargs > 2) cfail(c, tn->line, "write both types: %.*s<string, int>, ...", tn->nlen, tn->name);
                Type* kt = resolve_typenode(c, tn->arg, 0);
                Type* vt = resolve_typenode(c, tn->arg2, 0);
                if (kt->k == T_VOID || vt->k == T_VOID) cfail(c, tn->line, "a Dictionary cannot hold void");
                t = dict_of(c, kt, vt);
                if (tn->name[0] == 'K') t = t->kvp;
                break;
            }
            if (namecs(tn->name, tn->nlen, "List")) {
                if (!tn->arg) cfail(c, tn->line, "write the element type: List<int>, List<string>, ...");
                if (tn->nargs > 1) cfail(c, tn->line, "List takes one type: List<int>");
                Type* el = resolve_typenode(c, tn->arg, 0);
                if (el->k == T_VOID) cfail(c, tn->line, "cannot make a List of void");
                t = list_of(c, el);
                break;
            }
            int isFunc = namecs(tn->name, tn->nlen, "Func"), isAction = namecs(tn->name, tn->nlen, "Action");
            int isPred = namecs(tn->name, tn->nlen, "Predicate"), isCmp = namecs(tn->name, tn->nlen, "Comparison");
            if (isFunc || isAction || isPred || isCmp) {
                Type* ps[16]; int np = 0; Type* ret = &tVoid;
                if (tn->nargs > 16) cfail(c, tn->line, "too many type arguments");
                if (isFunc) {
                    if (!tn->nargs) cfail(c, tn->line, "write the types: Func<int> returns an int, Func<int, bool> takes an int and returns a bool");
                    for (int i = 0; i < tn->nargs - 1; i++) ps[np++] = resolve_typenode(c, tn->args[i], 0);
                    ret = resolve_typenode(c, tn->args[tn->nargs - 1], 0);
                    if (ret->k == T_VOID) cfail(c, tn->line, "a Func must return something; for no result use Action<...>");
                } else if (isAction) {
                    for (int i = 0; i < tn->nargs; i++) ps[np++] = resolve_typenode(c, tn->args[i], 0);
                } else {
                    if (tn->nargs != 1) cfail(c, tn->line, "%.*s takes one type: %.*s<int>", tn->nlen, tn->name, tn->nlen, tn->name);
                    Type* a = resolve_typenode(c, tn->args[0], 0);
                    ps[np++] = a;
                    if (isCmp) { ps[np++] = a; ret = &tInt; } else ret = &tBool;
                }
                for (int i = 0; i < np; i++) if (ps[i]->k == T_VOID) cfail(c, tn->line, "a parameter cannot be void");
                t = func_of(c, ps, np, ret);
                break;
            }
            if (tn->arg) cfail(c, tn->line, "'%.*s' is not generic (only List<T> is)", tn->nlen, tn->name);
            /* IEnumerator (iterator methods) and Coroutine (what StartCoroutine returns) are the same thing here */
            if (namecs(tn->name, tn->nlen, "IEnumerator") || namecs(tn->name, tn->nlen, "Coroutine")) { t = &tCoro; break; }
            int ci = find_class(c, tn->name, tn->nlen);
            if (ci < 0) cfail(c, tn->line, "unknown type '%.*s'", tn->nlen, tn->name);
            t = c->classes[ci].type;
        }
    }
    int dims = tn->dims + extraDims;
    if (dims && t->k == T_VOID) cfail(c, tn->line, "cannot make an array of void");
    for (int i = 0; i < dims; i++) t = array_of(c, t);
    return t;
}

/* ================= expressions ================= */
static Type* expr(C* c, Expr* e);
static void expr_to(C* c, Expr* e, Type* t, int line, const char* what);

static void coerce(C* c, Type* from, Type* to, int line, const char* what) {
    if (!assignable(from, to)) {
        if (from->k == T_VOID) cfail(c, line, "%s: this expression has no value (void)", what);
        if (from->k == T_FLOAT && to->k == T_INT) cfail(c, line, "%s: cannot convert float to int automatically, use (int)", what);
        if (from->k == T_CLASS && to->k == T_CLASS && to->isa[from->cls])
            cfail(c, line, "%s: not every %s is a %s; use a cast (%s)x, or 'x as %s' / 'x is %s y'", what,
                  tname(c, from), tname(c, to), tname(c, to), tname(c, to), tname(c, to));
        cfail(c, line, "%s: cannot convert %s to %s", what, tname(c, from), tname(c, to));
    }
    if ((from->k == T_INT || from->k == T_CHAR) && to->k == T_FLOAT) op1(c, OP_I2F, 0);
    else if (from->k == T_CHAR && to->k == T_INT) op1(c, OP_C2I, 0);
}

static Type* peek_type(C* c, Expr* e) {
    Func* f = c->fn;
    int len = f->len, ll = c->lastLabel, ns = c->nextSlot, nl = c->nlocals, line = c->line, nf = c->P->nfuncs;
    Type* t = expr(c, e);
    f->len = len; c->lastOp = -1; c->lastLabel = ll; c->nextSlot = ns; c->nlocals = nl; c->line = line;
    for (int i = nf; i < c->P->nfuncs; i++) {
        Func* g = &c->P->funcs[i];
        free(g->code); free(g->lines);
        memset(g, 0, sizeof *g);
    }
    c->P->nfuncs = nf;
    return t;
}

static void tostr(C* c, Type* t, int depth, int line) {
    if (t->k == T_VOID) cfail(c, line, "cannot use a void value in a string");
    int kind = t->k == T_INT ? 0 : t->k == T_FLOAT ? 1 : t->k == T_BOOL ? 2 : 3;
    op2(c, OP_TOSTR, kind, depth);
}

static const char* opname(int tok) {
    switch (tok) {
        case TK_PLUS: return "+"; case TK_MINUS: return "-"; case TK_STAR: return "*";
        case TK_SLASH: return "/"; case TK_PERCENT: return "%"; case TK_LT: return "<";
        case TK_LE: return "<="; case TK_GT: return ">"; case TK_GE: return ">=";
        case TK_EQ: return "=="; case TK_NE: return "!=";
        case TK_AMP: return "&"; case TK_PIPE: return "|"; case TK_CARET: return "^";
        case TK_SHL: return "<<"; case TK_SHR: return ">>";
    }
    return "?";
}

/* both operands already on the stack */
static Type* arith(C* c, int tok, Type* lt, Type* rt, int line) {
    if (tok == TK_PLUS && (lt->k == T_STRING || rt->k == T_STRING)) {
        if (lt->k != T_STRING) tostr(c, lt, 1, line);
        if (rt->k != T_STRING) tostr(c, rt, 0, line);
        op(c, OP_CONCAT);
        return &tString;
    }
    if (!isnum(lt) || !isnum(rt))
        cfail(c, line, "operator '%s' cannot be used with %s and %s", opname(tok), tname(c, lt), tname(c, rt));
    int f = lt->k == T_FLOAT || rt->k == T_FLOAT;
    if (f) { if (lt->k != T_FLOAT) op1(c, OP_I2F, 1); if (rt->k != T_FLOAT) op1(c, OP_I2F, 0); }
    else if (lt->k == T_CHAR) op1(c, OP_C2I, 1);       /* 'a' + 1 is an int */
    switch (tok) {
        case TK_PLUS: op(c, f ? OP_FADD : OP_IADD); break;
        case TK_MINUS: op(c, f ? OP_FSUB : OP_ISUB); break;
        case TK_STAR: op(c, f ? OP_FMUL : OP_IMUL); break;
        case TK_SLASH: op(c, f ? OP_FDIV : OP_IDIV); break;
        case TK_PERCENT: op(c, f ? OP_FMOD : OP_IMOD); break;
    }
    return f ? &tFloat : &tInt;
}

/* & | ^ on ints or bools, << >> on ints; both operands already on the stack */
static Type* bitwise(C* c, int tok, Type* lt, Type* rt, int line) {
    if (lt->k == T_BOOL && rt->k == T_BOOL && tok != TK_SHL && tok != TK_SHR) {
        op(c, tok == TK_AMP ? OP_IAND : tok == TK_PIPE ? OP_IOR : OP_IXOR);
        return &tBool;
    }
    if (!isint(lt) || !isint(rt))
        cfail(c, line, "operator '%s' cannot be used with %s and %s", opname(tok), tname(c, lt), tname(c, rt));
    if (lt->k == T_CHAR) op1(c, OP_C2I, 1);
    switch (tok) {
        case TK_AMP: op(c, OP_IAND); break;
        case TK_PIPE: op(c, OP_IOR); break;
        case TK_CARET: op(c, OP_IXOR); break;
        case TK_SHL: op(c, OP_ISHL); break;
        case TK_SHR: op(c, OP_ISHR); break;
    }
    return &tInt;
}

static int is_bitwise(int tok) { return tok == TK_AMP || tok == TK_PIPE || tok == TK_CARET || tok == TK_SHL || tok == TK_SHR; }

static Type* compare(C* c, int tok, Type* lt, Type* rt, int line) {
    if (isnum(lt) && isnum(rt)) {
        int f = lt->k == T_FLOAT || rt->k == T_FLOAT;
        if (f) { if (lt->k != T_FLOAT) op1(c, OP_I2F, 1); if (rt->k != T_FLOAT) op1(c, OP_I2F, 0); }
        switch (tok) {
            case TK_LT: op(c, f ? OP_FLT : OP_ILT); break;
            case TK_LE: op(c, f ? OP_FLE : OP_ILE); break;
            case TK_GT: op(c, f ? OP_FGT : OP_IGT); break;
            case TK_GE: op(c, f ? OP_FGE : OP_IGE); break;
            case TK_EQ: op(c, f ? OP_FEQ : OP_IEQ); break;
            case TK_NE: op(c, f ? OP_FNE : OP_INE); break;
        }
        return &tBool;
    }
    if (tok != TK_EQ && tok != TK_NE)
        cfail(c, line, "operator '%s' cannot be used with %s and %s", opname(tok), tname(c, lt), tname(c, rt));
    if (lt->k == T_BOOL && rt->k == T_BOOL) { op(c, tok == TK_EQ ? OP_IEQ : OP_INE); return &tBool; }
    if ((lt->k == T_STRING || rt->k == T_STRING) && isref(lt) && isref(rt) &&
        (lt->k == T_STRING || lt->k == T_NULL) && (rt->k == T_STRING || rt->k == T_NULL)) {
        op(c, tok == TK_EQ ? OP_SEQ : OP_SNE); return &tBool;
    }
    if (isref(lt) && isref(rt) && (lt == rt || lt->k == T_NULL || rt->k == T_NULL || (lt->k == T_CLASS && rt->k == T_CLASS))) {
        op(c, tok == TK_EQ ? OP_REQ : OP_RNE); return &tBool;
    }
    cfail(c, line, "cannot compare %s with %s", tname(c, lt), tname(c, rt));
}

static void need_bool(C* c, Type* t, int line, const char* what) {
    if (t->k != T_BOOL) cfail(c, line, "%s must be bool, not %s", what, tname(c, t));
}

/* ---------- l-values ---------- */
enum { LV_LOCAL, LV_STATIC, LV_FIELD, LV_ELEM, LV_CELL };
typedef struct { int kind, idx; Type* type; } LV;

static LV lvalue(C* c, Expr* e) {
    LV lv = { 0, 0, NULL };
    c->line = e->line;
    if (e->k == E_NAME) {
        Res r = resolve(c, e->name, e->nlen);
        switch (r.k) {
            case R_LOCAL: lv.kind = LV_LOCAL; lv.idx = r.idx; lv.type = r.type; return lv;
            case R_CELL: case R_UPVAL:
                op1(c, r.k == R_CELL ? OP_LOAD : OP_UPVAL, r.idx);
                lv.kind = LV_CELL; lv.type = r.type; return lv;
            case R_STATIC: lv.kind = LV_STATIC; lv.idx = r.idx; lv.type = r.type; return lv;
            case R_FIELD:
                if (c->curStatic) cfail(c, e->line, "cannot use instance field '%.*s' in a static method", e->nlen, e->name);
                op1(c, OP_LOAD, 0);
                lv.kind = LV_FIELD; lv.idx = r.idx; lv.type = r.type; return lv;
            case R_NONE: cfail(c, e->line, "unknown name '%.*s'", e->nlen, e->name);
            default: cfail(c, e->line, "cannot assign to '%.*s'", e->nlen, e->name);
        }
    }
    if (e->k == E_MEMBER) {
        if (e->a->k == E_NAME) {
            Res r = resolve(c, e->a->name, e->a->nlen);
            if (r.k == R_CLASS) {
                CField* f = find_field(c, r.idx, e->name, e->nlen);
                if (!f) cfail(c, e->line, "class '%.*s' has no field '%.*s'", e->a->nlen, e->a->name, e->nlen, e->name);
                if (!f->isStatic) cfail(c, e->line, "'%.*s' is not static; use an object", e->nlen, e->name);
                lv.kind = LV_STATIC; lv.idx = f->slot; lv.type = f->type; return lv;
            }
            if (r.k == R_BUILTIN) cfail(c, e->line, "'%.*s.%.*s' is read-only", e->a->nlen, e->a->name, e->nlen, e->name);
        }
        Type* t = expr(c, e->a);
        c->line = e->line;
        if (t->k == T_CLASS) {
            CField* f = find_field(c, t->cls, e->name, e->nlen);
            if (!f) cfail(c, e->line, "%s has no field '%.*s'", tname(c, t), e->nlen, e->name);
            if (f->isStatic) cfail(c, e->line, "'%.*s' is static; use %s.%.*s", e->nlen, e->name, tname(c, t), e->nlen, e->name);
            lv.kind = LV_FIELD; lv.idx = f->slot; lv.type = f->type; return lv;
        }
        cfail(c, e->line, "cannot assign to '%.*s' of %s", e->nlen, e->name, tname(c, t));
    }
    if (e->k == E_INDEX) {
        Type* t = expr(c, e->a);
        if (t->k == T_STRING) cfail(c, e->line, "strings cannot be changed; build a new string instead");
        if (t->k == T_DICT) {
            expr_to(c, e->b, t->key, e->line, "dictionary key");
            c->line = e->line;
            lv.kind = LV_ELEM; lv.type = t->elem; return lv;
        }
        if (t->k != T_ARRAY && t->k != T_LIST) cfail(c, e->line, "only arrays and lists can be indexed, not %s", tname(c, t));
        Type* it = expr(c, e->b);
        if (!isint(it)) cfail(c, e->line, "index must be int, not %s", tname(c, it));
        c->line = e->line;
        lv.kind = LV_ELEM; lv.type = t->elem; return lv;
    }
    cfail(c, e->line, "the left side must be a variable, field or array element");
}
static void lv_dup(C* c, LV* lv) { if (lv->kind == LV_FIELD || lv->kind == LV_CELL) op(c, OP_DUP); else if (lv->kind == LV_ELEM) op(c, OP_DUP2); }
static void lv_load(C* c, LV* lv) {
    switch (lv->kind) {
        case LV_LOCAL: op1(c, OP_LOAD, lv->idx); break;
        case LV_STATIC: op1(c, OP_GETSTATIC, lv->idx); break;
        case LV_FIELD: op1(c, OP_GETFIELD, lv->idx); break;
        case LV_ELEM: op(c, OP_GETELEM); break;
        case LV_CELL: op(c, OP_GETCELL); break;
    }
}
static void lv_store(C* c, LV* lv) {
    switch (lv->kind) {
        case LV_LOCAL: op1(c, OP_STORE, lv->idx); break;
        case LV_STATIC: op1(c, OP_SETSTATIC, lv->idx); break;
        case LV_FIELD: op1(c, OP_SETFIELD, lv->idx); break;
        case LV_ELEM: op(c, OP_SETELEM); break;
        case LV_CELL: op(c, OP_SETCELL); break;
    }
}

/* ---------- calls ---------- */
static void args_for(C* c, Expr* e, Type** params, int np, const char* what) {
    if (e->nargs != np) cfail(c, e->line, "%s takes %d argument%s, but %d were given", what, np, np == 1 ? "" : "s", e->nargs);
    for (int i = 0; i < np; i++) {
        char buf[120]; snprintf(buf, sizeof buf, "argument %d of %s", i + 1, what);
        if (params[i]->k == T_FUNC || (params[i]->k == T_CLASS && e->args[i]->k == E_COND)) { expr_to(c, e->args[i], params[i], e->args[i]->line, buf); c->line = e->line; continue; }
        Type* t = expr(c, e->args[i]);
        c->line = e->line;
        if (params[i]->k == T_ANY && t->k == T_CLASS) { tostr(c, t, 0, e->line); t = &tString; }   /* may call an override of ToString */
        coerce(c, t, params[i], e->args[i]->line, buf);
    }
}

/* virt: call through the vtable of the object (virtual and interface methods); otherwise this exact method */
static Type* call_method(C* c, Expr* e, CMethod* m, int hasThis, int virt) {
    char what[100];
    CClass* owner = &c->classes[m->owner];
    snprintf(what, sizeof what, "'%.*s.%.*s'", owner->nlen, owner->name, m->nlen, m->name);
    args_for(c, e, m->params, m->np, what);
    c->line = e->line;
    if (virt && m->sel >= 0) op2(c, OP_CALLV, m->sel, m->np + 1);     /* the VM looks up the function (or coroutine) */
    else {
        if (m->func < 0) cfail(c, e->line, "'%.*s' is abstract and has no body to call", m->nlen, m->name);
        op2(c, m->isIter ? OP_CORO_NEW : OP_CALL, m->func, m->np + (hasThis ? 1 : 0));
    }
    return m->ret;
}

/* Built-in method from the native table. With 'self' the receiver is already on the stack and
 * the table lists it as the first parameter (class names starting with '.', e.g. ".string"). */
static Type* call_native(C* c, Expr* e, const char* cls, int clen, int self) {
    Expr* f = e->a;
    /* overloads: same name, pick by argument count, then exact types, then convertible types */
    Type* at[MS_MAXNATIVEARGS];
    if (e->nargs > MS_MAXNATIVEARGS) cfail(c, e->line, "too many arguments for '%.*s'", f->nlen, f->name);
    for (int i = 0; i < e->nargs; i++) at[i] = peek_type(c, e->args[i]);
    int ni = -1, any = -1, sameCount = -1;
    for (int pass = 0; pass < 2 && ni < 0; pass++)
        for (int i = 0; i < ms_nnatives; i++) {
            const NativeDef* d = &ms_natives[i];
            if (!namecs(cls, clen, d->cls) || !namecs(f->name, f->nlen, d->name)) continue;
            if (any < 0) any = i;
            if (d->np == e->nargs + self && sameCount < 0) sameCount = i;
            if (d->np != e->nargs + self) continue;
            int ok = 1;
            for (int q = 0; q < e->nargs && ok; q++) {
                Type* pt = code_type(c, d->params[q + self]);
                ok = pass == 0 ? (at[q] == pt || (pt->k == T_ANY && at[q]->k != T_VOID) || (at[q]->k == T_NULL && pt->k == T_STRING))
                               : assignable(at[q], pt);
            }
            if (ok) { ni = i; break; }
        }
    const char* shown = self ? cls + 1 : cls; int slen = self ? clen - 1 : clen;
    if (any < 0) cfail(c, e->line, "'%.*s' has no method '%.*s'", slen, shown, f->nlen, f->name);
    if (ni < 0) ni = sameCount >= 0 ? sameCount : any;   /* report the mismatch against the closest overload */
    const NativeDef* nd = &ms_natives[ni];
    Type* params[MS_MAXNATIVEARGS];
    for (int i = self; i < nd->np; i++) params[i - self] = code_type(c, nd->params[i]);
    char what[80]; snprintf(what, sizeof what, "'%.*s.%s'", slen, shown, nd->name);
    args_for(c, e, params, nd->np - self, what);
    c->line = e->line;
    op2(c, OP_CALLN, ni, nd->np);
    return code_type(c, nd->ret);
}

/* list.Add(x) etc.; the list is already on the stack (the VM checks it for null) */
static Type* call_list(C* c, Expr* e, Type* lt) {
    Expr* f = e->a;
    Type* el = lt->elem;
    Type* pInsert[2] = { &tInt, el };
    char what[80]; snprintf(what, sizeof what, "'%s.%.*s'", tname(c, lt), f->nlen, f->name);
    #define IS(n) namecs(f->name, f->nlen, n)
    if (IS("Add")) { args_for(c, e, &el, 1, what); op1(c, OP_LIST, L_ADD); return &tVoid; }
    if (IS("Insert")) { args_for(c, e, pInsert, 2, what); op1(c, OP_LIST, L_INSERT); return &tVoid; }
    if (IS("RemoveAt")) { args_for(c, e, pInsert, 1, what); op1(c, OP_LIST, L_REMOVEAT); return &tVoid; }
    if (IS("Remove")) { args_for(c, e, &el, 1, what); op1(c, OP_LIST, L_REMOVE); return &tBool; }
    if (IS("Clear")) { args_for(c, e, NULL, 0, what); op1(c, OP_LIST, L_CLEAR); return &tVoid; }
    if (IS("AddRange")) {
        if (e->nargs != 1) cfail(c, e->line, "%s takes 1 argument", what);
        Type* st = expr(c, e->args[0]);
        if (!((st->k == T_LIST || st->k == T_ARRAY) && st->elem == el))
            cfail(c, e->line, "%s needs a %s or %s[], not %s", what, tname(c, lt), tname(c, el), tname(c, st));
        c->line = e->line;
        op1(c, OP_LIST, L_ADDRANGE);
        return &tVoid;
    }
    if (IS("IndexOf")) { args_for(c, e, &el, 1, what); op1(c, OP_LIST, L_INDEXOF); return &tInt; }
    /* with a delegate: Find(x => ...), Sort((a, b) => ...) */
    Type* pred = func_of(c, &el, 1, &tBool);
    Type* act = func_of(c, &el, 1, &tVoid);
    Type* two[2] = { el, el };
    Type* cmp = func_of(c, two, 2, &tInt);
    struct { const char* name; int sub; Type** param; Type* ret; } lf[] = {
        { "Find", LF_FIND, &pred, el }, { "FindLast", LF_FINDLAST, &pred, el }, { "FindAll", LF_FINDALL, &pred, lt },
        { "FindIndex", LF_FINDINDEX, &pred, &tInt }, { "FindLastIndex", LF_FINDLASTINDEX, &pred, &tInt },
        { "Exists", LF_EXISTS, &pred, &tBool }, { "TrueForAll", LF_TRUEFORALL, &pred, &tBool },
        { "RemoveAll", LF_REMOVEALL, &pred, &tInt }, { "ForEach", LF_FOREACH, &act, &tVoid },
    };
    for (size_t i = 0; i < sizeof lf / sizeof lf[0]; i++)
        if (IS(lf[i].name)) {
            args_for(c, e, lf[i].param, 1, what);
            op1(c, OP_LISTF, lf[i].sub | default_kind(el) << 8);
            return lf[i].ret;
        }
    if (IS("Sort")) {
        if (e->nargs == 0) {
            if (!isnum(el) && el->k != T_STRING)
                cfail(c, e->line, "%s can only sort numbers, chars and strings by itself; give it a comparison: Sort((a, b) => ...)", what);
            op1(c, OP_LIST, L_SORT);
            return &tVoid;
        }
        args_for(c, e, &cmp, 1, what);
        op1(c, OP_LISTF, LF_SORT);
        return &tVoid;
    }
    if (IS("Reverse")) { args_for(c, e, NULL, 0, what); op1(c, OP_LIST, L_REVERSE); return &tVoid; }
    if (IS("Contains")) {
        args_for(c, e, &el, 1, what);
        op1(c, OP_LIST, L_INDEXOF); op1(c, OP_ICONST, 0); op(c, OP_IGE);
        return &tBool;
    }
    #undef IS
    cfail(c, e->line, "List has no method '%.*s' (there is Add, AddRange, Insert, Remove, RemoveAt, RemoveAll, Clear, Contains, IndexOf, "
          "Find, FindLast, FindAll, FindIndex, FindLastIndex, Exists, TrueForAll, ForEach, Sort, Reverse, Count)", f->nlen, f->name);
}

/* stores the value on top of the stack into an 'out' argument and pops it */
static void store_out(C* c, Expr* o, Type* vt) {
    if (o->k != E_OUT) cfail(c, o->line, "this argument must be 'out var name'");
    if (o->prefix) {
        Type* t = o->tn ? resolve_typenode(c, o->tn, 0) : vt;
        coerce(c, vt, t, o->line, "'out' variable");
        int slot = declare_local(c, o->name, o->nlen, t, o->line);
        if (c->lastBoxed) op(c, OP_BOX);
        op1(c, OP_STORE, slot);
    } else {
        if (o->a->k != E_NAME) cfail(c, o->line, "'out' works with a variable name");
        Res r = resolve(c, o->a->name, o->a->nlen);
        if (r.k == R_CELL || r.k == R_UPVAL) cfail(c, o->line, "'out %.*s': a variable used by a lambda cannot be an 'out' argument", o->a->nlen, o->a->name);
        if (r.k != R_LOCAL && r.k != R_STATIC) cfail(c, o->line, "'out' works with a local variable or a static field");
        coerce(c, vt, r.type, o->line, "'out' argument");
        op1(c, r.k == R_LOCAL ? OP_STORE : OP_SETSTATIC, r.idx);
    }
    emit_pop(c);
}

/* d.Add(k, v) etc.; the dictionary is already on the stack (the VM checks it for null) */
static Type* call_dict(C* c, Expr* e, Type* dt) {
    Expr* f = e->a;
    Type* kv[2] = { dt->key, dt->elem };
    char what[96]; snprintf(what, sizeof what, "'%s.%.*s'", tname(c, dt), f->nlen, f->name);
    #define IS(n) namecs(f->name, f->nlen, n)
    if (IS("Add")) { args_for(c, e, kv, 2, what); op1(c, OP_DICT, D_ADD); return &tVoid; }
    if (IS("Remove")) { args_for(c, e, kv, 1, what); op1(c, OP_DICT, D_REMOVE); return &tBool; }
    if (IS("ContainsKey")) { args_for(c, e, kv, 1, what); op1(c, OP_DICT, D_CONTAINS); return &tBool; }
    if (IS("ContainsValue")) { args_for(c, e, &kv[1], 1, what); op1(c, OP_DICT, D_CONTAINSVAL); return &tBool; }
    if (IS("Clear")) { args_for(c, e, NULL, 0, what); op1(c, OP_DICT, D_CLEAR); return &tVoid; }
    if (IS("TryGetValue")) {
        if (e->nargs != 2) cfail(c, e->line, "%s takes 2 arguments: key and 'out var value'", what);
        expr_to(c, e->args[0], dt->key, e->args[0]->line, "argument 1 of TryGetValue");
        c->line = e->line;
        op1(c, OP_DICT, D_TRYGET | default_kind(dt->elem) << 8);
        store_out(c, e->args[1], dt->elem);
        return &tBool;
    }
    #undef IS
    cfail(c, e->line, "Dictionary has no method '%.*s' (there is Add, Remove, ContainsKey, ContainsValue, TryGetValue, Clear, Count, Keys, Values)", f->nlen, f->name);
}

/* f(args): the delegate is already on the stack */
static Type* invoke(C* c, Expr* e, Type* ft) {
    char what[110]; snprintf(what, sizeof what, "the %s", tname(c, ft));
    args_for(c, e, ft->params, ft->np, what);
    c->line = e->line;
    op2(c, OP_CALLF, ft->np, ft->elem->k != T_VOID);
    return ft->elem;
}

static Type* call(C* c, Expr* e) {
    Expr* f = e->a;
    if (f->k == E_MEMBER && f->prefix) {        /* a?.M(): nothing is called when a is null */
        Type* t = expr(c, f->a);
        c->line = e->line;
        if (!isref(t) || t->k == T_NULL) cfail(c, e->line, "'?.' needs something that can be null, not %s", tname(c, t));
        int j = jump(c, OP_JNULL);
        Expr st = *f; st.k = E_STACK; c->stackType = t;
        Expr mf = *f; mf.prefix = 0; mf.a = &st;
        Expr ce = *e; ce.a = &mf;
        Type* rt = call(c, &ce);
        if (rt->k == T_VOID) c->fn->code[j - 1] = OP_JNULL_POP;
        else if (!isref(rt)) cfail(c, e->line, "'?.%.*s()' gives %s or null, and there is no %s? here; add a value for null: ?.%.*s() ?\? 0",
                                   f->nlen, f->name, tname(c, rt), tname(c, rt), f->nlen, f->name);
        patch(c, j);
        return rt;
    }
    if (f->k == E_NAME) {
        Res rv = resolve(c, f->name, f->nlen);
        if (rv.type && rv.type->k == T_FUNC && rv.k != R_METHOD) { expr(c, f); return invoke(c, e, rv.type); }
        CMethod* m = find_method(c, c->cur, f->name, f->nlen);
        if (!m && (namecs(f->name, f->nlen, "StartCoroutine") || namecs(f->name, f->nlen, "StopCoroutine"))) {
            int start = namecs(f->name, f->nlen, "StartCoroutine");
            Type* pt = &tCoro;
            args_for(c, e, &pt, 1, start ? "'StartCoroutine'" : "'StopCoroutine'");
            c->line = e->line;
            op1(c, OP_CORO, start ? C_START : C_STOP);
            return start ? &tCoro : &tVoid;
        }
        if (!m && namecs(f->name, f->nlen, "StopAllCoroutines")) {
            args_for(c, e, NULL, 0, "'StopAllCoroutines'");
            op1(c, OP_CORO, C_STOPALL);
            return &tVoid;
        }
        if (!m) {
            Res r = resolve(c, f->name, f->nlen);
            if (r.k != R_NONE) cfail(c, e->line, "'%.*s' is not a method", f->nlen, f->name);
            cfail(c, e->line, "unknown method '%.*s'", f->nlen, f->name);
        }
        if (!m->isStatic) {
            if (c->curStatic) cfail(c, e->line, "cannot call instance method '%.*s' from a static method", f->nlen, f->name);
            op1(c, OP_LOAD, 0);
        }
        return call_method(c, e, m, !m->isStatic, 1);
    }
    if (f->k == E_MEMBER) {
        if (f->a->k == E_BASE) {               /* base.Method(): the base class's version, not the override */
            expr(c, f->a);
            int b = c->classes[c->cur].base;
            CMethod* m = find_method(c, b, f->name, f->nlen);
            if (!m && namecs(f->name, f->nlen, "ToString") && e->nargs == 0) { op2(c, OP_TOSTR, 4, 0); return &tString; }
            if (!m) cfail(c, e->line, "base class '%.*s' has no method '%.*s'", c->classes[b].nlen, c->classes[b].name, f->nlen, f->name);
            if (m->isStatic) { op(c, OP_POP); return call_method(c, e, m, 0, 0); }
            return call_method(c, e, m, 1, 0);
        }
        if (f->a->k == E_NAME) {
            Res r = resolve(c, f->a->name, f->a->nlen);
            if (r.k == R_CLASS) {
                CMethod* m = find_method(c, r.idx, f->name, f->nlen);
                CField* df = m ? NULL : find_field(c, r.idx, f->name, f->nlen);
                if (df && df->type->k == T_FUNC) { expr(c, f); return invoke(c, e, df->type); }      /* Game.onWin() */
                if (!m) cfail(c, e->line, "class '%.*s' has no method '%.*s'", f->a->nlen, f->a->name, f->nlen, f->name);
                if (!m->isStatic) cfail(c, e->line, "'%.*s' is not static; call it on an object", f->nlen, f->name);
                return call_method(c, e, m, 0, 0);
            }
            if (r.k == R_BUILTIN) return call_native(c, e, f->a->name, f->a->nlen, 0);
        }
        Type* t = expr(c, f->a);
        c->line = e->line;
        if (namecs(f->name, f->nlen, "ToString") && e->nargs == 0 && t->k != T_VOID) {
            if (t->k == T_STRING) op1(c, OP_NULLCHK, 0);
            tostr(c, t, 0, e->line);
            return &tString;
        }
        if (t->k == T_STRING) {
            op1(c, OP_NULLCHK, 0);
            return call_native(c, e, ".string", 7, 1);
        }
        if (t->k == T_LIST) return call_list(c, e, t);
        if (t->k == T_DICT) return call_dict(c, e, t);
        if (t->k == T_FUNC && namecs(f->name, f->nlen, "Invoke")) return invoke(c, e, t);
        if (t->k == T_CLASS) {
            CMethod* m = find_method(c, t->cls, f->name, f->nlen);
            CField* df = m ? NULL : find_field(c, t->cls, f->name, f->nlen);
            if (df && df->type->k == T_FUNC && !df->isStatic) { op1(c, OP_GETFIELD, df->slot); return invoke(c, e, df->type); }
            if (!m) cfail(c, e->line, "%s has no method '%.*s'", tname(c, t), f->nlen, f->name);
            if (m->isStatic) cfail(c, e->line, "'%.*s' is static; call it as %s.%.*s()", f->nlen, f->name, tname(c, t), f->nlen, f->name);
            if (m->sel < 0) op1(c, OP_NULLCHK, 0);     /* OP_CALLV checks it itself */
            return call_method(c, e, m, 1, 1);
        }
        cfail(c, e->line, "%s has no method '%.*s'", tname(c, t), f->nlen, f->name);
    }
    if (f->k != E_NAME && f->k != E_MEMBER) {                      /* GetAction()(), actions[i](1) */
        Type* t = expr(c, f);
        if (t->k == T_FUNC) return invoke(c, e, t);
    }
    cfail(c, e->line, "this cannot be called like a method");
}

/* ---------- main expression switch ---------- */
static Type* expr(C* c, Expr* e) {
    c->line = e->line;
    switch (e->k) {
        case E_INT: op1(c, OP_ICONST, e->ival); return &tInt;
        case E_FLOAT: emit_fconst(c, e->fval); return &tFloat;
        case E_BOOL: op(c, e->ival ? OP_TRUE : OP_FALSE); return &tBool;
        case E_CHAR: op1(c, OP_ICONST, e->ival); op(c, OP_I2C); return &tChar;
        case E_OUT: cfail(c, e->line, "'out' is only supported in TryGetValue for now");
        case E_PAIR: cfail(c, e->line, "{ key, value } is only allowed in a Dictionary initializer");
        case E_LAMBDA: cfail(c, e->line, "a lambda needs a known type here, e.g. Func<int, int> f = x => x * 2;");
        case E_STACK: return c->stackType;
        case E_COALESCE: {
            Expr* ca = e->a;
            int condCall = ca->k == E_CALL && ca->a->k == E_MEMBER && ca->a->prefix, condMember = ca->k == E_MEMBER && ca->prefix;
            if (condCall || condMember) {       /* a?.b ?? x, a?.F() ?? x: also for int, float... results */
                Expr* recv = condCall ? ca->a->a : ca->a;
                Type* t = expr(c, recv);
                c->line = e->line;
                if (!isref(t) || t->k == T_NULL) cfail(c, e->line, "'?.' needs something that can be null, not %s", tname(c, t));
                int jnull = jump(c, OP_JNULL_POP);
                Expr st = *recv; st.k = E_STACK; c->stackType = t;
                Expr mf = condCall ? *ca->a : *ca; mf.prefix = 0; mf.a = &st;
                Expr ce; Type* rt;
                if (condCall) { ce = *ca; ce.a = &mf; rt = call(c, &ce); } else rt = expr(c, &mf);
                if (rt->k == T_VOID) cfail(c, e->line, "'?\?' needs a value on its left");
                Type* bt = e->b->k == E_LAMBDA ? rt : peek_type(c, e->b);
                Type* T = assignable(bt, rt) ? rt : assignable(rt, bt) ? bt : NULL;
                if (!T) cfail(c, e->line, "'?\?' needs both sides of the same type (%s vs %s)", tname(c, rt), tname(c, bt));
                coerce(c, rt, T, e->line, "'?\?'");
                int jend = jump(c, isref(rt) ? OP_JNNULL : OP_JMP);    /* a null result also takes the right side */
                patch(c, jnull);
                expr_to(c, e->b, T, e->line, "right side of '?\?'");
                patch(c, jend);
                return T;
            }
            Type* lt = peek_type(c, e->a);
            Type* rt = e->b->k == E_LAMBDA ? lt : peek_type(c, e->b);
            if (!isref(lt)) cfail(c, e->line, "the left side of '?\?' must be something that can be null, not %s", tname(c, lt));
            Type* t = lt->k == T_NULL ? rt : assignable(rt, lt) ? lt : assignable(lt, rt) ? rt : NULL;
            if (!t) cfail(c, e->line, "'?\?' needs both sides of the same type (%s vs %s)", tname(c, lt), tname(c, rt));
            expr(c, e->a);
            c->line = e->line;
            int j = jump(c, OP_JNNULL);
            expr_to(c, e->b, t, e->line, "right side of '?\?'");
            patch(c, j);
            return t;
        }
        case E_NULL: op(c, OP_NULL); return &tNull;
        case E_STRING: op1(c, OP_SCONST, add_string(c, e->name, e->nlen)); return &tString;
        case E_THIS:
            if (c->curStatic) cfail(c, e->line, "'this' cannot be used in a static method");
            op1(c, OP_LOAD, 0);
            return c->classes[c->cur].type;
        case E_BASE: {
            if (c->curStatic) cfail(c, e->line, "'base' cannot be used in a static method");
            int b = c->classes[c->cur].base;
            if (b < 0) cfail(c, e->line, "class '%.*s' has no base class", c->classes[c->cur].nlen, c->classes[c->cur].name);
            op1(c, OP_LOAD, 0);
            return c->classes[b].type;
        }
        case E_IS: case E_AS: {
            Type* t = expr(c, e->a);
            c->line = e->line;
            if (e->k == E_IS && !e->tn) {        /* is null / is not null */
                if (!isref(t)) cfail(c, e->line, "%s can never be null", tname(c, t));
                op(c, OP_NULL);
                op(c, e->op ? OP_RNE : OP_REQ);
                return &tBool;
            }
            const char* w = e->k == E_IS ? "is" : "as";
            Type* to = resolve_typenode(c, e->tn, 0);
            if (to->k != T_CLASS) cfail(c, e->line, "'%s' works with classes and interfaces, not %s", w, tname(c, to));
            if (t->k != T_CLASS && t->k != T_NULL) cfail(c, e->line, "'%s' works with objects of classes, not %s", w, tname(c, t));
            if (e->k == E_AS) { op2(c, OP_ISINST, to->cls, 1); return to; }
            if (e->name) {                      /* x is Enemy en: en = x as Enemy; en != null */
                op2(c, OP_ISINST, to->cls, 1);
                int slot = declare_local(c, e->name, e->nlen, to, e->line);
                if (c->lastBoxed) { op(c, OP_DUP); init_local(c, slot, 1); }
                else op1(c, OP_STORE, slot);
                op(c, OP_NULL); op(c, OP_RNE);
                return &tBool;
            }
            op2(c, OP_ISINST, to->cls, 0);
            if (e->op) op(c, OP_NOT);
            return &tBool;
        }

        case E_NAME: {
            Res r = resolve(c, e->name, e->nlen);
            switch (r.k) {
                case R_LOCAL:
                    if (r.type->k == T_KVP) cfail(c, e->line, "use '%.*s.Key' and '%.*s.Value'", e->nlen, e->name, e->nlen, e->name);
                    op1(c, OP_LOAD, r.idx); return r.type;
                case R_CELL: op1(c, OP_LOAD, r.idx); op(c, OP_GETCELL); return r.type;
                case R_UPVAL: op1(c, OP_UPVAL, r.idx); op(c, OP_GETCELL); return r.type;
                case R_STATIC: op1(c, OP_GETSTATIC, r.idx); return r.type;
                case R_FIELD:
                    if (c->curStatic) cfail(c, e->line, "cannot use instance field '%.*s' in a static method", e->nlen, e->name);
                    op1(c, OP_LOAD, 0); op1(c, OP_GETFIELD, r.idx); return r.type;
                case R_METHOD: cfail(c, e->line, "'%.*s' is a method; call it with ()", e->nlen, e->name);
                case R_CLASS: case R_BUILTIN: cfail(c, e->line, "'%.*s' is a class, not a value", e->nlen, e->name);
                default: cfail(c, e->line, "unknown name '%.*s'", e->nlen, e->name);
            }
        }

        case E_MEMBER: {
            if (e->prefix) {                    /* a?.field: null when a is null */
                Type* t = expr(c, e->a);
                c->line = e->line;
                if (!isref(t) || t->k == T_NULL) cfail(c, e->line, "'?.' needs something that can be null, not %s", tname(c, t));
                int j = jump(c, OP_JNULL);
                Expr st = *e; st.k = E_STACK; c->stackType = t;
                Expr m = *e; m.prefix = 0; m.a = &st;
                Type* rt = expr(c, &m);
                if (!isref(rt)) cfail(c, e->line, "'?.%.*s' gives %s or null, and there is no %s? here; add a value for null: ?.%.*s ?\? 0",
                                      e->nlen, e->name, tname(c, rt), tname(c, rt), e->nlen, e->name);
                patch(c, j);
                return rt;
            }
            if (e->a->k == E_NAME) {
                Res r = resolve(c, e->a->name, e->a->nlen);
                if (r.k == R_LOCAL && r.type->k == T_KVP) {     /* foreach pair: key and value live in two slots */
                    int isKey = namecs(e->name, e->nlen, "Key");
                    if (!isKey && !namecs(e->name, e->nlen, "Value")) cfail(c, e->line, "a KeyValuePair has only 'Key' and 'Value'");
                    op1(c, OP_LOAD, r.idx + (isKey ? 0 : 1));
                    return isKey ? r.type->key : r.type->elem;
                }
                if (r.k == R_CLASS) {
                    CField* f = find_field(c, r.idx, e->name, e->nlen);
                    if (!f) cfail(c, e->line, "class '%.*s' has no field '%.*s'", e->a->nlen, e->a->name, e->nlen, e->name);
                    if (!f->isStatic) cfail(c, e->line, "'%.*s' is not static; use an object", e->nlen, e->name);
                    op1(c, OP_GETSTATIC, f->slot);
                    return f->type;
                }
                if (r.k == R_BUILTIN) {
                    for (int i = 0; i < ms_nconsts; i++)
                        if (namecs(e->a->name, e->a->nlen, ms_consts[i].cls) && namecs(e->name, e->nlen, ms_consts[i].name)) {
                            if (ms_consts[i].isFloat) { emit_fconst(c, ms_consts[i].fvalue); return &tFloat; }
                            op1(c, OP_ICONST, ms_consts[i].ivalue);
                            return &tInt;
                        }
                    cfail(c, e->line, "'%.*s' has no member '%.*s'", e->a->nlen, e->a->name, e->nlen, e->name);
                }
            }
            Type* t = expr(c, e->a);
            c->line = e->line;
            if ((t->k == T_STRING || t->k == T_ARRAY) && namecs(e->name, e->nlen, "Length")) {
                op(c, t->k == T_STRING ? OP_STRLEN : OP_ARRLEN);
                return &tInt;
            }
            if ((t->k == T_LIST || t->k == T_DICT) && namecs(e->name, e->nlen, "Count")) { op(c, OP_ARRLEN); return &tInt; }
            if (t->k == T_DICT && namecs(e->name, e->nlen, "Keys")) { op1(c, OP_DICT, D_KEYS); return list_of(c, t->key); }
            if (t->k == T_DICT && namecs(e->name, e->nlen, "Values")) { op1(c, OP_DICT, D_VALUES); return list_of(c, t->elem); }
            if (t->k == T_LIST && namecs(e->name, e->nlen, "Length")) cfail(c, e->line, "a List has 'Count', not 'Length'");
            if (t->k == T_ARRAY && namecs(e->name, e->nlen, "Count")) cfail(c, e->line, "an array has 'Length', not 'Count'");
            if (t->k == T_CLASS) {
                CField* f = find_field(c, t->cls, e->name, e->nlen);
                if (!f) {
                    if (find_method(c, t->cls, e->name, e->nlen)) cfail(c, e->line, "'%.*s' is a method; call it with ()", e->nlen, e->name);
                    cfail(c, e->line, "%s has no field '%.*s'", tname(c, t), e->nlen, e->name);
                }
                if (f->isStatic) cfail(c, e->line, "'%.*s' is static; use %s.%.*s", e->nlen, e->name, tname(c, t), e->nlen, e->name);
                op1(c, OP_GETFIELD, f->slot);
                return f->type;
            }
            cfail(c, e->line, "%s has no member '%.*s'", tname(c, t), e->nlen, e->name);
        }

        case E_INDEX: {
            Type* t = expr(c, e->a);
            if (t->k == T_DICT) {
                expr_to(c, e->b, t->key, e->line, "dictionary key");
                c->line = e->line;
                op(c, OP_GETELEM);
                return t->elem;
            }
            if (t->k != T_ARRAY && t->k != T_LIST && t->k != T_STRING) cfail(c, e->line, "only arrays, lists, dictionaries and strings can be indexed, not %s", tname(c, t));
            Type* it = expr(c, e->b);
            if (!isint(it)) cfail(c, e->line, "index must be int, not %s", tname(c, it));
            c->line = e->line;
            op(c, OP_GETELEM);
            return t->k == T_STRING ? &tChar : t->elem;
        }

        case E_CALL: return call(c, e);

        case E_NEW: {
            if (e->tn->base != TK_IDENT) cfail(c, e->line, "cannot use 'new' with a built-in type like that");
            if (namecs(e->tn->name, e->tn->nlen, "WaitForSeconds") || namecs(e->tn->name, e->tn->nlen, "WaitForSecondsRealtime") ||
                namecs(e->tn->name, e->tn->nlen, "WaitForEndOfFrame"))
                cfail(c, e->line, "'new %.*s(...)' can only be used in 'yield return'", e->tn->nlen, e->tn->name);
            int ci = find_class(c, e->tn->name, e->tn->nlen);
            if (ci < 0) {
                if (is_builtin_class(e->tn->name, e->tn->nlen)) cfail(c, e->line, "'%.*s' is built-in and cannot be created", e->tn->nlen, e->tn->name);
                cfail(c, e->line, "unknown class '%.*s'", e->tn->nlen, e->tn->name);
            }
            CClass* k = &c->classes[ci];
            if (k->isInterface) cfail(c, e->line, "'%.*s' is an interface; create an object of a class that implements it", k->nlen, k->name);
            if (k->isAbstract) cfail(c, e->line, "'%.*s' is abstract; create an object of a class derived from it", k->nlen, k->name);
            op1(c, OP_NEW, ci);
            char what[80]; snprintf(what, sizeof what, "constructor of '%.*s'", k->nlen, k->name);
            args_for(c, e, k->ctorParams, k->ctorNp, what);
            c->line = e->line;
            op2(c, OP_CALL, k->ctorFunc, k->ctorNp + 1);
            return k->type;
        }

        case E_NEWLIST: {
            Type* lt = resolve_typenode(c, e->tn, 0);
            Type* ct = e->a ? peek_type(c, e->a) : NULL;
            if (ct && isint(ct)) { expr(c, e->a); op(c, OP_POP); ct = NULL; }   /* the capacity is only a hint */
            c->line = e->line;
            if (ct) {                           /* new List<T>(otherListOrArray): a copy */
                if (lt->k != T_LIST || !((ct->k == T_LIST || ct->k == T_ARRAY) && ct->elem == lt->elem))
                    cfail(c, e->line, "'new %s(...)' takes a capacity (int) or a %s / %s[] to copy, not %s",
                          tname(c, lt), tname(c, lt), tname(c, lt->elem), tname(c, ct));
                op1(c, OP_LIST, L_NEW);
                op(c, OP_DUP);
                expr(c, e->a);
                c->line = e->line;
                op1(c, OP_LIST, L_ADDRANGE);
            }
            if (lt->k == T_DICT) {
                op1(c, OP_DICT, D_NEW);
                for (int i = 0; i < e->nargs; i++) {
                    Expr* it = e->args[i];
                    if (it->k != E_PAIR) cfail(c, it->line, "dictionary items are written { key, value } or [key] = value");
                    op(c, OP_DUP);
                    expr_to(c, it->a, lt->key, it->line, "dictionary key");
                    expr_to(c, it->b, lt->elem, it->line, "dictionary value");
                    c->line = it->line;
                    if (it->prefix) { op(c, OP_SETELEM); op(c, OP_POP); }   /* [k] = v overwrites */
                    else op1(c, OP_DICT, D_ADD);                             /* {k, v} is Add: duplicates fail */
                }
                return lt;
            }
            if (lt->k != T_LIST) cfail(c, e->line, "'new %s' needs '()'", tname(c, lt));
            if (!ct) op1(c, OP_LIST, L_NEW);
            for (int i = 0; i < e->nargs; i++) {
                if (e->args[i]->k == E_PAIR) cfail(c, e->args[i]->line, "a List item is a single value, not { a, b }");
                op(c, OP_DUP);
                expr_to(c, e->args[i], lt->elem, e->args[i]->line, "list element");
                op1(c, OP_LIST, L_ADD);
            }
            return lt;
        }

        case E_NEWARR: {
            Type* elem = resolve_typenode(c, e->tn, 0);
            if (elem->k == T_VOID) cfail(c, e->line, "cannot make an array of void");
            if (!e->prefix) {
                Type* st = expr(c, e->a);
                if (st->k != T_INT) cfail(c, e->line, "array size must be int, not %s", tname(c, st));
                c->line = e->line;
                op1(c, OP_NEWARR, default_kind(elem));
            } else {
                op1(c, OP_ICONST, e->nargs);
                op1(c, OP_NEWARR, default_kind(elem));
                for (int i = 0; i < e->nargs; i++) {
                    op(c, OP_DUP);
                    op1(c, OP_ICONST, i);
                    expr_to(c, e->args[i], elem, e->args[i]->line, "array element");
                    op(c, OP_SETELEM);
                    op(c, OP_POP);
                }
            }
            return array_of(c, elem);
        }

        case E_UNARY: {
            Type* t = expr(c, e->a);
            c->line = e->line;
            if (e->op == TK_MINUS) {
                if (isint(t)) { if (t->k == T_CHAR) op1(c, OP_C2I, 0); op(c, OP_INEG); return &tInt; }
                if (t->k == T_FLOAT) { op(c, OP_FNEG); return t; }
                cfail(c, e->line, "operator '-' cannot be used with %s", tname(c, t));
            }
            if (e->op == TK_TILDE) {
                if (!isint(t)) cfail(c, e->line, "operator '~' cannot be used with %s", tname(c, t));
                if (t->k == T_CHAR) op1(c, OP_C2I, 0);
                op(c, OP_INOT);
                return &tInt;
            }
            need_bool(c, t, e->line, "operand of '!'");
            op(c, OP_NOT);
            return &tBool;
        }

        case E_CAST: {
            Type* t = expr(c, e->a);
            c->line = e->line;
            if (e->tn) {                        /* (Enemy)x */
                Type* to = resolve_typenode(c, e->tn, 0);
                if (to->k != T_CLASS) cfail(c, e->line, "cannot cast to %s this way", tname(c, to));
                if (t->k == T_NULL || assignable(t, to)) return to;          /* up-cast: nothing to check */
                if (t->k != T_CLASS) cfail(c, e->line, "cannot convert %s to %s", tname(c, t), tname(c, to));
                if (!to->isa[t->cls] && !c->classes[t->cls].isInterface && !c->classes[to->cls].isInterface)
                    cfail(c, e->line, "cannot convert %s to %s (neither derives from the other)", tname(c, t), tname(c, to));
                op2(c, OP_ISINST, to->cls, 2);  /* fails at run time if the object is not one */
                return to;
            }
            if (!isnum(t)) cfail(c, e->line, "cannot cast %s to a number", tname(c, t));
            if (e->op == TK_KINT) { if (t->k == T_FLOAT) op(c, OP_F2I); else if (t->k == T_CHAR) op1(c, OP_C2I, 0); return &tInt; }
            if (e->op == TK_KCHAR) {
                if (t->k == T_FLOAT) op(c, OP_F2I);
                if (t->k != T_CHAR) op(c, OP_I2C);
                return &tChar;
            }
            if (t->k != T_FLOAT) op1(c, OP_I2F, 0);
            return &tFloat;
        }

        case E_BINARY: {
            Type* lt = expr(c, e->a);
            Type* rt = expr(c, e->b);
            c->line = e->line;
            switch (e->op) {
                case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PERCENT:
                    return arith(c, e->op, lt, rt, e->line);
                case TK_AMP: case TK_PIPE: case TK_CARET: case TK_SHL: case TK_SHR:
                    return bitwise(c, e->op, lt, rt, e->line);
                default:
                    return compare(c, e->op, lt, rt, e->line);
            }
        }

        case E_LOGIC: {
            Type* lt = expr(c, e->a);
            need_bool(c, lt, e->line, e->op == TK_AND ? "left side of '&&'" : "left side of '||'");
            c->line = e->line;
            int j = jump(c, e->op == TK_AND ? OP_JFALSE_KEEP : OP_JTRUE_KEEP);
            Type* rt = expr(c, e->b);
            need_bool(c, rt, e->line, e->op == TK_AND ? "right side of '&&'" : "right side of '||'");
            patch(c, j);
            return &tBool;
        }

        case E_COND: {
            Type* ct = expr(c, e->a);
            need_bool(c, ct, e->line, "condition of '?:'");
            Type* tb = peek_type(c, e->b);
            Type* tc = peek_type(c, e->c);
            Type* t;
            if (tb == tc) t = tb;
            else if (isnum(tb) && isnum(tc)) t = (tb->k == T_FLOAT || tc->k == T_FLOAT) ? &tFloat : &tInt;
            else if (tb->k == T_NULL && isref(tc)) t = tc;
            else if (tc->k == T_NULL && isref(tb)) t = tb;
            else if (tb->k == T_CLASS && assignable(tc, tb)) t = tb;
            else if (tc->k == T_CLASS && assignable(tb, tc)) t = tc;
            else cfail(c, e->line, "both sides of '?:' must have the same type (%s vs %s)", tname(c, tb), tname(c, tc));
            if (t->k == T_VOID) cfail(c, e->line, "'?:' branches cannot be void");
            c->line = e->line;
            int jz = jump(c, OP_JZ);
            expr_to(c, e->b, t, e->line, "'?:'");
            int je = jump(c, OP_JMP);
            patch(c, jz);
            expr_to(c, e->c, t, e->line, "'?:'");
            patch(c, je);
            return t;
        }

        case E_ASSIGN: {
            LV lv = lvalue(c, e->a);
            if (e->op == TK_ASSIGN) {
                expr_to(c, e->b, lv.type, e->line, "assignment");
                c->line = e->line;
                lv_store(c, &lv);
                return lv.type;
            }
            if (lv.type->k == T_FUNC && (e->op == TK_PLUSEQ || e->op == TK_MINUSEQ)) {    /* onHit += Handler; */
                lv_dup(c, &lv);
                lv_load(c, &lv);
                expr_to(c, e->b, lv.type, e->line, e->op == TK_PLUSEQ ? "'+='" : "'-='");
                c->line = e->line;
                op1(c, OP_DELEG, e->op == TK_MINUSEQ);
                lv_store(c, &lv);
                return lv.type;
            }
            int tok = e->op == TK_PLUSEQ ? TK_PLUS : e->op == TK_MINUSEQ ? TK_MINUS :
                      e->op == TK_STAREQ ? TK_STAR : e->op == TK_SLASHEQ ? TK_SLASH :
                      e->op == TK_AMPEQ ? TK_AMP : e->op == TK_PIPEEQ ? TK_PIPE : e->op == TK_CARETEQ ? TK_CARET :
                      e->op == TK_SHLEQ ? TK_SHL : e->op == TK_SHREQ ? TK_SHR : TK_PERCENT;
            lv_dup(c, &lv);
            lv_load(c, &lv);
            Type* rt = expr(c, e->b);
            c->line = e->line;
            Type* res = is_bitwise(tok) ? bitwise(c, tok, lv.type, rt, e->line) : arith(c, tok, lv.type, rt, e->line);
            if (lv.type->k == T_CHAR && res->k == T_INT) op(c, OP_I2C);     /* c += 1 stays a char, like C# */
            else coerce(c, res, lv.type, e->line, "compound assignment");
            lv_store(c, &lv);
            return lv.type;
        }

        case E_INCDEC: {
            LV lv = lvalue(c, e->a);
            if (!isnum(lv.type)) cfail(c, e->line, "'%s' needs an int or float, not %s", e->op == TK_INC ? "++" : "--", tname(c, lv.type));
            c->line = e->line;
            lv_dup(c, &lv);
            lv_load(c, &lv);
            int tmp = -1;
            if (!e->prefix) { tmp = alloc_temp(c); op1(c, OP_STORE, tmp); }
            if (isint(lv.type)) { op1(c, OP_ICONST, 1); op(c, e->op == TK_INC ? OP_IADD : OP_ISUB); }   /* a char stays a char */
            else { emit_fconst(c, 1.0f); op(c, e->op == TK_INC ? OP_FADD : OP_FSUB); }
            lv_store(c, &lv);
            if (!e->prefix) { emit_pop(c); op1(c, OP_LOAD, tmp); c->nextSlot--; }
            return lv.type;
        }
    }
    cfail(c, e->line, "internal error: unknown expression");
}

/* ================= lambdas ================= */
static void stmt(C* c, Stmt* s);
static void expr_stmt(C* c, Expr* e);

/* Before a method is compiled: which names do its lambdas use from outside themselves? Locals with
 * those names are boxed (kept in cells), so the method and the lambdas share them. It goes by name,
 * so it may box a variable that did not need it; that only costs a little speed. */
static void nl_add(C* c, NameList* L, const char* n, int l) {
    for (int i = 0; i < L->n; i++) if (nameeq(L->v[i].n, L->v[i].l, n, l)) return;
    L->v = (Name*)arena_grow(&c->A, L->v, L->n, &L->cap, sizeof(Name));
    L->v[L->n].n = n; L->v[L->n].l = l; L->n++;
}
static int nl_has(NameList* L, const char* n, int l) {
    for (int i = 0; i < L->n; i++) if (nameeq(L->v[i].n, L->v[i].l, n, l)) return 1;
    return 0;
}
static void scan_stmt(C* c, Stmt* s, NameList* uses, NameList* decls);
static void scan_expr(C* c, Expr* e, NameList* uses, NameList* decls) {
    if (!e) return;
    if (e->k == E_LAMBDA) {
        NameList u = { 0 }, d = { 0 };
        if (e->body) scan_stmt(c, e->body, &u, &d); else scan_expr(c, e->a, &u, &d);
        for (int i = 0; i < u.n; i++) {
            int own = nl_has(&d, u.v[i].n, u.v[i].l);
            for (int q = 0; q < e->nparams && !own; q++) own = nameeq(e->params[q].name, e->params[q].nlen, u.v[i].n, u.v[i].l);
            if (!own) nl_add(c, &c->caps, u.v[i].n, u.v[i].l);
            nl_add(c, uses, u.v[i].n, u.v[i].l);
        }
        return;
    }
    if (e->k == E_NAME) nl_add(c, uses, e->name, e->nlen);
    if ((e->k == E_OUT && e->prefix) || (e->k == E_IS && e->name)) nl_add(c, decls, e->name, e->nlen);
    scan_expr(c, e->a, uses, decls); scan_expr(c, e->b, uses, decls); scan_expr(c, e->c, uses, decls);
    for (int i = 0; i < e->nargs; i++) scan_expr(c, e->args[i], uses, decls);
}
static void scan_stmt(C* c, Stmt* s, NameList* uses, NameList* decls) {
    if (!s) return;
    if (s->k == S_VAR) {
        for (; s; s = s->els) { nl_add(c, decls, s->name, s->nlen); scan_expr(c, s->e, uses, decls); }
        return;
    }
    if (s->k == S_FOREACH) nl_add(c, decls, s->name, s->nlen);
    scan_expr(c, s->e, uses, decls); scan_expr(c, s->step, uses, decls);
    for (int i = 0; i < s->nlabels; i++) scan_expr(c, s->labels[i], uses, decls);
    for (int i = 0; i < s->n; i++) scan_stmt(c, s->list[i], uses, decls);
    scan_stmt(c, s->body, uses, decls); scan_stmt(c, s->els, uses, decls); scan_stmt(c, s->init, uses, decls);
}
static void scan_captures(C* c, Stmt* body, Expr* extra) {
    NameList u = { 0 }, d = { 0 };
    c->caps.n = 0;
    scan_stmt(c, body, &u, &d);
    scan_expr(c, extra, &u, &d);
}

static void emit_closure(C* c, int kind, int idx, int nup) { op(c, OP_CLOSURE); emit1(c, kind); emit1(c, idx); emit1(c, nup); }

/* Func<int, int> f = Square;  Action a = enemy.Die;  -> a delegate of that method. 0 = e is not a method name */
static int method_group(C* c, Expr* e, Type* ft, int line) {
    CMethod* m = NULL;
    int self = 0;                               /* 0 static method, 1 this, 2 the object of e->a */
    if (e->k == E_NAME) {
        if (resolve(c, e->name, e->nlen).k != R_METHOD) return 0;
        m = find_method(c, c->cur, e->name, e->nlen);
        if (!m->isStatic) {
            if (c->curStatic) cfail(c, line, "cannot use instance method '%.*s' in a static method", e->nlen, e->name);
            self = 1;
        }
    } else if (e->k == E_MEMBER) {
        if (e->a->k == E_BASE) return 0;
        if (e->a->k == E_NAME) {
            Res r = resolve(c, e->a->name, e->a->nlen);
            if (r.k == R_CLASS) {
                if (find_field(c, r.idx, e->name, e->nlen)) return 0;
                m = find_method(c, r.idx, e->name, e->nlen);
                if (!m) return 0;
                if (!m->isStatic) cfail(c, line, "'%.*s' is not static; use an object: obj.%.*s", e->nlen, e->name, e->nlen, e->name);
            }
            if (r.k == R_BUILTIN) cfail(c, line, "built-in methods like '%.*s.%.*s' cannot be used as a delegate yet; wrap it in a lambda",
                                       e->a->nlen, e->a->name, e->nlen, e->name);
        }
        if (!m) {
            Type* t = peek_type(c, e->a);
            if (t->k != T_CLASS || find_field(c, t->cls, e->name, e->nlen)) return 0;
            m = find_method(c, t->cls, e->name, e->nlen);
            if (!m) return 0;
            if (m->isStatic) cfail(c, line, "'%.*s' is static; use %s.%.*s", e->nlen, e->name, tname(c, t), e->nlen, e->name);
            self = 2;
        }
    } else return 0;
    int ok = m->np == ft->np && m->ret == ft->elem;
    for (int i = 0; ok && i < m->np; i++) ok = m->params[i] == ft->params[i];
    if (!ok) cfail(c, line, "method '%.*s' does not fit %s (it needs the same parameter types and return type)", m->nlen, m->name, tname(c, ft));
    c->line = line;
    if (!self) { op(c, OP_NULL); emit_closure(c, 1, m->func, 0); return 1; }
    if (self == 1) op1(c, OP_LOAD, 0);
    else { expr(c, e->a); c->line = line; op1(c, OP_NULLCHK, 0); }
    if (m->sel >= 0) emit_closure(c, 2, m->sel, 0);
    else emit_closure(c, 0, m->func, 0);
    return 1;
}

/* A lambda becomes a function of its own: slot 0 = the object of the method around it (or null),
 * then the parameters. Variables from outside are reached through the cells the delegate captured. */
static void compile_lambda(C* c, Expr* e, Type* ft) {
    Program* P = c->P;
    if (e->nparams != ft->np)
        cfail(c, e->line, "this lambda has %d parameter%s, but %s needs %d", e->nparams, e->nparams == 1 ? "" : "s", tname(c, ft), ft->np);
    if (P->nfuncs >= P->capfuncs) cfail(c, e->line, "internal error: too many lambdas");
    int fi = P->nfuncs++;
    /* the function around it, put back at the end */
    Func* sfn = c->fn; int sCtor = c->isCtor, sIter = c->isIter, sOp = c->lastOp, sLabel = c->lastLabel, sSlot = c->nextSlot, sLine = c->line;
    Type* sRet = c->retType; Loop* sLoop = c->loop;
    int baseLocals = c->nlocals;
    LamCtx lc; lc.depth = c->fdepth + 1; lc.nup = 0; lc.parent = c->lam;
    c->lam = &lc; c->fdepth++;
    c->fn = &P->funcs[fi];
    snprintf(c->fn->name, sizeof c->fn->name, "%.50s.lambda", sfn->name);
    c->fn->nparams = ft->np + 1; c->fn->nlocals = 1;
    c->nextSlot = 1; c->loop = NULL; c->lastOp = -1; c->lastLabel = -1;
    c->isCtor = 0; c->isIter = 0; c->retType = ft->elem;
    for (int i = 0; i < e->nparams; i++) {
        Param* pa = &e->params[i];
        if (pa->tn && resolve_typenode(c, pa->tn, 0) != ft->params[i])
            cfail(c, e->line, "parameter '%.*s' of this lambda must be %s", pa->nlen, pa->name, tname(c, ft->params[i]));
        if (find_local(c, pa->name, pa->nlen) >= 0)
            cfail(c, e->line, "a variable named '%.*s' already exists here; give the lambda parameter another name", pa->nlen, pa->name);
        declare_local(c, pa->name, pa->nlen, ft->params[i], e->line);
    }
    box_params(c);
    if (e->body) {
        stmt(c, e->body);
        c->line = e->line;
        op(c, ft->elem->k == T_VOID ? OP_RET : OP_NORET);
    } else if (ft->elem->k == T_VOID) {
        expr_stmt(c, e->a);
        op(c, OP_RET);
    } else {
        expr_to(c, e->a, ft->elem, e->a->line, "the lambda's result");
        op(c, OP_RETV);
    }
    c->nlocals = baseLocals; c->fdepth--; c->lam = lc.parent;
    c->fn = sfn; c->isCtor = sCtor; c->isIter = sIter; c->lastOp = sOp; c->lastLabel = sLabel; c->nextSlot = sSlot;
    c->retType = sRet; c->loop = sLoop; c->line = sLine;
    /* in the function around it: self, the captured cells, then the delegate */
    if (c->curStatic) op(c, OP_NULL); else op1(c, OP_LOAD, 0);
    for (int k = 0; k < lc.nup; k++) {
        if (lc.ups[k].from >= 0) op1(c, OP_UPVAL, lc.ups[k].from);
        else op1(c, OP_LOAD, c->locals[lc.ups[k].li].slot);
    }
    emit_closure(c, 0, fi, lc.nup);
}

/* an expression converted to type t; lambdas and method names need t to know what they are */
static void expr_to(C* c, Expr* e, Type* t, int line, const char* what) {
    if (t->k == T_FUNC && e->k == E_LAMBDA) { compile_lambda(c, e, t); return; }
    if (t->k == T_FUNC && method_group(c, e, t, line)) return;
    if (e->k == E_LAMBDA) cfail(c, line, "%s: a lambda cannot become %s", what, tname(c, t));
    if (e->k == E_COND && (t->k == T_CLASS || t->k == T_FUNC)) {   /* c ? new Bat() : new Rock() given to a Wrog, as in C# 9 */
        need_bool(c, expr(c, e->a), e->line, "condition of '?:'");
        c->line = e->line;
        int jz = jump(c, OP_JZ);
        expr_to(c, e->b, t, line, what);
        int je = jump(c, OP_JMP);
        patch(c, jz);
        expr_to(c, e->c, t, line, what);
        patch(c, je);
        return;
    }
    coerce(c, expr(c, e), t, line, what);
}

/* ================= statements ================= */
static void stmt(C* c, Stmt* s);


static void cond_expr(C* c, Expr* e, const char* what) {
    Type* t = expr(c, e);
    need_bool(c, t, e->line, what);
}

static void expr_stmt(C* c, Expr* e) {
    if (e->k == E_INCDEC) e->prefix = 1;       /* value unused: x++ behaves like ++x */
    if (e->k != E_ASSIGN && e->k != E_INCDEC && e->k != E_CALL && e->k != E_NEW)
        cfail(c, e->line, "only assignments, calls, ++/-- and 'new' can be used as a statement");
    Type* t = expr(c, e);
    if (t->k != T_VOID) emit_pop(c);
}

/* REPL: 'var x = 5;' at the top of the input makes a static field that the next inputs still see.
 * Declaring it again with the same type just assigns it. */
static void repl_var(C* c, Stmt* s) {
    for (; s; s = s->els) {
        c->line = s->line;
        CField* f = find_field(c, c->cur, s->name, s->nlen);
        Local* rv = NULL;
        for (int i = 0; i < c->nreplVars; i++)
            if (nameeq(c->replVars[i].name, c->replVars[i].nlen, s->name, s->nlen)) rv = &c->replVars[i];
        Type* old = f ? f->type : rv ? rv->type : NULL;
        Type* t;
        if (s->tn) {
            t = resolve_typenode(c, s->tn, 0);
            if (old && old != t) cfail(c, s->line, "'%.*s' is already a %s", s->nlen, s->name, tname(c, old));
            if (s->e) expr_to(c, s->e, t, s->line, "variable initializer");
            else push_default(c, t);
        } else {
            t = expr(c, s->e);
            if (t->k == T_VOID) cfail(c, s->line, "cannot assign a void value to a variable");
            if (t->k == T_NULL) cfail(c, s->line, "cannot use 'var' with null; write the type");
            if (old) { coerce(c, t, old, s->line, "assignment"); t = old; }
        }
        c->line = s->line;
        int slot;
        if (f) slot = f->slot;
        else if (rv) slot = rv->slot;
        else {
            if (c->nreplVars >= 64) cfail(c, s->line, "too many variables in one line");
            int used = (int)strlen(c->replDecls);
            int n = snprintf(c->replDecls + used, c->replDeclsCap - used, "static %s %.*s; ", tname(c, t), s->nlen, s->name);
            if (n >= c->replDeclsCap - used) cfail(c, s->line, "too many variables (restart the terminal)");
            Program* P = c->P;
            Value* d = (Value*)realloc(P->staticDefaults, sizeof(Value) * (P->nstatics + 1));
            if (!d) cfail(c, s->line, "out of memory");
            P->staticDefaults = d;
            slot = P->nstatics++;
            d[slot] = default_value(t);
            Local* lo = &c->replVars[c->nreplVars++];
            lo->name = s->name; lo->nlen = s->nlen; lo->type = t; lo->slot = slot;
        }
        op1(c, OP_SETSTATIC, slot);
        emit_pop(c);
    }
}

/* REPL: an expression on its own is printed, like in C# Interactive */
static void repl_expr(C* c, Expr* e) {
    if (e->k == E_ASSIGN || e->k == E_INCDEC) { expr_stmt(c, e); return; }
    Type* t = expr(c, e);
    if (t->k == T_CLASS) tostr(c, t, 0, e->line);
    if (t->k != T_VOID) op2(c, OP_CALLN, c->replPrint, 1);
}

static void push_loop(C* c, Loop* l, int contTarget) { memset(l, 0, sizeof *l); l->contTarget = contTarget; l->prev = c->loop; c->loop = l; }
static void pop_loop(C* c) {
    Loop* l = c->loop;
    for (int i = 0; i < l->nb; i++) patch(c, l->breaks[i]);
    c->loop = l->prev;
}

static void stmt(C* c, Stmt* s) {
    c->line = s->line;
    switch (s->k) {
        case S_BLOCK: {
            int nl = c->nlocals, ns = c->nextSlot;
            for (int i = 0; i < s->n; i++) {
                Stmt* t = s->list[i];
                if (s == c->replBody && t->k == S_VAR) repl_var(c, t);
                else if (s == c->replBody && t->k == S_EXPR) { c->line = t->line; repl_expr(c, t->e); }
                else stmt(c, t);
            }
            c->nlocals = nl; c->nextSlot = ns;
            return;
        }
        case S_VAR: {
          for (; s; s = s->els) {
            c->line = s->line;
            Type* t;
            if (s->tn) {
                t = resolve_typenode(c, s->tn, 0);
                if (s->e) expr_to(c, s->e, t, s->line, "variable initializer");
                else push_default(c, t);
            } else {
                t = expr(c, s->e);
                if (t->k == T_VOID) cfail(c, s->line, "cannot assign a void value to a variable");
                if (t->k == T_NULL) cfail(c, s->line, "cannot use 'var' with null; write the type");
            }
            c->line = s->line;
            int slot = declare_local(c, s->name, s->nlen, t, s->line);
            init_local(c, slot, c->lastBoxed);
          }
          return;
        }
        case S_EXPR: expr_stmt(c, s->e); return;
        case S_IF: {
            cond_expr(c, s->e, "'if' condition");
            int jz = jump(c, OP_JZ);
            stmt(c, s->body);
            if (s->els) {
                int je = jump(c, OP_JMP);
                patch(c, jz);
                stmt(c, s->els);
                patch(c, je);
            } else patch(c, jz);
            return;
        }
        case S_WHILE: {
            int start = label(c);
            cond_expr(c, s->e, "'while' condition");
            int jz = jump(c, OP_JZ);
            Loop l; push_loop(c, &l, start);
            stmt(c, s->body);
            jump_back(c, OP_JMP, start);
            patch(c, jz);
            pop_loop(c);
            return;
        }
        case S_DO: {
            int start = label(c);
            Loop l; push_loop(c, &l, -1);
            stmt(c, s->body);
            for (int i = 0; i < l.nc; i++) patch(c, l.conts[i]);
            cond_expr(c, s->e, "'while' condition");
            op(c, OP_NOT);
            jump_back(c, OP_JZ, start);
            pop_loop(c);
            return;
        }
        case S_FOR: {
            int nl = c->nlocals, ns = c->nextSlot;
            if (s->init) {
                if (s->init->k == S_EXPR) expr_stmt(c, s->init->e);
                else stmt(c, s->init);
            }
            int start = label(c);
            int jz = -1;
            if (s->e) { cond_expr(c, s->e, "'for' condition"); jz = jump(c, OP_JZ); }
            Loop l; push_loop(c, &l, -1);
            stmt(c, s->body);
            for (int i = 0; i < l.nc; i++) patch(c, l.conts[i]);
            if (s->step) expr_stmt(c, s->step);
            jump_back(c, OP_JMP, start);
            if (jz >= 0) patch(c, jz);
            pop_loop(c);
            c->nlocals = nl; c->nextSlot = ns;
            return;
        }
        case S_YIELDBREAK:
            if (!c->isIter) cfail(c, s->line, "'yield' can only be used in a method that returns IEnumerator");
            op(c, OP_RET);
            return;
        case S_YIELD: {
            if (!c->isIter) cfail(c, s->line, "'yield' can only be used in a method that returns IEnumerator");
            Expr* y = s->e;
            if (y->k == E_NULL || y->k == E_INT) { op1(c, OP_YIELD, Y_NEXT); return; }     /* yield return null / 0: next frame */
            if (y->k == E_NEW && y->tn->base == TK_IDENT) {
                if (namecs(y->tn->name, y->tn->nlen, "WaitForSeconds") || namecs(y->tn->name, y->tn->nlen, "WaitForSecondsRealtime")) {
                    Type* pf = &tFloat;
                    args_for(c, y, &pf, 1, "'WaitForSeconds'");
                    c->line = s->line;
                    op1(c, OP_YIELD, Y_SECONDS);
                    return;
                }
                if (namecs(y->tn->name, y->tn->nlen, "WaitForEndOfFrame")) {
                    args_for(c, y, NULL, 0, "'WaitForEndOfFrame'");
                    op1(c, OP_YIELD, Y_NEXT);
                    return;
                }
            }
            Type* t = expr(c, y);
            c->line = s->line;
            if (t->k != T_CORO) cfail(c, s->line, "'yield return' takes null, new WaitForSeconds(s) or a coroutine, not %s", tname(c, t));
            op1(c, OP_YIELD, Y_CORO);       /* wait until that coroutine ends (it is started if needed) */
            return;
        }
        case S_RETURN:
            if (c->isIter) cfail(c, s->line, "a coroutine cannot use 'return'; use 'yield break' to stop it");
            if (c->isCtor) {
                if (s->e) cfail(c, s->line, "a constructor cannot return a value");
                op1(c, OP_LOAD, 0); op(c, OP_RETV);
            } else if (c->retType->k == T_VOID) {
                if (s->e) cfail(c, s->line, "a void method cannot return a value");
                op(c, OP_RET);
            } else {
                if (!s->e) cfail(c, s->line, "this method must return a %s", tname(c, c->retType));
                expr_to(c, s->e, c->retType, s->line, "return value");
                op(c, OP_RETV);
            }
            return;
        case S_BREAK:
            if (!c->loop) cfail(c, s->line, "'break' outside of a loop");
            if (c->loop->nb >= 128) cfail(c, s->line, "too many 'break' statements in one loop");
            c->loop->breaks[c->loop->nb++] = jump(c, OP_JMP);
            return;
        case S_CONTINUE: {
            Loop* l = c->loop;
            while (l && l->isSwitch) l = l->prev;
            if (!l) cfail(c, s->line, "'continue' outside of a loop");
            if (l->contTarget >= 0) jump_back(c, OP_JMP, l->contTarget);
            else {
                if (l->nc >= 128) cfail(c, s->line, "too many 'continue' statements in one loop");
                l->conts[l->nc++] = jump(c, OP_JMP);
            }
            return;
        }
        case S_FOREACH: {
            /* for (int i = 0; i < coll.Length; i++) { T x = coll[i]; body }  over a copy of the reference */
            int nl = c->nlocals, ns = c->nextSlot;
            Type* ct = expr(c, s->e);
            c->line = s->line;
            if (ct->k == T_DICT) {
                /* for (i = next(0); i >= 0; i = next(i + 1)) { kv = entry i } ; kv takes two slots: key, value */
                if (s->tn && resolve_typenode(c, s->tn, 0) != ct->kvp)
                    cfail(c, s->line, "'foreach' over %s gives %s; write 'var'", tname(c, ct), tname(c, ct->kvp));
                int coll = alloc_temp(c), idx = alloc_temp(c);
                op1(c, OP_STORE, coll); emit_pop(c);
                op1(c, OP_ICONST, 0); op1(c, OP_STORE, idx); emit_pop(c);
                int kslot = declare_local(c, s->name, s->nlen, ct->kvp, s->line);
                c->locals[c->nlocals - 1].boxed = 0;          /* two slots; a lambda cannot use it */
                alloc_temp(c);                                  /* kslot + 1: the value */
                int start = label(c);
                op1(c, OP_LOAD, coll); op1(c, OP_LOAD, idx); op1(c, OP_DICT, D_NEXT);
                op1(c, OP_STORE, idx); op1(c, OP_ICONST, 0); op(c, OP_IGE);
                int jz = jump(c, OP_JZ);
                op1(c, OP_LOAD, coll); op1(c, OP_LOAD, idx); op1(c, OP_DICT, D_KEYAT); op1(c, OP_STORE, kslot); emit_pop(c);
                op1(c, OP_LOAD, coll); op1(c, OP_LOAD, idx); op1(c, OP_DICT, D_VALAT); op1(c, OP_STORE, kslot + 1); emit_pop(c);
                Loop l; push_loop(c, &l, -1);
                stmt(c, s->body);
                for (int i = 0; i < l.nc; i++) patch(c, l.conts[i]);
                c->line = s->line;
                op1(c, OP_LOAD, idx); op1(c, OP_ICONST, 1); op(c, OP_IADD); op1(c, OP_STORE, idx); emit_pop(c);
                jump_back(c, OP_JMP, start);
                patch(c, jz);
                pop_loop(c);
                c->nlocals = nl; c->nextSlot = ns;
                return;
            }
            Type* el = ct->k == T_ARRAY || ct->k == T_LIST ? ct->elem : ct->k == T_STRING ? &tChar : NULL;
            if (!el) cfail(c, s->line, "'foreach' needs an array, a List, a Dictionary or a string, not %s", tname(c, ct));
            Type* vt = s->tn ? resolve_typenode(c, s->tn, 0) : el;
            if (!assignable(el, vt)) cfail(c, s->line, "'foreach': cannot convert %s to %s", tname(c, el), tname(c, vt));
            int coll = alloc_temp(c), idx = alloc_temp(c);
            op1(c, OP_STORE, coll); emit_pop(c);
            op1(c, OP_ICONST, 0); op1(c, OP_STORE, idx); emit_pop(c);
            int var = declare_local(c, s->name, s->nlen, vt, s->line), varBoxed = c->lastBoxed;
            int start = label(c);
            op1(c, OP_LOAD, idx); op1(c, OP_LOAD, coll); op(c, OP_ARRLEN); op(c, OP_ILT);
            int jz = jump(c, OP_JZ);
            op1(c, OP_LOAD, coll); op1(c, OP_LOAD, idx); op(c, OP_GETELEM);
            coerce(c, el, vt, s->line, "'foreach' variable");
            init_local(c, var, varBoxed);                   /* a new variable each time round, as in C# */
            Loop l; push_loop(c, &l, -1);
            stmt(c, s->body);
            for (int i = 0; i < l.nc; i++) patch(c, l.conts[i]);
            c->line = s->line;
            op1(c, OP_LOAD, idx); op1(c, OP_ICONST, 1); op(c, OP_IADD); op1(c, OP_STORE, idx); emit_pop(c);
            jump_back(c, OP_JMP, start);
            patch(c, jz);
            pop_loop(c);
            c->nlocals = nl; c->nextSlot = ns;
            return;
        }
        case S_SWITCH: {
            int ns = c->nextSlot;
            Type* st = expr(c, s->e);
            c->line = s->line;
            if (!isint(st) && st->k != T_STRING && st->k != T_BOOL)
                cfail(c, s->line, "'switch' works on int, char, string or bool, not %s", tname(c, st));
            int tmp = alloc_temp(c);
            op1(c, OP_STORE, tmp); emit_pop(c);
            /* tests first: one jump per label to its section */
            int** jumps = (int**)arena_alloc(&c->A, sizeof(int*) * (s->n ? s->n : 1));
            int def = -1;
            for (int i = 0; i < s->n; i++) {
                Stmt* sec = s->list[i];
                if (sec->isDefault) { if (def >= 0) cfail(c, sec->line, "'switch' has two 'default' labels"); def = i; }
                jumps[i] = (int*)arena_alloc(&c->A, sizeof(int) * (sec->nlabels ? sec->nlabels : 1));
                for (int j = 0; j < sec->nlabels; j++) {
                    c->line = sec->labels[j]->line;
                    op1(c, OP_LOAD, tmp);
                    compare(c, TK_EQ, st, expr(c, sec->labels[j]), sec->labels[j]->line);
                    op(c, OP_NOT);
                    jumps[i][j] = jump(c, OP_JZ);
                }
            }
            int jdef = jump(c, OP_JMP);
            Loop l; push_loop(c, &l, -1); l.isSwitch = 1;
            for (int i = 0; i < s->n; i++) {
                Stmt* sec = s->list[i];
                for (int j = 0; j < sec->nlabels; j++) patch(c, jumps[i][j]);
                if (i == def) patch(c, jdef);
                int nl = c->nlocals, ns2 = c->nextSlot;
                for (int k = 0; k < sec->n; k++) stmt(c, sec->list[k]);
                c->nlocals = nl; c->nextSlot = ns2;
                /* no fall-through: the end of a section leaves the switch (C# requires 'break' there anyway) */
                if (l.nb >= 128) cfail(c, sec->line, "too many 'case' sections in one switch");
                l.breaks[l.nb++] = jump(c, OP_JMP);
            }
            if (def < 0) {
                if (l.nb >= 128) cfail(c, s->line, "too many 'case' sections in one switch");
                l.breaks[l.nb++] = jdef;
            }
            pop_loop(c);
            c->nextSlot = ns;
            return;
        }
    }
}

/* ================= classes and methods ================= */
static void begin_func(C* c, int func, int cls, const char* mname, int mlen, int isStatic) {
    c->fn = &c->P->funcs[func];
    snprintf(c->fn->name, sizeof c->fn->name, "%.*s.%.*s", c->classes[cls].nlen, c->classes[cls].name, mlen, mname);
    c->cur = cls; c->curStatic = isStatic;
    c->nlocals = 0; c->nextSlot = isStatic ? 0 : 1;
    c->fdepth = 0; c->lam = NULL; c->caps.n = 0;
    c->fn->nlocals = c->nextSlot;
    c->loop = NULL; c->lastOp = -1; c->lastLabel = -1;
}

static void compile_method(C* c, int cls, CMethod* m) {
    MethodDecl* d = m->decl;
    begin_func(c, m->func, cls, m->name, m->nlen, m->isStatic);
    c->fn->nparams = m->np + (m->isStatic ? 0 : 1);
    c->isCtor = 0; c->retType = m->ret; c->isIter = m->isIter;
    scan_captures(c, d->body, NULL);
    for (int i = 0; i < m->np; i++) declare_local(c, d->params[i].name, d->params[i].nlen, m->params[i], d->line);
    box_params(c);
    stmt(c, d->body);
    c->line = d->line;
    if (m->ret->k == T_VOID || m->isIter) op(c, OP_RET); else op(c, OP_NORET);
    c->isIter = 0;
}

static void compile_ctor(C* c, int cls) {
    CClass* k = &c->classes[cls];
    MethodDecl* d = k->ctor;
    begin_func(c, k->ctorFunc, cls, ".ctor", 5, 0);
    c->fn->nparams = k->ctorNp + 1;
    c->isCtor = 1; c->retType = &tVoid;
    scan_captures(c, d ? d->body : NULL, d ? d->baseCall : NULL);
    if (d) for (int i = 0; i < d->nparams; i++) declare_local(c, d->params[i].name, d->params[i].nlen, k->ctorParams[i], d->line);
    box_params(c);
    /* same order as C#: this class's field initializers, then the base constructor, then the body */
    for (int i = 0; i < k->nf; i++) {
        CField* f = &k->fields[i];
        if (f->isStatic || !f->decl->init) continue;
        c->line = f->decl->line;
        op1(c, OP_LOAD, 0);
        expr_to(c, f->decl->init, f->type, f->decl->line, "field initializer");
        op1(c, OP_SETFIELD, f->slot);
        op(c, OP_POP);
    }
    Expr* bc = d ? d->baseCall : NULL;
    if (k->base >= 0) {
        CClass* b = &c->classes[k->base];
        c->line = bc ? bc->line : d ? d->line : k->decl->line;
        if (!bc && b->ctorNp) cfail(c, c->line, "the constructor of '%.*s' takes %d argument%s; call it with ': base(...)' after the constructor of '%.*s'",
                                    b->nlen, b->name, b->ctorNp, b->ctorNp == 1 ? "" : "s", k->nlen, k->name);
        op1(c, OP_LOAD, 0);
        if (bc) {
            char what[80]; snprintf(what, sizeof what, "constructor of '%.*s'", b->nlen, b->name);
            c->curStatic = 1;                   /* like C#: 'this' is not ready yet in base(...) arguments */
            args_for(c, bc, b->ctorParams, b->ctorNp, what);
            c->curStatic = 0;
        }
        op2(c, OP_CALL, b->ctorFunc, b->ctorNp + 1);
        op(c, OP_POP);
    } else if (bc) cfail(c, bc->line, "'%.*s' has no base class, so it cannot call base(...)", k->nlen, k->name);
    if (d) stmt(c, d->body);
    c->line = d ? d->line : k->decl->line;
    op1(c, OP_LOAD, 0);
    op(c, OP_RETV);
}

static void compile_static_init(C* c, int func) {
    for (int ci = 0; ci < c->nclasses; ci++) {
        CClass* k = &c->classes[ci];
        if (ci == 0) begin_func(c, func, 0, ".cctor", 6, 1);
        c->cur = ci; c->curStatic = 1; c->isCtor = 0; c->retType = &tVoid;
        c->caps.n = 0;
        for (int i = 0; i < k->nf; i++) {
            CField* f = &k->fields[i];
            if (!f->isStatic || !f->decl->init) continue;
            c->line = f->decl->line;
            expr_to(c, f->decl->init, f->type, f->decl->line, "field initializer");
            op1(c, OP_SETSTATIC, f->slot);
            op(c, OP_POP);
        }
    }
    if (c->nclasses == 0) begin_func(c, func, 0, ".cctor", 6, 1);
    op(c, OP_RET);
}

/* ---------- class hierarchy ---------- */
static void add_sel(C* c, CMethod* m) {
    c->sels = (CMethod**)arena_grow(&c->A, c->sels, c->nsel, &c->capsel, sizeof(CMethod*));
    m->sel = c->nsel;
    c->sels[c->nsel++] = m;
}

static int same_sig(CMethod* a, CMethod* b) {
    if (a->ret != b->ret || a->np != b->np) return 0;
    for (int i = 0; i < a->np; i++) if (a->params[i] != b->params[i]) return 0;
    return 1;
}

/* puts every class after its base class and interfaces; finds cycles */
static void visit(C* c, int i, int* n) {
    CClass* k = &c->classes[i];
    if (k->state == 2) return;
    if (k->state == 1) cfail(c, k->decl->line, "'%.*s' derives from itself (a cycle of base classes)", k->nlen, k->name);
    k->state = 1;
    if (k->base >= 0) visit(c, k->base, n);
    for (int j = 0; j < k->nifaces; j++) visit(c, k->ifaces[j], n);
    k->state = 2;
    c->order[(*n)++] = i;
}

static void collect(C* c, ProgramAST* ast) {
    Program* P = c->P;
    int nc = ast->nclasses;
    c->nclasses = nc;
    c->classes = (CClass*)arena_alloc(&c->A, sizeof(CClass) * (nc ? nc : 1));
    for (int i = 0; i < nc; i++) {
        ClassDecl* d = &ast->classes[i];
        CClass* k = &c->classes[i];
        k->name = d->name; k->nlen = d->nlen; k->decl = d;
        k->isInterface = d->isInterface; k->isAbstract = d->isAbstract || d->isInterface;
        k->base = -1; k->ctorFunc = -1;
        k->type = (Type*)arena_alloc(&c->A, sizeof(Type));
        k->type->k = T_CLASS; k->type->cls = i;
        if (d->nlen >= 32) cfail(c, d->line, "class name too long");
        /* a user class may reuse a built-in name (e.g. 'Game'); the user's class wins */
        for (int j = 0; j < i; j++)
            if (nameeq(c->classes[j].name, c->classes[j].nlen, d->name, d->nlen)) cfail(c, d->line, "class '%.*s' is defined twice", d->nlen, d->name);
    }

    /* base class and interfaces */
    for (int i = 0; i < nc; i++) {
        ClassDecl* d = &ast->classes[i];
        CClass* k = &c->classes[i];
        k->ifaces = (int*)arena_alloc(&c->A, sizeof(int) * (d->nbases ? d->nbases : 1));
        for (int j = 0; j < d->nbases; j++) {
            TypeNode* b = d->bases[j];
            int bi = find_class(c, b->name, b->nlen);
            if (bi < 0) cfail(c, b->line, "unknown class or interface '%.*s'", b->nlen, b->name);
            if (bi == i) cfail(c, b->line, "'%.*s' cannot derive from itself", d->nlen, d->name);
            CClass* bk = &c->classes[bi];
            if (!bk->isInterface) {
                if (k->isInterface) cfail(c, b->line, "an interface can only extend interfaces, and '%.*s' is a class", b->nlen, b->name);
                if (j > 0) cfail(c, b->line, "a class has only one base class, and it must come first after ':' ('%.*s')", b->nlen, b->name);
                k->base = bi;
            } else {
                for (int q = 0; q < k->nifaces; q++)
                    if (k->ifaces[q] == bi) cfail(c, b->line, "'%.*s' is listed twice", b->nlen, b->name);
                k->ifaces[k->nifaces++] = bi;
            }
        }
    }
    c->order = (int*)arena_alloc(&c->A, sizeof(int) * (nc ? nc : 1));
    int n = 0;
    for (int i = 0; i < nc; i++) visit(c, i, &n);
    for (int oi = 0; oi < nc; oi++) {
        int i = c->order[oi];
        CClass* k = &c->classes[i];
        uint8_t* isa = (uint8_t*)arena_alloc(&c->A, nc);
        isa[i] = 1;
        if (k->base >= 0) for (int q = 0; q < nc; q++) isa[q] |= c->classes[k->base].type->isa[q];
        for (int j = 0; j < k->nifaces; j++) for (int q = 0; q < nc; q++) isa[q] |= c->classes[k->ifaces[j]].type->isa[q];
        k->type->isa = isa;
    }

    int nfuncs = 1 + 3 + ast->nlambdas;  /* static init, the Main/Update/Draw dispatchers, lambdas */
    for (int i = 0; i < nc; i++) nfuncs += 1 + ast->classes[i].nmethods;
    P->funcs = (Func*)calloc(nfuncs, sizeof(Func));
    P->capfuncs = nfuncs;
    P->classes = (ClassRT*)calloc(nc ? nc : 1, sizeof(ClassRT));
    P->nclasses = nc;

    /* fields: a derived class's instance fields come after its base class's */
    int nstatics = 0;
    for (int oi = 0; oi < nc; oi++) {
        int i = c->order[oi];
        ClassDecl* d = &ast->classes[i];
        CClass* k = &c->classes[i];
        k->ninst = k->base >= 0 ? c->classes[k->base].ninst : 0;
        k->fields = (CField*)arena_alloc(&c->A, sizeof(CField) * (d->nfields ? d->nfields : 1));
        for (int j = 0; j < d->nfields; j++) {
            FieldDecl* fd = &d->fields[j];
            for (int q = 0; q < k->nf; q++)
                if (nameeq(k->fields[q].name, k->fields[q].nlen, fd->name, fd->nlen)) cfail(c, fd->line, "field '%.*s' is defined twice", fd->nlen, fd->name);
            if (k->base >= 0 && find_field(c, k->base, fd->name, fd->nlen))
                cfail(c, fd->line, "field '%.*s' is already in base class '%.*s'; use a different name", fd->nlen, fd->name,
                      c->classes[k->base].nlen, c->classes[k->base].name);
            CField* f = &k->fields[k->nf++];
            f->name = fd->name; f->nlen = fd->nlen; f->isStatic = fd->isStatic; f->decl = fd;
            f->type = resolve_typenode(c, fd->tn, 0);
            f->slot = fd->isStatic ? nstatics++ : k->ninst++;
        }
    }
    P->nstatics = nstatics;
    P->staticDefaults = (Value*)calloc(nstatics ? nstatics : 1, sizeof(Value));

    int fi = 0;
    for (int oi = 0; oi < nc; oi++) {
        int i = c->order[oi];
        ClassDecl* d = &ast->classes[i];
        CClass* k = &c->classes[i];
        ClassRT* rt = &P->classes[i];
        snprintf(rt->name, sizeof rt->name, "%.*s", d->nlen, d->name);
        rt->nfields = k->ninst;
        rt->base = k->base;
        rt->defaults = (Value*)calloc(k->ninst ? k->ninst : 1, sizeof(Value));
        if (k->base >= 0) memcpy(rt->defaults, P->classes[k->base].defaults, sizeof(Value) * c->classes[k->base].ninst);
        for (int j = 0; j < k->nf; j++) {
            CField* f = &k->fields[j];
            if (f->isStatic) P->staticDefaults[f->slot] = default_value(f->type);
            else rt->defaults[f->slot] = default_value(f->type);
        }
        k->methods = (CMethod*)arena_alloc(&c->A, sizeof(CMethod) * (d->nmethods ? d->nmethods : 1));
        if (!k->isInterface) k->ctorFunc = fi++;
        for (int j = 0; j < d->nmethods; j++) {
            MethodDecl* md = &d->methods[j];
            Type** params = (Type**)arena_alloc(&c->A, sizeof(Type*) * (md->nparams ? md->nparams : 1));
            for (int q = 0; q < md->nparams; q++) {
                params[q] = resolve_typenode(c, md->params[q].tn, 0);
                for (int r = 0; r < q; r++)
                    if (nameeq(md->params[r].name, md->params[r].nlen, md->params[q].name, md->params[q].nlen))
                        cfail(c, md->line, "parameter '%.*s' is listed twice", md->params[q].nlen, md->params[q].name);
            }
            if (md->isCtor) {
                if (md->isIter) cfail(c, md->line, "a constructor cannot use 'yield'");
                if (k->ctor) cfail(c, md->line, "only one constructor per class is supported for now");
                k->ctor = md; k->ctorParams = params; k->ctorNp = md->nparams;
                continue;
            }
            if (find_own_method(c, i, md->name, md->nlen)) cfail(c, md->line, "method '%.*s' is defined twice (overloading is not supported yet)", md->nlen, md->name);
            if (find_field(c, i, md->name, md->nlen)) cfail(c, md->line, "'%.*s' is both a field and a method", md->nlen, md->name);
            CMethod* m = &k->methods[k->nm++];
            m->name = md->name; m->nlen = md->nlen; m->isStatic = md->isStatic; m->decl = md;
            m->ret = resolve_typenode(c, md->ret, 0);
            m->isIter = md->isIter;
            if (m->isIter && m->ret->k != T_CORO)
                cfail(c, md->line, "'%.*s' uses 'yield', so it must return IEnumerator", md->nlen, md->name);
            m->params = params; m->np = md->nparams;
            m->owner = i; m->sel = -1;
            m->func = md->body ? fi++ : -1;
            if (m->func >= 0) P->funcs[m->func].isIter = (uint8_t)m->isIter;
            /* virtual methods */
            CMethod* inh = k->base >= 0 ? find_method(c, k->base, md->name, md->nlen) : NULL;
            int isToString = namecs(md->name, md->nlen, "ToString");
            if (md->kind == MK_OVERRIDE) {
                if (isToString && (m->np || m->ret != &tString)) cfail(c, md->line, "write it as 'public override string ToString()'");
                if (inh) {
                    CClass* ik = &c->classes[inh->owner];
                    if (inh->sel < 0)
                        cfail(c, md->line, "cannot override '%.*s.%.*s' because it is not virtual; write 'public virtual ...' there",
                              ik->nlen, ik->name, md->nlen, md->name);
                    if (!same_sig(inh, m))
                        cfail(c, md->line, "'%.*s' must have the same return type and parameters as in '%.*s'", md->nlen, md->name, ik->nlen, ik->name);
                    m->sel = inh->sel;
                } else if (isToString) {        /* object.ToString() */
                    if (c->toStringSel < 0) { add_sel(c, m); c->toStringSel = m->sel; }
                    else m->sel = c->toStringSel;
                } else cfail(c, md->line, "'%.*s' has nothing to override: no base class of '%.*s' has a virtual method '%.*s'",
                             md->nlen, md->name, k->nlen, k->name, md->nlen, md->name);
            } else if (inh) {
                CClass* ik = &c->classes[inh->owner];
                cfail(c, md->line, "'%.*s' is already in base class '%.*s'; to replace it, make it 'virtual' (or 'abstract') there and 'override' here",
                      md->nlen, md->name, ik->nlen, ik->name);
            } else if (md->kind == MK_VIRTUAL || md->kind == MK_ABSTRACT) add_sel(c, m);
            else if (isToString && !m->isStatic) cfail(c, md->line, "write it as 'public override string ToString()'");
        }
    }
    P->staticInit = fi++;
    P->nfuncs = fi;

    /* vtables: what each selector calls for an object of this class */
    P->nsel = c->nsel;
    for (int oi = 0; oi < nc; oi++) {
        int i = c->order[oi];
        CClass* k = &c->classes[i];
        int* vt = (int*)arena_alloc(&c->A, sizeof(int) * (c->nsel ? c->nsel : 1));
        for (int s = 0; s < c->nsel; s++) vt[s] = k->base >= 0 ? c->classes[k->base].vtable[s] : -1;
        for (int j = 0; j < k->nm; j++)
            if (k->methods[j].sel >= 0) vt[k->methods[j].sel] = k->methods[j].func >= 0 ? k->methods[j].func : -2;
        k->vtable = vt;
        if (!k->isInterface) {
            /* every method of every interface this class implements, directly or through its base class */
            for (int q = 0; q < nc; q++) {
                CClass* ik = &c->classes[q];
                if (!ik->isInterface || !k->type->isa[q]) continue;
                for (int j = 0; j < ik->nm; j++) {
                    CMethod* im = &ik->methods[j];
                    CMethod* impl = find_method(c, i, im->name, im->nlen);
                    if (!impl || impl->isStatic)
                        cfail(c, k->decl->line, "class '%.*s' must have the method '%.*s' of interface '%.*s'",
                              k->nlen, k->name, im->nlen, im->name, ik->nlen, ik->name);
                    if (!same_sig(impl, im))
                        cfail(c, impl->decl->line, "'%.*s' must have the same return type and parameters as in interface '%.*s'",
                              im->nlen, im->name, ik->nlen, ik->name);
                    vt[im->sel] = impl->sel >= 0 ? vt[impl->sel] : impl->func;
                }
            }
            if (!k->isAbstract)
                for (int s = 0; s < c->nsel; s++)
                    if (vt[s] == -2) {
                        CMethod* am = c->sels[s];
                        CClass* ak = &c->classes[am->owner];
                        cfail(c, k->decl->line, "class '%.*s' must override the abstract method '%.*s.%.*s' (or be 'abstract class')",
                              k->nlen, k->name, ak->nlen, ak->name, am->nlen, am->name);
                    }
        }
        ClassRT* rt = &P->classes[i];
        rt->vtable = (int*)malloc(sizeof(int) * (c->nsel ? c->nsel : 1));
        rt->isa = (uint8_t*)malloc(nc ? nc : 1);
        if (!rt->vtable || !rt->isa) cfail(c, 1, "out of memory");
        for (int s = 0; s < c->nsel; s++) rt->vtable[s] = vt[s] >= 0 ? vt[s] : -1;
        memcpy(rt->isa, k->type->isa, nc);
        rt->toString = c->toStringSel >= 0 ? rt->vtable[c->toStringSel] : -1;
    }
}

/* Every class may have 'static void Main()', 'static void Update()' and 'static void Draw()' (like the Unity scripts of one scene):
 * the program calls all of them, in the order of the classes in the project (files by name, classes top to bottom).
 * With more than one, a small function made here does the calls, so the hosts keep calling a single entry. */
static int entry_point(C* c, const char* name) {
    int fns[256], ords[256], n = 0;
    for (int i = 0; i < c->nclasses; i++) {
        CMethod* m = find_own_method(c, i, name, (int)strlen(name));
        if (!m || !m->isStatic || m->np != 0 || m->ret->k != T_VOID) continue;
        if (n == 256) cfail(c, m->decl->line, "too many classes with 'static void %s()' (at most 256)", name);
        /* [Order(n)] on the class: smaller numbers first; equal ones keep the order of the classes (files by name) */
        int ord = c->classes[i].decl ? c->classes[i].decl->order : 0, k = n++;
        while (k > 0 && ords[k - 1] > ord) { fns[k] = fns[k - 1]; ords[k] = ords[k - 1]; k--; }
        fns[k] = m->func; ords[k] = ord;
    }
    if (n == 0) return -1;
    if (n == 1) return fns[0];
    Program* P = c->P;
    if (P->nfuncs >= P->capfuncs) cfail(c, 1, "internal error: no room for the '%s' dispatcher", name);
    int fi = P->nfuncs++;
    Func* f = &P->funcs[fi];
    int len = 3 * n + 1;
    f->code = (int32_t*)malloc(sizeof(int32_t) * len);
    f->lines = (int*)calloc(len, sizeof(int));
    if (!f->code || !f->lines) cfail(c, 1, "out of memory");
    int k = 0;
    for (int j = 0; j < n; j++) { f->code[k++] = OP_CALL; f->code[k++] = fns[j]; f->code[k++] = 0; }
    f->code[k++] = OP_RET;
    f->len = f->cap = len;
    f->nparams = 0; f->nlocals = 0;
    snprintf(f->name, sizeof f->name, "%s (all classes)", name);
    return fi;
}

static Program* compile(const char* src, char* err, int errlen, char* replDecls, int replDeclsCap) {
    C* c = (C*)calloc(1, sizeof(C));
    if (!c) { snprintf(err, errlen, "out of memory"); return NULL; }
    Type* prims[] = { &tVoid, &tInt, &tFloat, &tBool, &tString, &tNull, &tAny, &tChar, &tCoro };
    for (size_t i = 0; i < sizeof prims / sizeof prims[0]; i++) { prims[i]->arr = NULL; prims[i]->list = NULL; }
    c->toStringSel = -1;
    c->pc.A = &c->A; c->pc.jb = &c->jb; c->pc.err = err; c->pc.errlen = errlen;
    c->P = (Program*)calloc(1, sizeof(Program));
    err[0] = 0;
    if (setjmp(c->jb)) {
        ms_free_program(c->P);
        arena_free(&c->A);
        free(c);
        return NULL;
    }
    int ntok;
    Token* toks = ms_lex(&c->pc, src, &ntok);
    ProgramAST* ast = ms_parse(&c->pc, toks, ntok);
    collect(c, ast);
    if (replDecls) {
        int ci = find_class(c, "Repl", 4);
        CMethod* m = ci >= 0 ? find_method(c, ci, "Main", 4) : NULL;
        if (m) c->replBody = m->decl->body;
        c->replDecls = replDecls; c->replDeclsCap = replDeclsCap;
        for (int i = 0; i < ms_nnatives; i++)
            if (!strcmp(ms_natives[i].cls, "Console") && !strcmp(ms_natives[i].name, "WriteLine") && ms_natives[i].np == 1) c->replPrint = i;
    }
    for (int i = 0; i < c->nclasses; i++) {
        if (c->classes[i].isInterface) continue;
        compile_ctor(c, i);
        for (int j = 0; j < c->classes[i].nm; j++)
            if (c->classes[i].methods[j].func >= 0) compile_method(c, i, &c->classes[i].methods[j]);
    }
    compile_static_init(c, c->P->staticInit);
    c->P->entryMain = entry_point(c, "Main");
    c->P->entryUpdate = entry_point(c, "Update");
    c->P->entryDraw = entry_point(c, "Draw");
    if (c->P->entryMain < 0 && c->P->entryUpdate < 0)
        cfail(c, 1, "no entry point: add 'static void Main()' or 'static void Update()'");
    Program* P = c->P;
    arena_free(&c->A);
    free(c);
    return P;
}

Program* ms_compile(const char* src, char* err, int errlen) { return compile(src, err, errlen, NULL, 0); }
Program* ms_compile_repl(const char* src, char* err, int errlen, char* decls, int declsCap) { return compile(src, err, errlen, decls, declsCap); }

void ms_free_program(Program* p) {
    if (!p) return;
    for (int i = 0; i < p->capfuncs; i++) { free(p->funcs[i].code); free(p->funcs[i].lines); }
    free(p->funcs);
    for (int i = 0; i < p->nstrings; i++) free(p->strings[i]);
    free(p->strings);
    for (int i = 0; i < p->nclasses; i++) { free(p->classes[i].defaults); free(p->classes[i].vtable); free(p->classes[i].isa); }
    free(p->classes);
    free(p->staticDefaults);
    free(p);
}
