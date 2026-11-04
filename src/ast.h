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
    TK_OUT
};

typedef struct { int kind; const char* start; int len; int line; int32_t ival; float fval; } Token;

/* ---------- AST ---------- */
typedef struct TypeNode { int base; const char* name; int nlen; int dims; int line; struct TypeNode *arg, *arg2; } TypeNode;  /* List<arg>, Dictionary<arg, arg2> */

enum {
    E_INT, E_FLOAT, E_BOOL, E_STRING, E_NULL, E_NAME, E_THIS,
    E_MEMBER, E_INDEX, E_CALL, E_NEW, E_NEWARR,
    E_UNARY, E_BINARY, E_LOGIC, E_ASSIGN, E_INCDEC, E_CAST, E_COND,
    E_CHAR, E_NEWLIST,              /* E_NEWLIST: tn = List<T> or Dictionary<K,V>, args = initializer items, a = capacity or NULL */
    E_PAIR,                         /* dictionary initializer item: {a, b}, or [a] = b when prefix is set */
    E_OUT                           /* 'out x' (a = target) or 'out var x' / 'out T x' (prefix = 1, tn = type or NULL, name) */
};

typedef struct Expr Expr;
struct Expr {
    int k, line, op;
    int32_t ival; float fval;
    const char* name; int nlen;     /* identifiers, member names, string literal text */
    Expr *a, *b, *c;
    Expr** args; int nargs;
    TypeNode* tn;
    int prefix;                     /* ++x vs x++ ; for E_NEWARR: has initializer list */
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
};

typedef struct { TypeNode* tn; const char* name; int nlen; } Param;
typedef struct {
    int line, isStatic, isCtor, isIter;     /* isIter: the body uses 'yield' */
    TypeNode* ret; const char* name; int nlen;
    Param* params; int nparams;
    Stmt* body;
} MethodDecl;
typedef struct { int line, isStatic; TypeNode* tn; const char* name; int nlen; Expr* init; } FieldDecl;
typedef struct {
    int line; const char* name; int nlen;
    FieldDecl* fields; int nfields;
    MethodDecl* methods; int nmethods;
} ClassDecl;
typedef struct { ClassDecl* classes; int nclasses; } ProgramAST;

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
