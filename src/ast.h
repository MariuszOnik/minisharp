/* MiniSharp - tokens, AST and arena allocator (internal to the compiler). */
#ifndef MS_AST_H
#define MS_AST_H

#include <stdint.h>
#include <setjmp.h>
#include <stddef.h>

/* ---------- arena ---------- */
typedef struct ArenaBlock { struct ArenaBlock* next; size_t used, cap; char data[]; } ArenaBlock;
typedef struct { ArenaBlock* head; } Arena;
void* arena_alloc(Arena* a, size_t n);
void arena_free(Arena* a);
void* arena_grow(Arena* a, void* old, int n, int* cap, size_t sz);

/* ---------- tokens ---------- */
enum {
    TK_EOF, TK_IDENT, TK_INT, TK_FLOAT, TK_STRING,
    TK_CLASS, TK_STATIC, TK_VOID, TK_KINT, TK_KFLOAT, TK_KBOOL, TK_KSTRING, TK_VAR,
    TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_RETURN, TK_NEW, TK_TRUE, TK_FALSE, TK_NULL,
    TK_BREAK, TK_CONTINUE, TK_THIS, TK_PUBLIC, TK_PRIVATE, TK_USING, TK_NAMESPACE, TK_DO, TK_CONST, TK_READONLY,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACK, TK_RBRACK,
    TK_SEMI, TK_COMMA, TK_DOT, TK_QUESTION, TK_COLON,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT, TK_BANG,
    TK_ASSIGN, TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ, TK_SLASHEQ, TK_PERCENTEQ,
    TK_EQ, TK_NE, TK_LT, TK_LE, TK_GT, TK_GE, TK_AND, TK_OR, TK_INC, TK_DEC,
    TK_CHAR, TK_KCHAR, TK_FOREACH, TK_IN, TK_SWITCH, TK_CASE, TK_DEFAULT,
    TK_AMP, TK_PIPE, TK_CARET, TK_TILDE, TK_SHL, TK_AMPEQ, TK_PIPEEQ, TK_CARETEQ, TK_SHLEQ,
    TK_SHR, TK_SHREQ,                   /* never made by the lexer: the parser joins '>' '>' (see optok) */
    TK_OUT,
    TK_ABSTRACT, TK_VIRTUAL, TK_OVERRIDE, TK_SEALED, TK_INTERFACE, TK_BASE, TK_IS, TK_AS,
    TK_ARROW,                           /* => */
    TK_QDOT, TK_QQ                      /* ?. ?? */
};

typedef struct { int kind; const char* start; int len; int line; int32_t ival; float fval; } Token;

/* ---------- AST ---------- */
typedef struct TypeNode {
    int base; const char* name; int nlen; int dims; int line;
    struct TypeNode *arg, *arg2;        /* List<arg>, Dictionary<arg, arg2> */
    struct TypeNode** args; int nargs;  /* all type arguments: Func<int, int, bool> */
} TypeNode;

enum {
    E_INT, E_FLOAT, E_BOOL, E_STRING, E_NULL, E_NAME, E_THIS,
    E_MEMBER, E_INDEX, E_CALL, E_NEW, E_NEWARR,
    E_UNARY, E_BINARY, E_LOGIC, E_ASSIGN, E_INCDEC, E_CAST, E_COND,
    E_CHAR, E_NEWLIST,              /* E_NEWLIST: tn = List<T> or Dictionary<K,V>, args = initializer items, a = capacity or NULL */
    E_PAIR,                         /* dictionary initializer item: {a, b}, or [a] = b when prefix is set */
    E_OUT,                          /* 'out x' (a = target) or 'out var x' / 'out T x' (prefix = 1, tn = type or NULL, name) */
    E_BASE,                         /* 'base' (only as base.Member) */
    E_IS, E_AS,                     /* a is T [name] / a as T: tn = type (NULL: 'is null'), name = pattern variable; op = 1 for 'is not' */
    E_LAMBDA,                       /* params / nparams (tn NULL if not written), body (block) or a (expression) */
    E_COALESCE,                     /* a ?? b */
    E_STACK                         /* compiler only: a value already on the stack (for a?.b) */
};

