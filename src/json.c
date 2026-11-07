/* MiniSharp - small JSON tree (see json.h). */
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

JVal* json_new(int t) {
    JVal* v = (JVal*)calloc(1, sizeof(JVal));
    if (v) v->t = t;
    return v;
}

static char* dupstr(const char* s) {
    size_t n = strlen(s);
    char* d = (char*)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

JVal* json_str(const char* s) {
    JVal* v = json_new(JS_STR);
    if (v) v->s = dupstr(s ? s : "");
    return v;
}

JVal* json_num(double d) {
    char buf[40];
    if (d == floor(d) && fabs(d) < 1e15) snprintf(buf, sizeof buf, "%.0f", d);
    else {
        for (int p = 1; p <= 17; p++) {                 /* the shortest text that reads back the same, as in JavaScript */
            snprintf(buf, sizeof buf, "%.*g", p, d);
            if (strtod(buf, NULL) == d) break;
        }
    }
    if (!strcmp(buf, "-0")) strcpy(buf, "0");
    JVal* v = json_new(JS_NUM);
    if (v) v->s = dupstr(buf);
    return v;
}

void json_free(JVal* v) {
    if (!v) return;
    free(v->s);
    for (int i = 0; i < v->n; i++) { json_free(v->items[i]); if (v->keys) free(v->keys[i]); }
    free(v->items); free(v->keys);
    free(v);
}

static int grow(JVal* v) {
    if (v->n < v->cap) return 1;
    int nc = v->cap ? v->cap * 2 : 8;
    JVal** it = (JVal**)realloc(v->items, (size_t)nc * sizeof(JVal*));
    if (!it) return 0;
    v->items = it;
    if (v->t == JS_OBJ) {
        char** k = (char**)realloc(v->keys, (size_t)nc * sizeof(char*));
        if (!k) return 0;
        v->keys = k;
    }
    v->cap = nc;
    return 1;
}

void json_push(JVal* arr, JVal* v) {
    if (!arr || !v || !grow(arr)) { json_free(v); return; }
    arr->items[arr->n++] = v;
}

void json_remove_at(JVal* arr, int i) {
    if (!arr || i < 0 || i >= arr->n) return;
    json_free(arr->items[i]);
    if (arr->keys) free(arr->keys[i]);
    memmove(arr->items + i, arr->items + i + 1, (size_t)(arr->n - i - 1) * sizeof(JVal*));
    if (arr->keys) memmove(arr->keys + i, arr->keys + i + 1, (size_t)(arr->n - i - 1) * sizeof(char*));
    arr->n--;
}

JVal* json_get(const JVal* o, const char* key) {
    if (!o || o->t != JS_OBJ) return NULL;
    for (int i = o->n - 1; i >= 0; i--) if (!strcmp(o->keys[i], key)) return o->items[i];   /* the last one wins, as in JS */
    return NULL;
}

void json_set(JVal* o, const char* key, JVal* v) {
    if (!o || o->t != JS_OBJ) { json_free(v); return; }
    for (int i = 0; i < o->n; i++)
        if (!strcmp(o->keys[i], key)) { json_free(o->items[i]); o->items[i] = v; return; }
    if (!grow(o)) { json_free(v); return; }
    o->keys[o->n] = dupstr(key);
    o->items[o->n++] = v;
}

/* ---------- parsing ---------- */
typedef struct { const char* p; char* err; int errlen; int bad; } P;

static void fail(P* p, const char* msg) {
    if (!p->bad) snprintf(p->err, (size_t)p->errlen, "JSON: %s", msg);
    p->bad = 1;
}
static void ws(P* p) { while (*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r') p->p++; }

static void put_utf8(char** o, unsigned cp) {
    char* d = *o;
    if (cp < 0x80) *d++ = (char)cp;
    else if (cp < 0x800) { *d++ = (char)(0xC0 | (cp >> 6)); *d++ = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { *d++ = (char)(0xE0 | (cp >> 12)); *d++ = (char)(0x80 | ((cp >> 6) & 63)); *d++ = (char)(0x80 | (cp & 63)); }
    else { *d++ = (char)(0xF0 | (cp >> 18)); *d++ = (char)(0x80 | ((cp >> 12) & 63)); *d++ = (char)(0x80 | ((cp >> 6) & 63)); *d++ = (char)(0x80 | (cp & 63)); }
    *o = d;
}

static int hex4(const char* s, unsigned* out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static char* parse_string(P* p) {
    p->p++;                                              /* opening quote */
    const char* s = p->p;
    size_t n = 0;
    while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; n++; }
    char* out = (char*)malloc(n * 4 + 1), *o = out;
    if (!out) { fail(p, "out of memory"); return NULL; }
    while (*p->p && *p->p != '"') {
        char c = *p->p++;
        if (c != '\\') { *o++ = c; continue; }
        c = *p->p++;
        switch (c) {
            case 'n': *o++ = '\n'; break; case 't': *o++ = '\t'; break; case 'r': *o++ = '\r'; break;
            case 'b': *o++ = '\b'; break; case 'f': *o++ = '\f'; break;
            case '"': case '\\': case '/': *o++ = c; break;
            case 'u': {
                unsigned cp;
                if (!hex4(p->p, &cp)) { fail(p, "bad \\u escape"); free(out); return NULL; }
                p->p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && p->p[0] == '\\' && p->p[1] == 'u') {
                    unsigned lo;
                    if (hex4(p->p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) { p->p += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                }
                put_utf8(&o, cp);
                break;
            }
            default: fail(p, "bad escape"); free(out); return NULL;
        }
    }
    if (*p->p != '"') { fail(p, "unterminated string"); free(out); return NULL; }
    p->p++;
    *o = 0;
    return out;
}

static JVal* parse_value(P* p, int depth) {
    if (depth > 200) { fail(p, "too deep"); return NULL; }
    ws(p);
    char c = *p->p;
    if (c == '{' || c == '[') {
        int obj = c == '{';
        JVal* v = json_new(obj ? JS_OBJ : JS_ARR);
        if (!v) { fail(p, "out of memory"); return NULL; }
        p->p++;
        ws(p);
        if (*p->p == (obj ? '}' : ']')) { p->p++; return v; }
        for (;;) {
            char* key = NULL;
            if (obj) {
                ws(p);
                if (*p->p != '"') { fail(p, "expected a key"); json_free(v); return NULL; }
                key = parse_string(p);
                if (!key) { json_free(v); return NULL; }
                ws(p);
                if (*p->p != ':') { fail(p, "expected ':'"); free(key); json_free(v); return NULL; }
                p->p++;
            }
            JVal* item = parse_value(p, depth + 1);
            if (!item || !grow(v)) { free(key); json_free(item); json_free(v); return NULL; }
            if (obj) v->keys[v->n] = key;
            v->items[v->n++] = item;
            ws(p);
            if (*p->p == ',') { p->p++; continue; }
            if (*p->p == (obj ? '}' : ']')) { p->p++; return v; }
            fail(p, obj ? "expected ',' or '}'" : "expected ',' or ']'");
            json_free(v);
            return NULL;
        }
    }
    if (c == '"') { char* s = parse_string(p); if (!s) return NULL; JVal* v = json_new(JS_STR); if (v) v->s = s; else free(s); return v; }
    if (!strncmp(p->p, "true", 4)) { p->p += 4; return json_new(JS_TRUE); }
    if (!strncmp(p->p, "false", 5)) { p->p += 5; return json_new(JS_FALSE); }
    if (!strncmp(p->p, "null", 4)) { p->p += 4; return json_new(JS_NULL); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char* s = p->p;
        if (*p->p == '-') p->p++;
        while ((*p->p >= '0' && *p->p <= '9') || *p->p == '.' || *p->p == 'e' || *p->p == 'E' || *p->p == '+' || *p->p == '-') p->p++;
        JVal* v = json_new(JS_NUM);
        if (!v) return NULL;
        v->s = (char*)malloc((size_t)(p->p - s) + 1);
        if (v->s) { memcpy(v->s, s, (size_t)(p->p - s)); v->s[p->p - s] = 0; }
        return v;
    }
    fail(p, "unexpected character");
    return NULL;
}

JVal* json_parse(const char* text, char* err, int errlen) {
    P p = { text, err, errlen, 0 };
    JVal* v = parse_value(&p, 0);
    if (!v) return NULL;
    ws(&p);
    if (*p.p) { fail(&p, "text after the value"); json_free(v); return NULL; }
    return v;
}

/* ---------- writing, as JSON.stringify ---------- */
typedef struct { char* p; size_t n, cap; int oom; } B;
static void add(B* b, const char* s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 256;
        while (c < b->n + n + 1) c *= 2;
        char* q = (char*)realloc(b->p, c);
        if (!q) { b->oom = 1; return; }
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void adds(B* b, const char* s) { add(b, s, strlen(s)); }

static void write_string(B* b, const char* s) {
    adds(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char e[8];
        switch (c) {
            case '"': adds(b, "\\\""); break;
            case '\\': adds(b, "\\\\"); break;
            case '\n': adds(b, "\\n"); break;
            case '\r': adds(b, "\\r"); break;
            case '\t': adds(b, "\\t"); break;
            case '\b': adds(b, "\\b"); break;
            case '\f': adds(b, "\\f"); break;
            default:
                if (c < 0x20) { snprintf(e, sizeof e, "\\u%04x", c); adds(b, e); }
                else add(b, (const char*)&c, 1);
        }
    }
    adds(b, "\"");
}

static void write_value(B* b, const JVal* v) {
    switch (v->t) {
        case JS_NULL: adds(b, "null"); break;
        case JS_FALSE: adds(b, "false"); break;
        case JS_TRUE: adds(b, "true"); break;
        case JS_NUM: adds(b, v->s ? v->s : "0"); break;
        case JS_STR: write_string(b, v->s ? v->s : ""); break;
        case JS_ARR:
            adds(b, "[");
            for (int i = 0; i < v->n; i++) { if (i) adds(b, ","); write_value(b, v->items[i]); }
            adds(b, "]");
            break;
        case JS_OBJ:
            adds(b, "{");
            for (int i = 0; i < v->n; i++) {
                if (i) adds(b, ",");
                write_string(b, v->keys[i]); adds(b, ":"); write_value(b, v->items[i]);
            }
            adds(b, "}");
            break;
    }
}

char* json_write(const JVal* v) {
    B b = { NULL, 0, 0, 0 };
    write_value(&b, v);
    if (b.oom) { free(b.p); return NULL; }
    return b.p;
}

/* ---------- JavaScript-like conveniences ---------- */
const char* json_text(const JVal* v, char* buf, int cap) {
    if (!v || v->t == JS_NULL) return NULL;
    switch (v->t) {
        case JS_STR: return v->s;
        case JS_TRUE: return "true";
        case JS_FALSE: return "false";
        case JS_NUM: {                                   /* String(1.50) is "1.5": normalize like JavaScript */
            double d = strtod(v->s, NULL);
            JVal* t = json_num(d);
            snprintf(buf, (size_t)cap, "%s", t && t->s ? t->s : v->s);
            json_free(t);
            return buf;
        }
        default: snprintf(buf, (size_t)cap, "%s", v->t == JS_ARR ? "" : "[object Object]"); return buf;
    }
}

int json_truthy(const JVal* v) {
    if (!v) return 0;
    switch (v->t) {
        case JS_NULL: case JS_FALSE: return 0;
        case JS_NUM: { double d = strtod(v->s, NULL); return d != 0 && d == d; }
        case JS_STR: return v->s && v->s[0];
        default: return 1;
    }
}

double json_number(const JVal* v, double dflt) {
    if (!v) return dflt;
    if (v->t == JS_NUM) return strtod(v->s, NULL);
    if (v->t == JS_TRUE) return 1;
    if (v->t == JS_FALSE || v->t == JS_NULL) return 0;
    if (v->t == JS_STR) { char* e; double d = strtod(v->s, &e); return *v->s && !*e ? d : dflt; }
    return dflt;
}
