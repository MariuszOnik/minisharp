/* MiniSharp - REPL (terminal). Each line is compiled as the body of
 *     class Repl { <earlier variables as static fields> static void Main() { <line> } }
 * and run on one VM that lives as long as the terminal, so the values of the fields carry over. */
#include "ms.h"
#include "repl.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define DECLS_CAP 4096
#define TIME_LIMIT 4                    /* seconds (+ up to one) before a loop is stopped */

struct Repl {
    VM* vm;
    Program** progs; int nprogs, cap;   /* every line run so far: values may point at their string constants */
    char decls[DECLS_CAP];              /* "static int x; static string s; " */
};

static const char* SKELETON = "class Repl { static void Main() { } }";

static int keep(Repl* r, Program* p) {
    if (r->nprogs == r->cap) {
        int cap = r->cap ? r->cap * 2 : 16;
        Program** n = (Program**)realloc(r->progs, sizeof(Program*) * cap);
        if (!n) return 0;
        r->progs = n; r->cap = cap;
    }
    r->progs[r->nprogs++] = p;
    return 1;
}

Repl* repl_new(void) {
    Repl* r = (Repl*)calloc(1, sizeof(Repl));
    if (!r) return NULL;
    char err[200];
    Program* p = ms_compile(SKELETON, err, sizeof err);
    if (!p || !keep(r, p) || !(r->vm = vm_new(p))) { if (p && !r->nprogs) ms_free_program(p); repl_free(r); return NULL; }
    return r;
}

void repl_free(Repl* r) {
    if (!r) return;
    vm_free(r->vm);
    for (int i = 0; i < r->nprogs; i++) ms_free_program(r->progs[i]);
    free(r->progs);
    free(r);
}

/* "line 1: x" and "line 1 (Repl.Main): x" -> "x": the terminal has only one line */
static void strip_line(char* s) {
    if (strncmp(s, "line ", 5)) return;
    const char* p = s + 5;
    while (isdigit((unsigned char)*p)) p++;
    if (!strncmp(p, ": ", 2)) p += 2;
    else if (!strncmp(p, " (Repl.Main): ", 14)) p += 14;
    else return;
    memmove(s, p, strlen(p) + 1);
}

int repl_eval(Repl* r, const char* line, char* err, int errlen) {
    err[0] = 0;
    int n = (int)strlen(line);
    while (n > 0 && isspace((unsigned char)line[n - 1])) n--;
    if (n == 0) return 1;
    int semi = line[n - 1] != ';' && line[n - 1] != '}';     /* "Math.Sqrt(2)" needs no ';' */

    size_t cap = strlen(r->decls) + n + 64;
    char* src = (char*)malloc(cap);
    char* decls = (char*)malloc(DECLS_CAP);
    if (!src || !decls) { free(src); free(decls); snprintf(err, errlen, "out of memory"); return 0; }
    snprintf(src, cap, "class Repl { %sstatic void Main() { %.*s%s\n} }", r->decls, n, line, semi ? ";" : "");
    memcpy(decls, r->decls, DECLS_CAP);
    Program* p = ms_compile_repl(src, err, errlen, decls, DECLS_CAP);
    free(src);
    if (!p) {
        free(decls);
        strip_line(err);
        char* f = strstr(err, "found ';'");          /* the ';' we added, not one the user typed */
        if (semi && f && !f[9]) snprintf(f, errlen - (f - err), "found end of line");
        return 0;
    }
    if (!keep(r, p)) ms_free_program(p);
    if (r->progs[r->nprogs - 1] != p || !vm_set_program(r->vm, p)) { free(decls); snprintf(err, errlen, "out of memory"); return 0; }
    memcpy(r->decls, decls, DECLS_CAP);         /* new variables exist from now on, even if the line fails */
    free(decls);

    VM* vm = r->vm;
    vm->sp = 0; vm->fp = 0; vm->quit = 0;       /* a failed line may have left frames and its error behind */
    vm->err[0] = 0;
    vm->deadline = (long long)time(NULL) + TIME_LIMIT;
    int ok = vm_call(vm, p->entryMain);
    vm->deadline = 0;
    if (!ok && !vm->quit) { snprintf(err, errlen, "%s", vm->err); strip_line(err); return 0; }
    return 1;
}

/* ---------- completion ---------- */
static const char* KEYWORDS[] = {
    "var", "int", "float", "bool", "char", "string", "new", "null", "true", "false",
    "if", "else", "for", "foreach", "in", "while", "do", "switch", "break", "continue", "return",
    "List", "Dictionary",
};

static int isword(int ch) { return isalnum(ch) || ch == '_'; }

static int has(char out[][REPL_WORD], int n, const char* s) {
    for (int i = 0; i < n; i++) if (!strcmp(out[i], s)) return 1;
    return 0;
}

/* adds s if it starts with the prefix (any letter case: "con" finds Console) and is longer */
static int offer(char out[][REPL_WORD], int n, int max, const char* s, int slen, const char* pre, int plen) {
    if (n >= max || slen <= plen || slen >= REPL_WORD) return n;
    for (int i = 0; i < plen; i++) if (tolower((unsigned char)s[i]) != tolower((unsigned char)pre[i])) return n;
    char w[REPL_WORD];
    memcpy(w, s, slen); w[slen] = 0;
    if (has(out, n, w)) return n;
    strcpy(out[n], w);
    return n + 1;
}

int repl_complete(Repl* r, const char* line, int cursor, int* start, char out[][REPL_WORD], int max) {
    int ws = cursor;
    while (ws > 0 && isword((unsigned char)line[ws - 1])) ws--;
    *start = ws;
    const char* pre = line + ws; int plen = cursor - ws;
    int n = 0;
    if (ws > 0 && line[ws - 1] == '.') {                /* members of a built-in class */
        int os = ws - 1;
        while (os > 0 && isword((unsigned char)line[os - 1])) os--;
        int olen = ws - 1 - os;
        if (olen == 0) return 0;
        for (int i = 0; i < ms_nnatives; i++) {
            const NativeDef* d = &ms_natives[i];
            if ((int)strlen(d->cls) == olen && !memcmp(d->cls, line + os, olen))
                n = offer(out, n, max, d->name, (int)strlen(d->name), pre, plen);
        }
        for (int i = 0; i < ms_nconsts; i++) {
            const ConstDef* d = &ms_consts[i];
            if ((int)strlen(d->cls) == olen && !memcmp(d->cls, line + os, olen))
                n = offer(out, n, max, d->name, (int)strlen(d->name), pre, plen);
        }
        return n;
    }
    if (plen == 0 || isdigit((unsigned char)line[ws])) return 0;
    /* the terminal's own variables first: "static int x; " */
    for (const char* s = r->decls; (s = strchr(s, ';')); s++) {
        const char* e = s;
        while (e > r->decls && e[-1] != ' ') e--;
        n = offer(out, n, max, e, (int)(s - e), pre, plen);
    }
    for (int i = 0; i < ms_nnatives; i++)
        if (ms_natives[i].cls[0] != '.') n = offer(out, n, max, ms_natives[i].cls, (int)strlen(ms_natives[i].cls), pre, plen);
    for (int i = 0; i < ms_nconsts; i++) n = offer(out, n, max, ms_consts[i].cls, (int)strlen(ms_consts[i].cls), pre, plen);
    for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++)
        n = offer(out, n, max, KEYWORDS[i], (int)strlen(KEYWORDS[i]), pre, plen);
    return n;
}
