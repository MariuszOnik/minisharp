/* MiniSharp - stack virtual machine with a mark & sweep garbage collector. */
#include "ms.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ================= heap ================= */
static size_t obj_size(Obj* o) {
    switch (o->type) {
        case O_STRING: return sizeof(ObjString) + ((ObjString*)o)->len + 1;
        case O_ARRAY: return sizeof(ObjArray) + sizeof(Value) * ((ObjArray*)o)->len;
        case O_LIST: return sizeof(ObjList) + sizeof(Value) * ((ObjList*)o)->cap;
        case O_CORO: return sizeof(ObjCoro) + sizeof(Value) * ((ObjCoro*)o)->nlocals;
        case O_DICT: return sizeof(ObjDict) + sizeof(DictEntry) * ((ObjDict*)o)->cap + sizeof(int32_t) * ((ObjDict*)o)->icap;
        case O_CELL: return sizeof(ObjCell);
        case O_CLOSURE: return sizeof(ObjClosure) + sizeof(Value) * ((ObjClosure*)o)->nup;
        default: return sizeof(ObjInstance) + sizeof(Value) * ((ObjInstance*)o)->nfields;
    }
}

static void mark_value(VM* vm, Value v) {
    if (v.t != V_OBJ || !v.as.o) return;
    Obj* o = v.as.o;
    if (o->marked || o->pinned) return;
    o->marked = 1;
    if (o->type == O_STRING) return;
    if (vm->ngray == vm->capgray) {
        int nc = vm->capgray ? vm->capgray * 2 : 256;
        Obj** g = (Obj**)realloc(vm->gray, sizeof(Obj*) * nc);
        if (!g) return;     /* worst case: children stay unmarked; avoided by reserving below */
        vm->gray = g; vm->capgray = nc;
    }
    vm->gray[vm->ngray++] = o;
}

static void free_obj(Obj* o) {
    if (o->type == O_LIST) free(((ObjList*)o)->items);
    if (o->type == O_DICT) { free(((ObjDict*)o)->entries); free(((ObjDict*)o)->index); }
    free(o);
}

static void collect(VM* vm) {
    for (int i = 0; i < vm->sp; i++) mark_value(vm, vm->stack[i]);
    for (int i = 0; i < vm->p->nstatics; i++) mark_value(vm, vm->statics[i]);
    for (int i = 0; i < vm->ncoros; i++) { Value v; v.t = V_OBJ; v.as.o = (Obj*)vm->coros[i]; mark_value(vm, v); }
    for (int i = 0; i < vm->fp; i++)
        if (vm->frames[i].clo) { Value v; v.t = V_OBJ; v.as.o = vm->frames[i].clo; mark_value(vm, v); }
    while (vm->ngray > 0) {
        Obj* o = vm->gray[--vm->ngray];
        if (o->type == O_ARRAY) {
            ObjArray* a = (ObjArray*)o;
            for (int i = 0; i < a->len; i++) mark_value(vm, a->items[i]);
        } else if (o->type == O_LIST) {
            ObjList* l = (ObjList*)o;
            for (int i = 0; i < l->len; i++) mark_value(vm, l->items[i]);
        } else if (o->type == O_DICT) {
            ObjDict* d = (ObjDict*)o;
            for (int i = 0; i < d->used; i++)
                if (d->entries[i].live) { mark_value(vm, d->entries[i].key); mark_value(vm, d->entries[i].val); }
        } else if (o->type == O_CELL) {
            mark_value(vm, ((ObjCell*)o)->v);
        } else if (o->type == O_CLOSURE) {
            ObjClosure* cl = (ObjClosure*)o;
            mark_value(vm, cl->self);
            for (int i = 0; i < cl->nup; i++) mark_value(vm, cl->ups[i]);
        } else if (o->type == O_CORO) {
            ObjCoro* co = (ObjCoro*)o;
            for (int i = 0; i < co->nlocals; i++) mark_value(vm, co->locals[i]);
            if (co->waitFor) { Value w; w.t = V_OBJ; w.as.o = co->waitFor; mark_value(vm, w); }
            if (co->owner) { Value w; w.t = V_OBJ; w.as.o = co->owner; mark_value(vm, w); }
        } else {
            ObjInstance* in = (ObjInstance*)o;
            for (int i = 0; i < in->nfields; i++) mark_value(vm, in->fields[i]);
        }
    }
    Obj** link = &vm->objects;
    while (*link) {
        Obj* o = *link;
        if (o->marked) { o->marked = 0; link = &o->next; }
        else { *link = o->next; vm->bytes -= obj_size(o); free_obj(o); }
    }
    vm->nextGC = vm->bytes * 2;
    if (vm->nextGC < 1024 * 1024) vm->nextGC = 1024 * 1024;
}

static Obj* alloc_obj(VM* vm, size_t size, int type) {
    if (vm->bytes + size > vm->nextGC) collect(vm);
    Obj* o = (Obj*)malloc(size);
    if (!o) { collect(vm); o = (Obj*)malloc(size); if (!o) return NULL; }
    o->type = (uint8_t)type; o->marked = 0; o->pinned = 0;
    o->next = vm->objects; vm->objects = o;
    vm->bytes += size;
    return o;
}

static ObjString* new_string(VM* vm, int len) {
    ObjString* s = (ObjString*)alloc_obj(vm, sizeof(ObjString) + len + 1, O_STRING);
    if (s) { s->len = len; s->chars[len] = 0; }
    return s;
}

Value vm_make_string(VM* vm, const char* str, int len) {
    Value v; v.t = V_NULL; v.as.o = NULL;
    ObjString* s = new_string(vm, len);
    if (!s) return v;
    memcpy(s->chars, str, len);
    v.t = V_OBJ; v.as.o = (Obj*)s;
    return v;
}

Value vm_make_array(VM* vm, int len) {
    Value v; v.t = V_NULL; v.as.o = NULL;
    ObjArray* a = (ObjArray*)alloc_obj(vm, sizeof(ObjArray) + sizeof(Value) * len, O_ARRAY);
    if (!a) return v;
    a->len = len;
    for (int i = 0; i < len; i++) { a->items[i].t = V_NULL; a->items[i].as.o = NULL; }
    v.t = V_OBJ; v.as.o = (Obj*)a;
    return v;
}

/* makes room for one more item; 0 = out of memory */
static int list_reserve(VM* vm, ObjList* l) {
    if (l->len < l->cap) return 1;
    int nc = l->cap ? l->cap * 2 : 8;
    Value* items = (Value*)realloc(l->items, sizeof(Value) * nc);
    if (!items) return 0;
    vm->bytes += sizeof(Value) * (size_t)(nc - l->cap);
    l->items = items; l->cap = nc;
    return 1;
}