typedef struct Expr Expr;
struct Expr {
    int k, line, op;
    int32_t ival; float fval;
    const char* name; int nlen;     /* identifiers, member names, string literal text */
    Expr *a, *b, *c;
    Expr** args; int nargs;
    TypeNode* tn;
    int prefix;                     /* ++x vs x++ ; for E_NEWARR: has initializer list; E_MEMBER: a?.name */
    struct Param* params; int nparams; struct Stmt* body;      /* E_LAMBDA */
    const char *s0, *s1;            /* source text [s0, s1) this node was parsed from (NULL: made up by the parser) */
};

enum { S_BLOCK, S_VAR, S_EXPR, S_IF, S_WHILE, S_DO, S_FOR, S_RETURN, S_BREAK, S_CONTINUE,
       S_FOREACH, S_SWITCH, S_CASE, S_YIELD, S_YIELDBREAK };   /* S_SWITCH: list = S_CASE sections; S_CASE: labels + list of statements */

typedef struct Stmt Stmt;
struct Stmt {
    int k, line;
    Stmt** list; int n;
    TypeNode* tn;                   /* S_VAR: NULL means 'var' */
    const char* name; int nlen;
    Expr* e; Expr* step;
    Stmt *body, *els, *init;
    Expr** labels; int nlabels; int isDefault;
    const char *s0, *s1;            /* source text incl. ';' (NULL: made up by the parser, e.g. the body of '=> expr') */
};

typedef struct Param { TypeNode* tn; const char* name; int nlen; } Param;
enum { MK_NONE, MK_VIRTUAL, MK_OVERRIDE, MK_ABSTRACT };
typedef struct {
    int line, isStatic, isCtor, isIter;     /* isIter: the body uses 'yield' */
    int kind;                               /* MK_*; interface methods are MK_ABSTRACT */
    TypeNode* ret; const char* name; int nlen;
    Param* params; int nparams;
    Stmt* body;                             /* NULL for abstract and interface methods */
    Expr* baseCall;                         /* constructor: ': base(args)' as an E_CALL, or NULL */
    int exprBody;                           /* written as '=> expr;' (body is a made-up block with one statement) */
    const char *s0, *s1;                    /* source text from the first modifier to the closing '}' or ';' */
} MethodDecl;
enum { FA_IN = 1, FA_OUT = 2, FA_ORDER = 4 };   /* attributes: [In] / [Out] on a field, [Order(n)] on a class */
typedef struct { int line, isStatic; TypeNode* tn; const char* name; int nlen; Expr* init; const char *s0, *s1; int attrs; } FieldDecl;
typedef struct {
    int line; const char* name; int nlen;
    int isAbstract, isInterface;
    TypeNode** bases; int nbases;           /* ': Base, IFoo, IBar' (which one is the class is decided later) */
    FieldDecl* fields; int nfields;
    MethodDecl* methods; int nmethods;
    const char *s0, *s1;                    /* source text from the first attribute or modifier to the closing '}' */
    int order;                              /* [Order(n)]: its Main/Update/Draw run before the ones of higher numbers (0 by default) */
} ClassDecl;
typedef struct { ClassDecl* classes; int nclasses; int nlambdas; } ProgramAST;

/* ---------- front end ---------- */
typedef struct {
    Arena* A;
    jmp_buf* jb;
    char* err; int errlen;
} ParseCtx;

/* Both functions longjmp to ctx->jb on error, after writing ctx->err. */
Token* ms_lex(ParseCtx* ctx, const char* src, int* ntok);
ProgramAST* ms_parse(ParseCtx* ctx, Token* toks, int ntok);

#endif
