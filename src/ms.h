/* MiniSharp - a small C#-like language that compiles and runs on Nintendo 3DS.
 * Shared definitions: values, objects, bytecode, program and VM. */
#ifndef MS_H
#define MS_H

#include <stdint.h>
#include <stddef.h>

/* ---------- runtime values ---------- */
typedef struct Obj Obj;
enum { V_NULL, V_INT, V_FLOAT, V_BOOL, V_OBJ, V_CHAR };
typedef struct {
    uint8_t t;
    union { int32_t i; float f; Obj* o; } as;
} Value;

enum { O_STRING, O_ARRAY, O_INSTANCE, O_LIST, O_CORO, O_DICT, O_CELL, O_CLOSURE };
struct Obj { uint8_t type, marked, pinned; Obj* next; };
typedef struct { Obj h; int32_t len; char chars[]; } ObjString;
typedef struct { Obj h; int32_t len; Value items[]; } ObjArray;
typedef struct { Obj h; int32_t cls; int32_t nfields; Value fields[]; } ObjInstance;
typedef struct { Obj h; int32_t len, cap; Value* items; } ObjList;
/* Dictionary, laid out like .NET's so foreach gives the same order: entries are kept in the order
 * they were added, and a removed entry's place goes to a free list that the next Add reuses.
 * 'index' is an open-addressing hash table of entry numbers (-1 empty, -2 removed). */
typedef struct { Value key, val; uint32_t hash; int32_t live; } DictEntry;   /* free entry: hash = next free */
typedef struct {
    Obj h;
    int32_t count, used, cap, icap, ntomb, freeList, freeCount;
    DictEntry* entries; int32_t* index;
} ObjDict;

/* A local variable that a lambda uses lives in a cell, so the method and the lambda share it. */
typedef struct { Obj h; Value v; } ObjCell;
/* A delegate (Func / Action): a lambda with its captured cells, or a method with its object.
 * Calling it puts 'self' in slot 0 (CL_SELF) or drops that slot (CL_STATIC, a static method). */
enum { CL_SELF, CL_STATIC, CL_MULTI /* a += b: ups = the delegates to call in order */ };
typedef struct { Obj h; int32_t func; uint8_t kind; int32_t nup; Value self; Value ups[]; } ObjClosure;

/* A coroutine (IEnumerator). 'yield' is only allowed as a statement in the iterator method itself,
 * so its whole state is the method's locals plus the resume position. */
typedef struct {
    Obj h;
    int32_t func, ip, nlocals;
    uint8_t started, done, running, waitKind;   /* waitKind: Y_* */
    int32_t waitUntil, lastFrame;               /* ms for WaitForSeconds; frame of the last yield */
    Obj* waitFor;                               /* coroutine we wait for (Y_CORO) */
    Obj* owner;                                 /* started by the owner's 'yield return': stops with it */
    Value locals[];
} ObjCoro;

/* ---------- static types (used by compiler and native signatures) ---------- */
enum { T_VOID, T_INT, T_FLOAT, T_BOOL, T_STRING, T_NULL, T_CLASS, T_ARRAY, T_ANY, T_CHAR, T_LIST,
       T_STRARR /* string[], only in native signatures */, T_CORO /* IEnumerator / Coroutine */,
       T_DICT, T_KVP /* Dictionary<K,V> and the KeyValuePair<K,V> of its foreach */,
       T_FUNC /* Func<...> / Action<...> / Predicate<T> / Comparison<T> */ };