/* ================= helpers ================= */
static void fmt_float(float f, char* buf, int n) {
    if (isnan(f)) { snprintf(buf, n, "NaN"); return; }
    if (isinf(f)) { snprintf(buf, n, f > 0 ? "Infinity" : "-Infinity"); return; }
    snprintf(buf, n, "%.7g", (double)f);
}

void ms_value_to_text(VM* vm, Value v, char* buf, int n) {
    switch (v.t) {
        case V_NULL: buf[0] = 0; return;
        case V_INT: snprintf(buf, n, "%d", (int)v.as.i); return;
        case V_FLOAT: fmt_float(v.as.f, buf, n); return;
        case V_BOOL: snprintf(buf, n, v.as.i ? "True" : "False"); return;
        case V_CHAR: buf[0] = (char)v.as.i; buf[1] = 0; return;
        default: {
            Obj* o = v.as.o;
            if (o->type == O_STRING) snprintf(buf, n, "%s", ((ObjString*)o)->chars);
            else if (o->type == O_ARRAY) snprintf(buf, n, "Array[%d]", (int)((ObjArray*)o)->len);
            else if (o->type == O_LIST) snprintf(buf, n, "List[%d]", (int)((ObjList*)o)->len);
            else if (o->type == O_DICT) snprintf(buf, n, "Dictionary[%d]", (int)((ObjDict*)o)->count);
            else if (o->type == O_CORO) snprintf(buf, n, "Coroutine");
            else if (o->type == O_CLOSURE) snprintf(buf, n, "Delegate");
            else snprintf(buf, n, "%s", vm->p->classes[((ObjInstance*)o)->cls].name);
        }
    }
}

/* ================= VM ================= */
VM* vm_new(Program* p) {
    VM* vm = (VM*)calloc(1, sizeof(VM));
    if (!vm) return NULL;
    vm->p = p;
    vm->stack = (Value*)calloc(MS_STACK, sizeof(Value));
    vm->statics = (Value*)calloc(p->nstatics ? p->nstatics : 1, sizeof(Value));
    if (!vm->stack || !vm->statics) { vm_free(vm); return NULL; }
    for (int i = 0; i < p->nstatics; i++) vm->statics[i] = p->staticDefaults[i];
    vm->nextGC = 1024 * 1024;
    return vm;
}

int vm_set_program(VM* vm, Program* p) {
    int old = vm->p->nstatics;
    if (p->nstatics > old) {
        Value* s = (Value*)realloc(vm->statics, sizeof(Value) * p->nstatics);
        if (!s) return 0;
        vm->statics = s;
        for (int i = old; i < p->nstatics; i++) s[i] = p->staticDefaults[i];
    }
    vm->p = p;
    return 1;
}

void vm_free(VM* vm) {
    if (!vm) return;
    Obj* o = vm->objects;
    while (o) { Obj* n = o->next; free_obj(o); o = n; }
    free(vm->gray); free(vm->stack); free(vm->statics); free(vm->coros);
    free(vm);
}

static int runtime_error(VM* vm, Frame* fr, int ip, const char* msg) {
    int line = (ip > 0 && ip <= fr->f->len) ? fr->f->lines[ip - 1] : 0;
    snprintf(vm->err, sizeof vm->err, "line %d (%.63s): %.170s", line, fr->f->name, msg);
    return 0;
}

#define STR(v) ((ObjString*)(v).as.o)
#define ARR(v) ((ObjArray*)(v).as.o)
#define INST(v) ((ObjInstance*)(v).as.o)

static int str_equal(Value a, Value b) {
    if (a.t == V_NULL || b.t == V_NULL) return a.t == b.t;
    ObjString* x = STR(a); ObjString* y = STR(b);
    return x == y || (x->len == y->len && !memcmp(x->chars, y->chars, x->len));
}

/* List.IndexOf/Remove: strings by content, objects by reference, numbers by value */
static int value_equal(Value a, Value b) {
    if (a.t == V_NULL || b.t == V_NULL) return a.t == b.t;
    if (a.t == V_OBJ && b.t == V_OBJ) {
        if (a.as.o->type == O_STRING && b.as.o->type == O_STRING) return str_equal(a, b);
        return a.as.o == b.as.o;
    }
    if (a.t == V_FLOAT) return b.t == V_FLOAT && a.as.f == b.as.f;
    return a.as.i == b.as.i;
}

static int list_index_of(ObjList* l, Value v) {
    for (int i = 0; i < l->len; i++) if (value_equal(l->items[i], v)) return i;
    return -1;
}

/* ================= Dictionary ================= */
static uint32_t hash_value(Value v) {
    if (v.t == V_OBJ) {
        if (v.as.o->type == O_STRING) {         /* FNV-1a over the characters */
            ObjString* s = STR(v); uint32_t h = 2166136261u;
            for (int i = 0; i < s->len; i++) { h ^= (unsigned char)s->chars[i]; h *= 16777619u; }
            return h;
        }
        uintptr_t p = (uintptr_t)v.as.o;
        return (uint32_t)(p >> 3) * 2654435761u;
    }
    if (v.t == V_NULL) return 0;
    uint32_t bits = (uint32_t)v.as.i;           /* also the float's bits */
    return bits * 2654435761u ^ (bits >> 15);
}

static int dict_find(ObjDict* d, Value key, uint32_t h) {
    if (!d->icap) return -1;
    for (uint32_t i = h & (uint32_t)(d->icap - 1);; i = (i + 1) & (uint32_t)(d->icap - 1)) {
        int e = d->index[i];
        if (e == -1) return -1;
        if (e >= 0 && d->entries[e].hash == h && value_equal(d->entries[e].key, key)) return e;
    }
}

/* makes room for one more entry: grows the entry array when it is full (entries never move, so
 * the order is kept) and rebuilds the index when it gets too full of entries or removed marks */
static int dict_reserve(VM* vm, ObjDict* d) {
    int needEntry = d->freeCount == 0 && d->used == d->cap;
    int filled = d->used - d->freeCount + d->ntomb + 1;          /* index slots in use after this Add */
    if (!needEntry && filled * 4 <= d->icap * 3) return 1;
    int cap = d->cap;
    if (needEntry) cap = cap ? cap * 2 : 8;
    int icap = 16;
    while (icap < cap * 2) icap *= 2;
    DictEntry* ne = cap == d->cap ? d->entries : (DictEntry*)realloc(d->entries, sizeof(DictEntry) * (size_t)cap);
    if (!ne) return 0;
    vm->bytes += sizeof(DictEntry) * (size_t)(cap - d->cap);
    d->entries = ne; d->cap = cap;
    int32_t* ni = (int32_t*)malloc(sizeof(int32_t) * (size_t)icap);
    if (!ni) return 0;
    for (int i = 0; i < icap; i++) ni[i] = -1;
    for (int e = 0; e < d->used; e++) {
        if (!ne[e].live) continue;
        uint32_t i = ne[e].hash & (uint32_t)(icap - 1);
        while (ni[i] != -1) i = (i + 1) & (uint32_t)(icap - 1);
        ni[i] = e;
    }
    vm->bytes += sizeof(int32_t) * (size_t)icap;
    vm->bytes -= sizeof(int32_t) * (size_t)d->icap;
    free(d->index);
    d->index = ni; d->icap = icap; d->ntomb = 0;
    return 1;
}

/* 1 = stored, 0 = out of memory, -1 = key exists and onlyNew was set */
static int dict_put(VM* vm, ObjDict* d, Value key, Value val, int onlyNew) {
    uint32_t h = hash_value(key);
    int e = dict_find(d, key, h);
    if (e >= 0) { if (onlyNew) return -1; d->entries[e].val = val; return 1; }
    if (!dict_reserve(vm, d)) return 0;
    uint32_t i = h & (uint32_t)(d->icap - 1);
    while (d->index[i] >= 0) i = (i + 1) & (uint32_t)(d->icap - 1);
    if (d->index[i] == -2) d->ntomb--;
    if (d->freeCount > 0) { e = d->freeList; d->freeList = (int32_t)d->entries[e].hash; d->freeCount--; }   /* like .NET */
    else e = d->used++;
    d->entries[e].key = key; d->entries[e].val = val; d->entries[e].hash = h; d->entries[e].live = 1;
    d->index[i] = e;
    d->count++;
    return 1;
}

static int dict_remove(ObjDict* d, Value key) {
    uint32_t h = hash_value(key);
    if (!d->icap) return 0;
    for (uint32_t i = h & (uint32_t)(d->icap - 1);; i = (i + 1) & (uint32_t)(d->icap - 1)) {
        int e = d->index[i];
        if (e == -1) return 0;
        if (e >= 0 && d->entries[e].hash == h && value_equal(d->entries[e].key, key)) {
            d->index[i] = -2; d->ntomb++;
            d->entries[e].live = 0; d->count--;
            d->entries[e].key.t = V_NULL; d->entries[e].val.t = V_NULL;
            d->entries[e].hash = (uint32_t)d->freeList; d->freeList = e; d->freeCount++;
            return 1;
        }
    }
}

static ObjList* new_list(VM* vm, int n) {
    ObjList* l = (ObjList*)alloc_obj(vm, sizeof(ObjList), O_LIST);
    if (!l) return NULL;
    l->len = 0; l->cap = 0; l->items = NULL;
    if (n > 0) {
        l->items = (Value*)malloc(sizeof(Value) * (size_t)n);
        if (!l->items) return NULL;             /* the empty list is collected later */
        l->cap = n; vm->bytes += sizeof(Value) * (size_t)n;
    }
    return l;
}

static int vm_run(VM* vm, int startFp);

int vm_call(VM* vm, int func) {
    Program* P = vm->p;
    if (func < 0 || func >= P->nfuncs) return 1;
    int startFp = vm->fp;
    if (vm->fp >= MS_FRAMES) { snprintf(vm->err, sizeof vm->err, "call stack overflow"); return 0; }
    Frame* fr = &vm->frames[vm->fp++];
    fr->f = &P->funcs[func]; fr->ip = 0; fr->base = vm->sp; fr->clo = NULL;
    for (int i = 0; i < fr->f->nlocals; i++) { vm->stack[vm->sp + i].t = V_NULL; vm->stack[vm->sp + i].as.o = NULL; }
    vm->sp += fr->f->nlocals;
    return vm_run(vm, startFp);
}

/* ================= coroutines ================= */
/* StopCoroutine: also stops the nested coroutine it started with 'yield return Other()', like Unity */
static void coro_stop(ObjCoro* co) {
    while (co && !co->done) {
        co->done = 1;
        ObjCoro* child = co->waitKind == Y_CORO ? (ObjCoro*)co->waitFor : NULL;
        co = child && child->owner == (Obj*)co ? child : NULL;
    }
}

/* Runs a coroutine until its next yield or its end. 0 = runtime error or quit. */
static int coro_resume(VM* vm, ObjCoro* co) {
    Func* f = &vm->p->funcs[co->func];
    if (vm->fp >= MS_FRAMES || vm->sp + co->nlocals + 512 >= MS_STACK) { snprintf(vm->err, sizeof vm->err, "stack overflow (coroutine)"); return 0; }
    int startFp = vm->fp;
    Frame* fr = &vm->frames[vm->fp++];
    fr->f = f; fr->ip = co->ip; fr->base = vm->sp; fr->clo = NULL;
    memcpy(vm->stack + vm->sp, co->locals, sizeof(Value) * (size_t)co->nlocals);
    vm->sp += co->nlocals;
    ObjCoro* prev = vm->curCoro;
    vm->curCoro = co; co->running = 1;
    int r = vm_run(vm, startFp);
    vm->curCoro = prev; co->running = 0;
    if (r == 0) return 0;
    if (r == 1) {                       /* reached the end or 'yield break' */
        co->done = 1;
        /* like Unity, a coroutine waiting for this one continues in the same frame */
        for (int i = 0; i < vm->ncoros; i++) {
            ObjCoro* w = vm->coros[i];
            if (w->waitKind == Y_CORO && w->waitFor == (Obj*)co && !w->done && !w->running) {
                w->waitFor = NULL; w->waitKind = Y_NEXT; w->lastFrame = -1;
                if (!coro_resume(vm, w)) return 0;
            }
        }
    }
    return 1;
}

/* StartCoroutine: registers it and runs it at once up to the first yield, like Unity */
static int coro_start(VM* vm, ObjCoro* co) {
    if (co->started) return 1;
    co->started = 1;
    if (vm->ncoros == vm->capcoros) {
        int nc = vm->capcoros ? vm->capcoros * 2 : 16;
        ObjCoro** a = (ObjCoro**)realloc(vm->coros, sizeof(ObjCoro*) * nc);
        if (!a) { snprintf(vm->err, sizeof vm->err, "out of memory"); return 0; }
        vm->coros = a; vm->capcoros = nc;
    }
    vm->coros[vm->ncoros++] = co;
    return coro_resume(vm, co);
}