/* ---------- bytecode ---------- */
enum {
    OP_ICONST, OP_FCONST, OP_SCONST, OP_NULL, OP_TRUE, OP_FALSE,
    OP_POP, OP_DUP, OP_DUP2,
    OP_LOAD, OP_STORE, OP_STOREP,
    OP_GETSTATIC, OP_SETSTATIC, OP_GETFIELD, OP_SETFIELD, OP_GETELEM, OP_SETELEM,
    OP_IADD, OP_ISUB, OP_IMUL, OP_IDIV, OP_IMOD, OP_INEG,
    OP_FADD, OP_FSUB, OP_FMUL, OP_FDIV, OP_FMOD, OP_FNEG,
    OP_I2F, OP_F2I, OP_TOSTR, OP_CONCAT,
    OP_ILT, OP_ILE, OP_IGT, OP_IGE, OP_IEQ, OP_INE,
    OP_FLT, OP_FLE, OP_FGT, OP_FGE, OP_FEQ, OP_FNE,
    OP_SEQ, OP_SNE, OP_REQ, OP_RNE, OP_NOT,
    OP_JMP, OP_JZ, OP_JFALSE_KEEP, OP_JTRUE_KEEP,
    OP_CALL, OP_CALLN, OP_NULLCHK, OP_RET, OP_RETV,
    OP_NEW, OP_NEWARR, OP_ARRLEN, OP_STRLEN, OP_NORET,
    OP_IAND, OP_IOR, OP_IXOR, OP_ISHL, OP_ISHR, OP_INOT, OP_I2C, OP_C2I,
    OP_LIST,                            /* sub-op (L_*) follows */
    OP_CORO_NEW,                        /* func, argc: calling an iterator method makes a coroutine */
    OP_YIELD,                           /* Y_* kind */
    OP_CORO,                            /* C_* sub-op */
    OP_DICT,                            /* D_* sub-op, | default-value kind << 8 for D_TRYGET */
    OP_CALLV,                           /* selector, argc (with 'this'): virtual / interface call through the object's vtable */
    OP_ISINST,                          /* class, mode: 0 'is' (bool), 1 'as' (object or null), 2 cast (fails if not one) */
    OP_BOX, OP_GETCELL, OP_SETCELL,     /* value -> new cell; cell -> value; cell value -> value */
    OP_UPVAL,                           /* k: push captured cell k of the running lambda */
    OP_CLOSURE,                         /* kind (0 lambda / method, 1 static method, 2 virtual: idx = selector), idx, nup:
                                           self, cells... -> delegate */
    OP_CALLF,                           /* argc, hasResult: delegate, args... -> call it */
    OP_DELEG,                           /* 0 a += b, 1 a -= b */
    OP_JNULL, OP_JNULL_POP, OP_JNNULL,  /* offset: a?.b (null stays / is popped), a ?? b (non-null stays, null is popped) */
    OP_LISTF                            /* LF_* | default-value kind << 8: list, delegate -> result */
};
enum { LF_FIND, LF_FINDLAST, LF_FINDALL, LF_FINDINDEX, LF_FINDLASTINDEX, LF_EXISTS, LF_TRUEFORALL, LF_REMOVEALL, LF_FOREACH, LF_SORT };
enum { D_NEW, D_ADD, D_REMOVE, D_CONTAINS, D_CONTAINSVAL, D_CLEAR, D_TRYGET, D_KEYS, D_VALUES, D_NEXT, D_KEYAT, D_VALAT };
enum { Y_NEXT, Y_SECONDS, Y_CORO };
enum { C_START, C_STOP, C_STOPALL };
enum { L_NEW, L_ADD, L_INSERT, L_REMOVEAT, L_REMOVE, L_CLEAR, L_INDEXOF, L_ADDRANGE, L_SORT, L_REVERSE };

typedef struct {
    int32_t* code; int* lines; int len, cap;
    int nparams, nlocals;
    uint8_t isIter;                     /* calling it makes a coroutine */
    char name[64];
} Func;

typedef struct {
    char name[32]; int nfields; Value* defaults;
    int base;                           /* base class or -1 */
    int* vtable;                        /* [Program.nsel]: function for each virtual selector, -1 none */
    uint8_t* isa;                       /* [nclasses]: 1 if an object of this class is also that class / interface */
    int toString;                       /* 'override string ToString()' or -1 */
} ClassRT;

typedef struct {
    Func* funcs; int nfuncs, capfuncs;
    ObjString** strings; int nstrings, capstrings;
    ClassRT* classes; int nclasses;
    int nsel;                           /* virtual selectors (length of every vtable) */
    Value* staticDefaults; int nstatics;
    int staticInit, entryMain, entryUpdate, entryDraw;
} Program;

/* ---------- VM ---------- */
#define MS_STACK  8192
#define MS_FRAMES 256

typedef struct { Func* f; int ip; int base; Obj* clo; } Frame;    /* clo: the delegate being run, for OP_UPVAL */

typedef struct VM {
    Program* p;
    Value* stack; int sp;
    Frame frames[MS_FRAMES]; int fp;
    Value* statics;
    Obj* objects; size_t bytes, nextGC;
    Obj** gray; int ngray, capgray;
    char err[256];
    int quit;
    ObjCoro** coros; int ncoros, capcoros;     /* started coroutines (GC roots) */
    ObjCoro* curCoro;                           /* the one running right now */
    int frameNo;                                /* counts vm_tick_coroutines calls */
    long long deadline; unsigned loops;         /* REPL: a loop still running at time() > deadline stops (0 = never) */
} VM;

/* ---------- natives ---------- */
typedef Value (*NativeFn)(VM* vm, Value* args);
#define MS_MAXNATIVEARGS 10
typedef struct { const char* cls; const char* name; int np; int params[MS_MAXNATIVEARGS]; int ret; NativeFn fn; } NativeDef;
typedef struct { const char* cls; const char* name; int isFloat; int ivalue; float fvalue; } ConstDef;
extern const NativeDef ms_natives[];
extern const int ms_nnatives;
extern const ConstDef ms_consts[];
extern const int ms_nconsts;

/* ---------- API ---------- */
Program* ms_compile(const char* src, char* err, int errlen);
void ms_free_program(Program* p);
/* REPL: like ms_compile, but in Repl.Main a variable declared at the top level becomes a static field
 * ("static int x; " is appended to 'decls' so the next input can declare it in the class) and an
 * expression on its own is printed */
Program* ms_compile_repl(const char* src, char* err, int errlen, char* decls, int declsCap);

VM* vm_new(Program* p);
int vm_set_program(VM* vm, Program* p); /* REPL: go on with a newer program whose statics start with the old ones */
void vm_free(VM* vm);
int vm_call(VM* vm, int func);          /* 1 = ok, 0 = runtime error or quit */
int vm_tick_coroutines(VM* vm);         /* once per frame after Update(); same result codes */
Value vm_make_string(VM* vm, const char* s, int len);
Value vm_make_array(VM* vm, int len);      /* filled with null */
void ms_value_to_text(VM* vm, Value v, char* buf, int buflen);

#endif