int vm_tick_coroutines(VM* vm) {
    int now = plat_millis();
    int n = vm->ncoros;                 /* ones started during this tick already ran */
    for (int i = 0; i < n; i++) {
        ObjCoro* co = vm->coros[i];
        if (co->done || co->running || co->lastFrame == vm->frameNo) continue;
        if (co->waitKind == Y_SECONDS && now < co->waitUntil) continue;
        if (co->waitKind == Y_CORO && co->waitFor && !((ObjCoro*)co->waitFor)->done) continue;
        if (!coro_resume(vm, co)) return 0;
    }
    int k = 0;
    for (int i = 0; i < vm->ncoros; i++) if (!vm->coros[i]->done) vm->coros[k++] = vm->coros[i];
    vm->ncoros = k;
    vm->frameNo++;
    return 1;
}

/* Calling an iterator method: the arguments on the stack become a new coroutine. 0 = out of memory */
static int coro_new(VM* vm, int fidx, int argc, Value** spp) {
    Value* sp = *spp;
    int nl = vm->p->funcs[fidx].nlocals;
    ObjCoro* co = (ObjCoro*)alloc_obj(vm, sizeof(ObjCoro) + sizeof(Value) * nl, O_CORO);
    if (!co) return 0;
    co->func = fidx; co->ip = 0; co->nlocals = nl;
    co->started = co->done = co->running = 0; co->waitKind = Y_NEXT;
    co->waitUntil = 0; co->lastFrame = -1; co->waitFor = NULL; co->owner = NULL;
    for (int i = 0; i < nl; i++) {
        if (i < argc) co->locals[i] = sp[i - argc];
        else { co->locals[i].t = V_NULL; co->locals[i].as.o = NULL; }
    }
    sp -= argc;
    sp->t = V_OBJ; sp->as.o = (Obj*)co; sp++;
    *spp = sp;
    return 1;
}

/* Runs from the top frame until the frame at startFp returns (1), yields (2) or fails (0). */
static int vm_run(VM* vm, int startFp) {
    Program* P = vm->p;
    Frame* fr = &vm->frames[vm->fp - 1];
    Value* stack = vm->stack;
    Value* sp = stack + vm->sp;
    Value* bp = stack + fr->base;
    int32_t* code = fr->f->code;
    int ip = fr->ip;

#define SYNC() (vm->sp = (int)(sp - stack), fr->ip = ip)
#define FAIL(msg) do { SYNC(); runtime_error(vm, fr, ip, msg); vm->fp = startFp; return 0; } while (0)
#define FAILED() do { vm->fp = startFp; return 0; } while (0)   /* vm->err is already set (or quit) */
#define ARG() (code[ip++])

    for (;;) {
        int o = code[ip++];
        switch (o) {
            case OP_ICONST: sp->t = V_INT; sp->as.i = ARG(); sp++; break;
            case OP_FCONST: { int32_t b = ARG(); sp->t = V_FLOAT; memcpy(&sp->as.f, &b, 4); sp++; break; }
            case OP_SCONST: sp->t = V_OBJ; sp->as.o = (Obj*)P->strings[ARG()]; sp++; break;
            case OP_NULL: sp->t = V_NULL; sp->as.o = NULL; sp++; break;
            case OP_TRUE: sp->t = V_BOOL; sp->as.i = 1; sp++; break;
            case OP_FALSE: sp->t = V_BOOL; sp->as.i = 0; sp++; break;
            case OP_POP: sp--; break;
            case OP_DUP: sp[0] = sp[-1]; sp++; break;
            case OP_DUP2: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; break;
            case OP_LOAD: *sp++ = bp[ARG()]; break;
            case OP_STORE: bp[ARG()] = sp[-1]; break;
            case OP_STOREP: bp[ARG()] = *--sp; break;
            case OP_GETSTATIC: *sp++ = vm->statics[ARG()]; break;
            case OP_SETSTATIC: vm->statics[ARG()] = sp[-1]; break;
            case OP_GETFIELD: {
                int idx = ARG();
                if (sp[-1].t != V_OBJ) FAIL("null reference (reading a field of null)");
                sp[-1] = INST(sp[-1])->fields[idx];
                break;
            }
            case OP_SETFIELD: {
                int idx = ARG();
                if (sp[-2].t != V_OBJ) FAIL("null reference (setting a field of null)");
                INST(sp[-2])->fields[idx] = sp[-1];
                sp[-2] = sp[-1]; sp--;
                break;
            }
            case OP_GETELEM: {            /* array, List, Dictionary or string (gives a char) */
                if (sp[-2].t != V_OBJ) FAIL("null reference (indexing null)");
                Obj* ob = sp[-2].as.o; int i = sp[-1].as.i;
                if (ob->type == O_DICT) {
                    int e = dict_find((ObjDict*)ob, sp[-1], hash_value(sp[-1]));
                    if (e < 0) {
                        char k[64], m[200]; ms_value_to_text(vm, sp[-1], k, sizeof k);
                        snprintf(m, sizeof m, "key '%s' is not in the dictionary (use ContainsKey or TryGetValue)", k); FAIL(m);
                    }
                    sp[-2] = ((ObjDict*)ob)->entries[e].val; sp--;
                    break;
                }
                int len = ob->type == O_ARRAY ? ((ObjArray*)ob)->len : ob->type == O_LIST ? ((ObjList*)ob)->len : ((ObjString*)ob)->len;
                if (i < 0 || i >= len) {
                    char m[80]; snprintf(m, sizeof m, "index %d is outside the %s (length %d)", i,
                                         ob->type == O_ARRAY ? "array" : ob->type == O_LIST ? "list" : "string", len); FAIL(m);
                }
                if (ob->type == O_ARRAY) sp[-2] = ((ObjArray*)ob)->items[i];
                else if (ob->type == O_LIST) sp[-2] = ((ObjList*)ob)->items[i];
                else { sp[-2].t = V_CHAR; sp[-2].as.i = (unsigned char)((ObjString*)ob)->chars[i]; }
                sp--;
                break;
            }
            case OP_SETELEM: {            /* array, List or Dictionary */
                if (sp[-3].t != V_OBJ) FAIL("null reference (indexing null)");
                Obj* ob = sp[-3].as.o; int i = sp[-2].as.i;
                if (ob->type == O_DICT) {
                    if (!dict_put(vm, (ObjDict*)ob, sp[-2], sp[-1], 0)) FAIL("out of memory");
                    sp[-3] = sp[-1]; sp -= 2;
                    break;
                }
                int len = ob->type == O_ARRAY ? ((ObjArray*)ob)->len : ((ObjList*)ob)->len;
                if (i < 0 || i >= len) {
                    char m[80]; snprintf(m, sizeof m, "index %d is outside the %s (length %d)", i, ob->type == O_ARRAY ? "array" : "list", len); FAIL(m);
                }
                if (ob->type == O_ARRAY) ((ObjArray*)ob)->items[i] = sp[-1]; else ((ObjList*)ob)->items[i] = sp[-1];
                sp[-3] = sp[-1]; sp -= 2;
                break;
            }
            case OP_IADD: sp[-2].as.i = (int32_t)((uint32_t)sp[-2].as.i + (uint32_t)sp[-1].as.i); sp--; break;
            case OP_ISUB: sp[-2].as.i = (int32_t)((uint32_t)sp[-2].as.i - (uint32_t)sp[-1].as.i); sp--; break;
            case OP_IMUL: sp[-2].as.i = (int32_t)((uint32_t)sp[-2].as.i * (uint32_t)sp[-1].as.i); sp--; break;
            case OP_IDIV:
                if (sp[-1].as.i == 0) FAIL("division by zero");
                if (sp[-1].as.i == -1) sp[-2].as.i = (int32_t)(0u - (uint32_t)sp[-2].as.i);
                else sp[-2].as.i /= sp[-1].as.i;
                sp--; break;
            case OP_IMOD:
                if (sp[-1].as.i == 0) FAIL("division by zero");
                if (sp[-1].as.i == -1) sp[-2].as.i = 0; else sp[-2].as.i %= sp[-1].as.i;
                sp--; break;
            case OP_INEG: sp[-1].as.i = (int32_t)(0u - (uint32_t)sp[-1].as.i); break;
            case OP_FADD: sp[-2].as.f += sp[-1].as.f; sp--; break;
            case OP_FSUB: sp[-2].as.f -= sp[-1].as.f; sp--; break;
            case OP_FMUL: sp[-2].as.f *= sp[-1].as.f; sp--; break;
            case OP_FDIV: sp[-2].as.f /= sp[-1].as.f; sp--; break;
            case OP_FMOD: sp[-2].as.f = fmodf(sp[-2].as.f, sp[-1].as.f); sp--; break;
            case OP_FNEG: sp[-1].as.f = -sp[-1].as.f; break;
            case OP_I2F: { Value* v = sp - 1 - ARG(); v->t = V_FLOAT; v->as.f = (float)v->as.i; break; }
            case OP_F2I: {
                float f = sp[-1].as.f; int32_t r;
                if (f != f) r = 0; else if (f >= 2147483647.0f) r = 2147483647; else if (f <= -2147483648.0f) r = (-2147483647 - 1); else r = (int32_t)f;
                sp[-1].t = V_INT; sp[-1].as.i = r; break;
            }
            case OP_TOSTR: {               /* kind 4: an object's own class name, without its ToString override (base.ToString()) */
                int kind = ARG(); int depth = ARG();
                Value* v = sp - 1 - depth;
                char buf[96];
                if (kind == 3 && v->t == V_OBJ && v->as.o->type == O_STRING) break;
                if (kind == 3 && v->t == V_OBJ && v->as.o->type == O_INSTANCE && P->classes[INST(*v)->cls].toString >= 0) {
                    /* override string ToString(): run it here, nested, and put its result in place of the object */
                    int f = P->classes[INST(*v)->cls].toString;
                    *sp = *v;
                    vm->sp = (int)(sp - stack) + 1; fr->ip = ip;
                    if (vm->fp >= MS_FRAMES) FAIL("stack overflow (too much recursion?)");
                    int top = vm->fp;
                    Frame* nf = &vm->frames[vm->fp++];
                    nf->f = &P->funcs[f]; nf->ip = 0; nf->base = vm->sp - 1; nf->clo = NULL;
                    for (int i = 1; i < nf->f->nlocals; i++) { stack[nf->base + i].t = V_NULL; stack[nf->base + i].as.o = NULL; }
                    vm->sp = nf->base + nf->f->nlocals;
                    if (vm->sp + 512 >= MS_STACK) FAIL("stack overflow (too much recursion?)");
                    int r = vm_run(vm, top);
                    if (r != 1) { vm->fp = startFp; return 0; }
                    *v = stack[vm->sp];
                    sp = stack + vm->sp;
                    break;
                }
                ms_value_to_text(vm, *v, buf, sizeof buf);
                SYNC();
                Value s = vm_make_string(vm, buf, (int)strlen(buf));
                if (s.t == V_NULL) FAIL("out of memory");
                *v = s;
                break;
            }
            case OP_CONCAT: {
                ObjString* a = sp[-2].t == V_OBJ ? STR(sp[-2]) : NULL;
                ObjString* b = sp[-1].t == V_OBJ ? STR(sp[-1]) : NULL;
                int la = a ? a->len : 0, lb = b ? b->len : 0;
                SYNC();
                ObjString* r = new_string(vm, la + lb);
                if (!r) FAIL("out of memory");
                if (la) memcpy(r->chars, a->chars, la);
                if (lb) memcpy(r->chars + la, b->chars, lb);
                sp[-2].t = V_OBJ; sp[-2].as.o = (Obj*)r; sp--;
                break;
            }
#define ICMP(OPC, X) case OPC: sp[-2].as.i = (sp[-2].as.i X sp[-1].as.i); sp[-2].t = V_BOOL; sp--; break;
#define FCMP(OPC, X) case OPC: sp[-2].as.i = (sp[-2].as.f X sp[-1].as.f); sp[-2].t = V_BOOL; sp--; break;
            ICMP(OP_ILT, <) ICMP(OP_ILE, <=) ICMP(OP_IGT, >) ICMP(OP_IGE, >=) ICMP(OP_IEQ, ==) ICMP(OP_INE, !=)
            FCMP(OP_FLT, <) FCMP(OP_FLE, <=) FCMP(OP_FGT, >) FCMP(OP_FGE, >=) FCMP(OP_FEQ, ==) FCMP(OP_FNE, !=)
            case OP_SEQ: case OP_SNE: {
                int eq = str_equal(sp[-2], sp[-1]);
                sp[-2].t = V_BOOL; sp[-2].as.i = (o == OP_SEQ) ? eq : !eq; sp--;
                break;
            }
            case OP_REQ: case OP_RNE: {
                int eq = (sp[-2].t == sp[-1].t) && (sp[-2].t == V_NULL || sp[-2].as.o == sp[-1].as.o);
                sp[-2].t = V_BOOL; sp[-2].as.i = (o == OP_REQ) ? eq : !eq; sp--;
                break;
            }
            case OP_NOT: sp[-1].as.i = !sp[-1].as.i; break;
#define LOOP_CHECK() if (off < 0 && vm->deadline && !(++vm->loops & 0xFFFF) && time(NULL) > vm->deadline) \
                         FAIL("stopped: still running after 5 seconds (endless loop?)")
            case OP_JMP: { int off = ARG(); ip += off; LOOP_CHECK(); break; }
            case OP_JZ: { int off = ARG(); if (!(--sp)->as.i) { ip += off; LOOP_CHECK(); } break; }
            case OP_JFALSE_KEEP: { int off = ARG(); if (!sp[-1].as.i) ip += off; else sp--; break; }
            case OP_JTRUE_KEEP: { int off = ARG(); if (sp[-1].as.i) ip += off; else sp--; break; }
            case OP_NULLCHK: { int d = ARG(); if (sp[-1 - d].t != V_OBJ) FAIL("null reference (calling a method on null)"); break; }
            case OP_CALLV: {               /* virtual call: the object (first argument) picks the function */
                int sel = ARG(); int argc = ARG();
                Value self = sp[-argc];
                if (self.t != V_OBJ) FAIL("null reference (calling a method on null)");
                int fidx = P->classes[INST(self)->cls].vtable[sel];
                if (fidx < 0) FAIL("internal error: missing virtual method");
                if (P->funcs[fidx].isIter) { SYNC(); if (!coro_new(vm, fidx, argc, &sp)) FAIL("out of memory"); break; }
                Func* nf = &P->funcs[fidx];
                SYNC();
                if (vm->fp >= MS_FRAMES) FAIL("stack overflow (too much recursion?)");
                int base = (int)(sp - stack) - argc;
                if (base + nf->nlocals + 512 >= MS_STACK) FAIL("stack overflow (too much recursion?)");
                for (int i = argc; i < nf->nlocals; i++) { stack[base + i].t = V_NULL; stack[base + i].as.o = NULL; }
                fr = &vm->frames[vm->fp++];
                fr->f = nf; fr->ip = 0; fr->base = base; fr->clo = NULL;
                sp = stack + base + nf->nlocals;
                bp = stack + base;
                code = nf->code; ip = 0;
                break;
            }
            case OP_BOX: {
                SYNC();
                ObjCell* cell = (ObjCell*)alloc_obj(vm, sizeof(ObjCell), O_CELL);
                if (!cell) FAIL("out of memory");
                cell->v = sp[-1];
                sp[-1].t = V_OBJ; sp[-1].as.o = (Obj*)cell;
                break;
            }
            case OP_GETCELL: sp[-1] = ((ObjCell*)sp[-1].as.o)->v; break;
            case OP_SETCELL: ((ObjCell*)sp[-2].as.o)->v = sp[-1]; sp[-2] = sp[-1]; sp--; break;
            case OP_UPVAL: *sp++ = ((ObjClosure*)fr->clo)->ups[ARG()]; break;
            case OP_CLOSURE: {
                int kind = ARG(); int idx = ARG(); int nup = ARG();
                Value self = sp[-nup - 1];
                if (kind == 2) {            /* virtual method: the object picks the function now */
                    if (self.t != V_OBJ) FAIL("null reference (making a delegate of a method of null)");
                    idx = P->classes[INST(self)->cls].vtable[idx];
                }
                SYNC();
                ObjClosure* cl = (ObjClosure*)alloc_obj(vm, sizeof(ObjClosure) + sizeof(Value) * nup, O_CLOSURE);
                if (!cl) FAIL("out of memory");
                cl->func = idx; cl->kind = kind == 1 ? CL_STATIC : CL_SELF; cl->nup = nup;
                cl->self = sp[-nup - 1];
                for (int i = 0; i < nup; i++) cl->ups[i] = sp[i - nup];
                sp -= nup;
                sp[-1].t = V_OBJ; sp[-1].as.o = (Obj*)cl;
                break;
            }
            case OP_CALLF: {
                int argc = ARG();
                Value cv = sp[-argc - 1];
                if (cv.t != V_OBJ) FAIL("null reference (calling a delegate that is null)");
                ObjClosure* cl = (ObjClosure*)cv.as.o;
                int n = argc;
                if (cl->kind == CL_STATIC) { memmove(sp - argc - 1, sp - argc, sizeof(Value) * (size_t)argc); sp--; }
                else { sp[-argc - 1] = cl->self; n = argc + 1; }
                int fidx = cl->func;
                if (P->funcs[fidx].isIter) { SYNC(); if (!coro_new(vm, fidx, n, &sp)) FAIL("out of memory"); break; }
                Func* nf = &P->funcs[fidx];
                SYNC();
                if (vm->fp >= MS_FRAMES) FAIL("stack overflow (too much recursion?)");
                int base = (int)(sp - stack) - n;
                if (base + nf->nlocals + 512 >= MS_STACK) FAIL("stack overflow (too much recursion?)");
                for (int i = n; i < nf->nlocals; i++) { stack[base + i].t = V_NULL; stack[base + i].as.o = NULL; }
                fr = &vm->frames[vm->fp++];
                fr->f = nf; fr->ip = 0; fr->base = base; fr->clo = (Obj*)cl;
                sp = stack + base + nf->nlocals;
                bp = stack + base;
                code = nf->code; ip = 0;
                break;
            }
            case OP_ISINST: {
                int ci = ARG(); int mode = ARG();
                Value v = sp[-1];
                int ok = v.t == V_OBJ && v.as.o->type == O_INSTANCE && P->classes[INST(v)->cls].isa[ci];
                if (mode == 0) { sp[-1].t = V_BOOL; sp[-1].as.i = ok; break; }
                if (ok || v.t == V_NULL) break;
                if (mode == 1) { sp[-1].t = V_NULL; sp[-1].as.o = NULL; break; }
                char m[160]; snprintf(m, sizeof m, "invalid cast: this %s is not a %s", P->classes[INST(v)->cls].name, P->classes[ci].name);
                FAIL(m);
            }
            case OP_CALL: {
                int fidx = ARG(); int argc = ARG();
                Func* nf = &P->funcs[fidx];
                SYNC();
                if (vm->fp >= MS_FRAMES) FAIL("stack overflow (too much recursion?)");
                int base = (int)(sp - stack) - argc;
                if (base + nf->nlocals + 512 >= MS_STACK) FAIL("stack overflow (too much recursion?)");
                for (int i = argc; i < nf->nlocals; i++) { stack[base + i].t = V_NULL; stack[base + i].as.o = NULL; }
                fr = &vm->frames[vm->fp++];
                fr->f = nf; fr->ip = 0; fr->base = base; fr->clo = NULL;
                sp = stack + base + nf->nlocals;
                bp = stack + base;
                code = nf->code; ip = 0;
                break;
            }
            case OP_CALLN: {
                int ni = ARG(); int argc = ARG();
                SYNC();
                Value* args = sp - argc;
                Value r = ms_natives[ni].fn(vm, args);
                sp -= argc;
                if (vm->err[0]) { char m[256]; snprintf(m, sizeof m, "%s", vm->err); FAIL(m); }
                if (vm->quit) { vm->fp = startFp; vm->sp = (int)(sp - stack); return 0; }
                if (ms_natives[ni].ret != T_VOID) *sp++ = r;
                break;
            }
            case OP_RET: case OP_RETV: {
                Value r; r.t = V_NULL; r.as.o = NULL;
                if (o == OP_RETV) r = sp[-1];
                int base = fr->base;
                vm->fp--;
                sp = stack + base;
                if (o == OP_RETV) *sp++ = r;
                if (vm->fp == startFp) { vm->sp = (int)(sp - stack); if (o == OP_RETV) vm->sp--; return 1; }
                fr = &vm->frames[vm->fp - 1];
                code = fr->f->code; ip = fr->ip; bp = stack + fr->base;
                break;
            }
            case OP_NEW: {
                int ci = ARG();
                ClassRT* k = &P->classes[ci];
                SYNC();
                ObjInstance* in = (ObjInstance*)alloc_obj(vm, sizeof(ObjInstance) + sizeof(Value) * k->nfields, O_INSTANCE);
                if (!in) FAIL("out of memory");
                in->cls = ci; in->nfields = k->nfields;
                for (int i = 0; i < k->nfields; i++) in->fields[i] = k->defaults[i];
                sp->t = V_OBJ; sp->as.o = (Obj*)in; sp++;
                break;
            }
            case OP_NEWARR: {
                int kind = ARG();
                int n = sp[-1].as.i;
                if (n < 0) FAIL("array size cannot be negative");
                if (n > 4 * 1024 * 1024) FAIL("array too big");
                SYNC();
                ObjArray* a = (ObjArray*)alloc_obj(vm, sizeof(ObjArray) + sizeof(Value) * n, O_ARRAY);
                if (!a) FAIL("out of memory");
                a->len = n;
                Value d; d.as.i = 0;
                d.t = kind == 1 ? V_INT : kind == 2 ? V_FLOAT : kind == 3 ? V_BOOL : kind == 4 ? V_CHAR : V_NULL;
                if (kind == 2) d.as.f = 0.0f;
                for (int i = 0; i < n; i++) a->items[i] = d;
                sp[-1].t = V_OBJ; sp[-1].as.o = (Obj*)a;
                break;
            }
            case OP_ARRLEN: {             /* array.Length, list.Count or string.Length */
                if (sp[-1].t != V_OBJ) FAIL("null reference (Length/Count of null)");
                Obj* ob = sp[-1].as.o;
                sp[-1].t = V_INT;
                sp[-1].as.i = ob->type == O_ARRAY ? ((ObjArray*)ob)->len : ob->type == O_LIST ? ((ObjList*)ob)->len :
                              ob->type == O_DICT ? ((ObjDict*)ob)->count : ((ObjString*)ob)->len;
                break;
            }
            case OP_STRLEN:
                if (sp[-1].t != V_OBJ) FAIL("null reference (Length of a null string)");
                sp[-1].t = V_INT; sp[-1].as.i = STR(sp[-1])->len; break;
            case OP_NORET: FAIL("method ended without returning a value");
            case OP_IAND: sp[-2].as.i &= sp[-1].as.i; sp--; break;
            case OP_IOR: sp[-2].as.i |= sp[-1].as.i; sp--; break;
            case OP_IXOR: sp[-2].as.i ^= sp[-1].as.i; sp--; break;
            case OP_ISHL: sp[-2].as.i = (int32_t)((uint32_t)sp[-2].as.i << (sp[-1].as.i & 31)); sp--; break;
            case OP_ISHR: sp[-2].as.i >>= (sp[-1].as.i & 31); sp--; break;
            case OP_INOT: sp[-1].as.i = ~sp[-1].as.i; break;
            case OP_I2C: sp[-1].t = V_CHAR; sp[-1].as.i &= 0xFFFF; break;
            case OP_C2I: sp[-1 - ARG()].t = V_INT; break;
            case OP_LIST: {
                int sub = ARG();
                if (sub == L_NEW) {
                    SYNC();
                    ObjList* l = (ObjList*)alloc_obj(vm, sizeof(ObjList), O_LIST);
                    if (!l) FAIL("out of memory");
                    l->len = 0; l->cap = 0; l->items = NULL;
                    sp->t = V_OBJ; sp->as.o = (Obj*)l; sp++;
                    break;
                }
                /* stack: list, args...  (the list is checked for null here) */
                int nargs = sub == L_CLEAR ? 0 : sub == L_INSERT ? 2 : 1;
                Value* lv = sp - 1 - nargs;
                if (lv->t != V_OBJ) FAIL("null reference (calling a method on a null list)");
                ObjList* l = (ObjList*)lv->as.o;
                switch (sub) {
                    case L_ADD:
                        if (!list_reserve(vm, l)) FAIL("out of memory");
                        l->items[l->len++] = sp[-1];
                        sp -= 2;
                        break;
                    case L_INSERT: {
                        int i = sp[-2].as.i;
                        if (i < 0 || i > l->len) { char m[80]; snprintf(m, sizeof m, "Insert: index %d is outside the list (length %d)", i, (int)l->len); FAIL(m); }
                        if (!list_reserve(vm, l)) FAIL("out of memory");
                        memmove(l->items + i + 1, l->items + i, sizeof(Value) * (size_t)(l->len - i));
                        l->items[i] = sp[-1]; l->len++;
                        sp -= 3;
                        break;
                    }
                    case L_REMOVEAT: {
                        int i = sp[-1].as.i;
                        if (i < 0 || i >= l->len) { char m[80]; snprintf(m, sizeof m, "RemoveAt: index %d is outside the list (length %d)", i, (int)l->len); FAIL(m); }
                        memmove(l->items + i, l->items + i + 1, sizeof(Value) * (size_t)(l->len - i - 1));
                        l->len--;
                        sp -= 2;
                        break;
                    }
                    case L_REMOVE: {
                        int i = list_index_of(l, sp[-1]);
                        if (i >= 0) { memmove(l->items + i, l->items + i + 1, sizeof(Value) * (size_t)(l->len - i - 1)); l->len--; }
                        sp[-2].t = V_BOOL; sp[-2].as.i = i >= 0; sp--;
                        break;
                    }
                    case L_CLEAR: l->len = 0; sp--; break;
                    case L_ADDRANGE: {            /* list, array-or-list: appends a copy of every item */
                        if (sp[-1].t != V_OBJ) FAIL("AddRange(null)");
                        Obj* src = sp[-1].as.o;
                        int n = src->type == O_ARRAY ? ((ObjArray*)src)->len : ((ObjList*)src)->len;
                        Value* items = src->type == O_ARRAY ? ((ObjArray*)src)->items : ((ObjList*)src)->items;
                        for (int i = 0; i < n; i++) {
                            if (!list_reserve(vm, l)) FAIL("out of memory");
                            if (src == (Obj*)l) items = l->items;     /* l.AddRange(l): the buffer may have moved */
                            l->items[l->len++] = items[i];
                        }
                        sp -= 2;
                        break;
                    }
                    case L_INDEXOF: sp[-2].t = V_INT; sp[-2].as.i = list_index_of(l, sp[-1]); sp--; break;
                    default: FAIL("internal error: bad list op");
                }
                break;
            }
            case OP_DICT: {
                int sub = ARG(), kind = sub >> 8;
                sub &= 0xFF;
                if (sub == D_NEW) {
                    SYNC();
                    ObjDict* d = (ObjDict*)alloc_obj(vm, sizeof(ObjDict), O_DICT);
                    if (!d) FAIL("out of memory");
                    d->count = d->used = d->cap = d->icap = d->ntomb = d->freeCount = 0; d->freeList = -1;
                    d->entries = NULL; d->index = NULL;
                    sp->t = V_OBJ; sp->as.o = (Obj*)d; sp++;
                    break;
                }
                int nargs = sub == D_ADD ? 2 : (sub == D_CLEAR || sub == D_KEYS || sub == D_VALUES) ? 0 : 1;
                Value* dv = sp - 1 - nargs;
                if (dv->t != V_OBJ) FAIL("null reference (calling a method on a null dictionary)");
                ObjDict* d = (ObjDict*)dv->as.o;
                switch (sub) {
                    case D_ADD: {
                        int r = dict_put(vm, d, sp[-2], sp[-1], 1);
                        if (r == 0) FAIL("out of memory");
                        if (r < 0) {
                            char k[64], m[200]; ms_value_to_text(vm, sp[-2], k, sizeof k);
                            snprintf(m, sizeof m, "Add: the key '%s' is already in the dictionary (use d[key] = value)", k); FAIL(m);
                        }
                        sp -= 3;
                        break;
                    }
                    case D_REMOVE: { int r = dict_remove(d, sp[-1]); sp[-2].t = V_BOOL; sp[-2].as.i = r; sp--; break; }
                    case D_CONTAINS: { int r = dict_find(d, sp[-1], hash_value(sp[-1])) >= 0; sp[-2].t = V_BOOL; sp[-2].as.i = r; sp--; break; }
                    case D_CONTAINSVAL: {
                        int r = 0;
                        for (int i = 0; i < d->used && !r; i++) r = d->entries[i].live && value_equal(d->entries[i].val, sp[-1]);
                        sp[-2].t = V_BOOL; sp[-2].as.i = r; sp--;
                        break;
                    }
                    case D_CLEAR:
                        for (int i = 0; i < d->icap; i++) d->index[i] = -1;
                        d->count = d->used = d->ntomb = d->freeCount = 0; d->freeList = -1;
                        sp--;
                        break;
                    case D_TRYGET: {            /* leaves: found, value (the value on top) */
                        int e = dict_find(d, sp[-1], hash_value(sp[-1]));
                        sp[-2].t = V_BOOL; sp[-2].as.i = e >= 0;
                        if (e >= 0) sp[-1] = d->entries[e].val;
                        else {
                            sp[-1].as.i = 0;
                            sp[-1].t = kind == 1 ? V_INT : kind == 2 ? V_FLOAT : kind == 3 ? V_BOOL : kind == 4 ? V_CHAR : V_NULL;
                            if (kind == 2) sp[-1].as.f = 0.0f;
                        }
                        break;
                    }
                    case D_KEYS: case D_VALUES: {
                        SYNC();
                        ObjList* l = new_list(vm, d->count);
                        if (!l) FAIL("out of memory");
                        for (int i = 0; i < d->used; i++)
                            if (d->entries[i].live) l->items[l->len++] = sub == D_KEYS ? d->entries[i].key : d->entries[i].val;
                        sp[-1].t = V_OBJ; sp[-1].as.o = (Obj*)l;
                        break;
                    }
                    case D_NEXT: {              /* next live entry at or after i, or -1 */
                        int i = sp[-1].as.i;
                        while (i < d->used && !d->entries[i].live) i++;
                        sp[-2].t = V_INT; sp[-2].as.i = i < d->used ? i : -1; sp--;
                        break;
                    }
                    case D_KEYAT: case D_VALAT: {
                        int i = sp[-1].as.i;
                        if (i < 0 || i >= d->used) FAIL("internal error: bad dictionary position");
                        sp[-2] = sub == D_KEYAT ? d->entries[i].key : d->entries[i].val; sp--;
                        break;
                    }
                    default: FAIL("internal error: bad dictionary op");
                }
                break;
            }
            case OP_CORO_NEW: {
                int fidx = ARG(); int argc = ARG();
                SYNC();
                if (!coro_new(vm, fidx, argc, &sp)) FAIL("out of memory");
                break;
            }
            case OP_YIELD: {
                int kind = ARG();
                ObjCoro* co = vm->curCoro;
                if (!co || vm->fp - 1 != startFp) FAIL("internal error: yield outside a coroutine");
                co->waitKind = (uint8_t)kind; co->waitFor = NULL;
                if (kind == Y_SECONDS) { float s = (--sp)->as.f; co->waitUntil = plat_millis() + (int)(s * 1000.0f); }
                else if (kind == Y_CORO) {
                    Value w = *--sp;
                    if (w.t == V_OBJ) co->waitFor = w.as.o; else co->waitKind = Y_NEXT;
                }
                co->ip = ip;
                co->lastFrame = vm->frameNo;
                memcpy(co->locals, bp, sizeof(Value) * (size_t)co->nlocals);
                vm->fp = startFp;
                vm->sp = fr->base;
                /* 'yield return Other()' starts Other if nobody did */
                if (co->waitFor && !((ObjCoro*)co->waitFor)->started) {
                    ((ObjCoro*)co->waitFor)->owner = (Obj*)co;
                    if (!coro_start(vm, (ObjCoro*)co->waitFor)) return 0;
                }
                return 2;
            }
            case OP_CORO: {
                int sub = ARG();
                if (sub == C_STOPALL) { for (int i = 0; i < vm->ncoros; i++) vm->coros[i]->done = 1; break; }
                Value v = sp[-1];
                if (v.t != V_OBJ) FAIL(sub == C_START ? "StartCoroutine(null)" : "StopCoroutine(null)");
                ObjCoro* co = (ObjCoro*)v.as.o;
                if (sub == C_STOP) { coro_stop(co); sp--; break; }
                SYNC();                       /* C_START: leaves the coroutine on the stack as the result */
                if (!coro_start(vm, co)) FAILED();
                break;
            }
            default: FAIL("internal error: bad opcode");
        }
    }
}
