/* MiniSharp - a light index of the project's code for the editor (see codeindex.h).
 * It reads the text, not the AST: class bodies give the members, and a name's type comes from
 * the nearest declaration ("Ship s", "var s = new Ship()", "foreach (var b in bullets)"). */
#include "codeindex.h"
#include "ms.h"
#include "repl.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 6

static int is_id0(int c) { return isalpha(c) || c == '_'; }
static int is_id(int c) { return isalnum(c) || c == '_'; }

static const char* MODIFIERS[] = { "public", "private", "protected", "internal", "static", "const", "readonly",
                                   "override", "virtual", "sealed", "abstract", NULL };
/* words that can come before a name without being its type */
static const char* NOT_TYPES[] = { "return", "new", "in", "else", "case", "out", "ref", "using", "namespace", "class", "interface",
                                   "yield", "throw", "is", "as", "goto", NULL };

static int in_list(const char** list, const char* s, int n) {
    for (int i = 0; list[i]; i++) if ((int)strlen(list[i]) == n && !memcmp(list[i], s, n)) return 1;
    return 0;
}

/* class ranges, for ci_class_at */
typedef struct { int cls, text, start, end; } Range;
static Range g_ranges[128];
static int g_nranges;

static void add_member(CodeIndex* ci, int cls, const char* type, const char* name, int nlen, const char* sig, int isMethod) {
    if (ci->n == ci->cap) {
        int cap = ci->cap ? ci->cap * 2 : 64;
        CIMember* m = (CIMember*)realloc(ci->m, sizeof(CIMember) * cap);
        if (!m) return;
        ci->m = m; ci->cap = cap;
    }
    CIMember* m = &ci->m[ci->n++];
    snprintf(m->cls, sizeof m->cls, "%s", ci->classes[cls]);
    snprintf(m->name, sizeof m->name, "%.*s", nlen, name);
    snprintf(m->type, sizeof m->type, "%s", type);
    snprintf(m->sig, sizeof m->sig, "%s", sig);
    m->isMethod = isMethod;
}

static int skip_string(const char* s, int p) {
    char q = s[p++];
    while (s[p] && s[p] != q && s[p] != '\n') p += s[p] == '\\' && s[p + 1] ? 2 : 1;
    return s[p] == q ? p + 1 : p;
}

/* "<...>" from s[p] == '<'; returns the position after the matching '>' */
static int skip_generic(const char* s, int p) {
    int d = 0;
    for (; s[p]; p++) {
        if (s[p] == '<') d++;
        else if (s[p] == '>' && --d == 0) return p + 1;
        else if (!is_id(s[p]) && s[p] != ',' && s[p] != ' ' && s[p] != '[' && s[p] != ']') return p;
    }
    return p;
}

/* a type at s[p]: Name, Name<...>, Name[] ...; returns its end or p if none */
static int read_type(const char* s, int p) {
    if (!is_id0((unsigned char)s[p])) return p;
    while (is_id((unsigned char)s[p])) p++;
    if (s[p] == '<') p = skip_generic(s, p);
    while (s[p] == '[' && s[p + 1] == ']') p += 2;
    return p;
}

static int skip_sp(const char* s, int p) { while (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r') p++; return p; }

/* one member declaration in a class body starting at s[p]; returns where its head ends (';', '{' or '=') */
static int member(CodeIndex* ci, int cls, const char* s, int p) {
    /* the head without comments, on one line */
    char head[300];
    int n = 0, par = 0, q = p;
    while (s[q]) {
        char c = s[q];
        if (c == '/' && s[q + 1] == '/') { while (s[q] && s[q] != '\n') q++; continue; }
        if (c == '/' && s[q + 1] == '*') { q += 2; while (s[q] && !(s[q] == '*' && s[q + 1] == '/')) q++; if (s[q]) q += 2; continue; }
        if (par == 0 && (c == ';' || c == '{' || c == '=' || c == '}')) break;
        if (c == '(') par++;
        if (c == ')') par--;
        if (n < (int)sizeof head - 1) head[n++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        q++;
    }
    head[n] = 0;
    int i = skip_sp(head, 0);
    for (;;) {                                  /* modifiers */
        int e = i;
        while (is_id((unsigned char)head[e])) e++;
        if (e > i && in_list(MODIFIERS, head + i, e - i)) i = skip_sp(head, e);
        else break;
    }
    if (!strncmp(head + i, "class ", 6) || !strncmp(head + i, "interface ", 10)) return q;
    int ts = i, te = read_type(head, i);
    if (te == ts) return q;
    char type[48];
    snprintf(type, sizeof type, "%.*s", te - ts, head + ts);
    i = skip_sp(head, te);
    if (head[i] == '(') return q;               /* constructor */
    int ns = i;
    while (is_id((unsigned char)head[i])) i++;
    int nlen = i - ns;
    if (!nlen) return q;
    i = skip_sp(head, i);
    char sig[100];
    if (head[i] == '(') {                       /* method: "void Hit(int dmg)" */
        int e = i, d = 0;
        for (; head[e]; e++) { if (head[e] == '(') d++; else if (head[e] == ')' && --d == 0) { e++; break; } }
        snprintf(sig, sizeof sig, "%s %.*s%.*s", type, nlen, head + ns, e - i, head + i);
        add_member(ci, cls, type, head + ns, nlen, sig, 1);
        return q;
    }
    for (;;) {                                  /* fields: "float x, y" */
        snprintf(sig, sizeof sig, "%s %.*s", type, nlen, head + ns);
        add_member(ci, cls, type, head + ns, nlen, sig, 0);
        if (head[i] != ',') break;
        i = skip_sp(head, i + 1);
        ns = i;
        while (is_id((unsigned char)head[i])) i++;
        nlen = i - ns;
        if (!nlen) break;
        i = skip_sp(head, i);
    }
    if (s[q] != '=') return q;
    /* "float x = 200, y;": more names after an initializer */
    for (int r = q + 1, d = 0; s[r] && s[r] != ';'; r++) {
        if (s[r] == '"' || s[r] == '\'') { r = skip_string(s, r) - 1; continue; }
        if (strchr("([{", s[r])) d++;
        else if (strchr(")]}", s[r])) { if (--d < 0) break; }
        else if (s[r] == ',' && d == 0) {
            int a = skip_sp(s, r + 1), b = a;
            while (is_id((unsigned char)s[b])) b++;
            if (b == a) break;
            snprintf(sig, sizeof sig, "%s %.*s", type, b - a, s + a);
            add_member(ci, cls, type, s + a, b - a, sig, 0);
        }
    }
    return q;
}

static void scan(CodeIndex* ci, int t) {
    const char* s = ci->texts[t];
    int depth = 0, stackDepth[16], stackCls[16], stackRange[16], nstack = 0, pending = -1, atStart = 0;
    for (int p = 0; s[p]; ) {
        char c = s[p];
        int inBody = nstack && depth == stackDepth[nstack - 1];
        if (c == '/' && s[p + 1] == '/') { while (s[p] && s[p] != '\n') p++; continue; }
        if (c == '/' && s[p + 1] == '*') { p += 2; while (s[p] && !(s[p] == '*' && s[p + 1] == '/')) p++; if (s[p]) p += 2; continue; }
        if (c == '"' || c == '\'') { p = skip_string(s, p); atStart = 0; continue; }
        if (c == '{') {
            depth++;
            if (pending >= 0 && nstack < 16) {
                stackDepth[nstack] = depth; stackCls[nstack] = pending; stackRange[nstack] = -1;
                if (g_nranges < 128) {
                    Range* r = &g_ranges[g_nranges]; r->cls = pending; r->text = t; r->start = p + 1; r->end = (int)strlen(s);
                    stackRange[nstack] = g_nranges++;
                }
                nstack++;
                pending = -1;
            }
            atStart = nstack && depth == stackDepth[nstack - 1];
            p++; continue;
        }
        if (c == '}') {
            if (nstack && depth == stackDepth[nstack - 1]) {
                nstack--;
                if (stackRange[nstack] >= 0) g_ranges[stackRange[nstack]].end = p;
            }
            depth--;
            atStart = nstack && depth == stackDepth[nstack - 1];
            p++; continue;
        }
        if (c == ';') { atStart = inBody; p++; continue; }
        if (isspace((unsigned char)c)) { p++; continue; }
        if (c == '[' && atStart) { while (s[p] && s[p] != ']') p++; continue; }     /* [attribute] */
        if (is_id0((unsigned char)c)) {
            int e = p;
            while (is_id((unsigned char)s[e])) e++;
            if ((e - p == 5 && !memcmp(s + p, "class", 5)) || (e - p == 9 && !memcmp(s + p, "interface", 9))) {
                int ns = skip_sp(s, e), ne = ns;
                while (is_id((unsigned char)s[ne])) ne++;
                if (ne > ns && ci->nclasses < 64) {
                    int k = ci->nclasses++;
                    snprintf(ci->classes[k], 32, "%.*s", ne - ns, s + ns);
                    ci->bases[k][0] = 0;
                    pending = k;
                    int b = skip_sp(s, ne);
                    if (s[b] == ':') {                  /* class Boss : Enemy, IHealable */
                        int n = 0;
                        for (b = skip_sp(s, b + 1); is_id0((unsigned char)s[b]); ) {
                            int be = b;
                            while (is_id((unsigned char)s[be])) be++;
                            int w = snprintf(ci->bases[k] + n, sizeof ci->bases[k] - n, "%s%.*s", n ? " " : "", be - b, s + b);
                            if (w > 0 && w < (int)sizeof ci->bases[k] - n) n += w;
                            b = skip_sp(s, be);
                            if (s[b] != ',') break;
                            b = skip_sp(s, b + 1);
                        }
                        ne = b;
                    }
                }
                p = ne; atStart = 0; continue;
            }
            if (atStart && inBody) { p = member(ci, stackCls[nstack - 1], s, p); atStart = 0; continue; }
            p = e; atStart = 0; continue;
        }
        atStart = 0;
        p++;
    }
}

void ci_build(CodeIndex* ci, const char** texts, int ntexts) {
    ci->n = 0; ci->nclasses = 0;
    ci->texts = texts; ci->ntexts = ntexts;
    g_nranges = 0;
    for (int t = 0; t < ntexts; t++) scan(ci, t);
}

void ci_free(CodeIndex* ci) { free(ci->m); memset(ci, 0, sizeof *ci); }

const char* ci_class_at(const CodeIndex* ci, int t, int pos) {
    const char* best = NULL;
    int bestStart = -1;
    for (int i = 0; i < g_nranges; i++) {
        const Range* r = &g_ranges[i];
        if (r->text == t && pos >= r->start && pos <= r->end && r->start > bestStart) { best = ci->classes[r->cls]; bestStart = r->start; }
    }
    return best;
}

/* ---------- types ---------- */
static int is_user_class(const CodeIndex* ci, const char* s, int n) {
    for (int i = 0; i < ci->nclasses; i++) if ((int)strlen(ci->classes[i]) == n && !memcmp(ci->classes[i], s, n)) return 1;
    return 0;
}
static int is_builtin_class(const char* s, int n) {
    for (int i = 0; i < ms_nnatives; i++) if ((int)strlen(ms_natives[i].cls) == n && !memcmp(ms_natives[i].cls, s, n)) return 1;
    for (int i = 0; i < ms_nconsts; i++) if ((int)strlen(ms_consts[i].cls) == n && !memcmp(ms_consts[i].cls, s, n)) return 1;
    return 0;
}

static int has_word(const char* list, const char* w) {
    int n = (int)strlen(w);
    for (const char* p = list; (p = strstr(p, w)) != NULL; p++)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1;
    return 0;
}

/* the classes a member of 'type' can come from: the class itself, then its base classes and interfaces */
static int lineage(const CodeIndex* ci, const char* type, const char** out, int max) {
    int n = 0;
    if (max > 0) out[n++] = type;
    for (int q = 0; q < n && n < max; q++) {
        int k = -1;
        for (int i = 0; i < ci->nclasses; i++) if (!strcmp(ci->classes[i], out[q])) { k = i; break; }
        if (k < 0) continue;
        for (const char* b = ci->bases[k]; *b && n < max; ) {
            const char* e = strchr(b, ' ');
            int len = e ? (int)(e - b) : (int)strlen(b);
            for (int i = 0; i < ci->nclasses; i++)
                if ((int)strlen(ci->classes[i]) == len && !memcmp(ci->classes[i], b, len)) {
                    int dup = 0;
                    for (int j = 0; j < n; j++) dup |= out[j] == ci->classes[i];
                    if (!dup) out[n++] = ci->classes[i];
                }
            b += len + (e ? 1 : 0);
        }
    }
    return n;
}

/* "Dictionary<string, List<int>>" -> the n-th type argument */
static int type_arg(const char* type, int n, char* out, int cap) {
    const char* p = strchr(type, '<');
    if (!p) return 0;
    p++;
    for (int k = 0; ; k++) {
        while (*p == ' ') p++;
        const char* s = p;
        int d = 0;
        while (*p && !(d == 0 && (*p == ',' || *p == '>'))) { if (*p == '<') d++; if (*p == '>') d--; p++; }
        if (k == n) { snprintf(out, cap, "%.*s", (int)(p - s), s); return 1; }
        if (*p != ',') return 0;
        p++;
    }
}

static int element_type(const char* base, char* out, int cap) {
    int n = (int)strlen(base);
    if (n > 2 && !strcmp(base + n - 2, "[]")) { snprintf(out, cap, "%.*s", n - 2, base); return 1; }
    if (!strncmp(base, "List<", 5)) return type_arg(base, 0, out, cap);
    if (!strcmp(base, "string")) { snprintf(out, cap, "char"); return 1; }
    if (!strncmp(base, "Dictionary<", 11)) { snprintf(out, cap, "KeyValuePair%s", base + 10); return 1; }
    return 0;
}

static const char* code_name(int k) {
    switch (k) { case T_INT: return "int"; case T_FLOAT: return "float"; case T_BOOL: return "bool";
                 case T_STRING: return "string"; case T_CHAR: return "char"; default: return NULL; }
}

static int type_of_member(const CodeIndex* ci, const char* owner, const char* name, char* out, int cap) {
    const char* from[16];
    int nf = lineage(ci, owner, from, 16);
    for (int f = 0; f < nf; f++)
        for (int i = 0; i < ci->n; i++)
            if (!strcmp(ci->m[i].cls, from[f]) && !strcmp(ci->m[i].name, name)) { snprintf(out, cap, "%s", ci->m[i].type); return 1; }
    if (!strcmp(name, "Count") || !strcmp(name, "Length") || !strcmp(name, "IndexOf")) { snprintf(out, cap, "int"); return 1; }
    if (!strncmp(owner, "KeyValuePair<", 13)) return type_arg(owner, !strcmp(name, "Key") ? 0 : 1, out, cap);
    if (!strncmp(owner, "Dictionary<", 11) && (!strcmp(name, "Keys") || !strcmp(name, "Values"))) {
        char a[48];
        if (!type_arg(owner, !strcmp(name, "Keys") ? 0 : 1, a, sizeof a)) return 0;
        snprintf(out, cap, "List<%s>", a);
        return 1;
    }
    const char* cls = !strcmp(owner, "string") ? ".string" : owner;
    for (int i = 0; i < ms_nnatives; i++)
        if (!strcmp(ms_natives[i].cls, cls) && !strcmp(ms_natives[i].name, name) && code_name(ms_natives[i].ret)) {
            snprintf(out, cap, "%s", code_name(ms_natives[i].ret));
            return 1;
        }
    return 0;
}

static int resolve_end(const CodeIndex* ci, int t, int end, char* type, int cap, int depth);

/* s[o] starts a whole-word use of a name; is it its declaration? then its type */
static int decl_at(const CodeIndex* ci, int t, int o, int nlen, char* type, int cap, int depth) {
    const char* s = ci->texts[t];
    int a = skip_sp(s, o + nlen);
    int isIn = s[a] == 'i' && s[a + 1] == 'n' && !is_id((unsigned char)s[a + 2]);
    if (!isIn && !strchr("=;,)", s[a])) return 0;
    if (s[a] == '=' && s[a + 1] == '=') return 0;
    /* the type just before it */
    int p = o;
    while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t')) p--;
    int te = p;
    while (p >= 2 && s[p - 1] == ']' && s[p - 2] == '[') p -= 2;
    if (p > 0 && s[p - 1] == '>') {
        int d = 0;
        while (p > 0) { p--; if (s[p] == '>') d++; else if (s[p] == '<' && --d == 0) break; }
    }
    int ts = p;
    while (ts > 0 && is_id((unsigned char)s[ts - 1])) ts--;
    if (ts == p || !is_id0((unsigned char)s[ts]) || in_list(NOT_TYPES, s + ts, p - ts)) return 0;
    if (ts > 0 && s[ts - 1] == '.') return 0;
    if (p - ts != 3 || memcmp(s + ts, "var", 3)) { snprintf(type, cap, "%.*s", te - ts, s + ts); return 1; }
    /* var: from what it is given */
    if (isIn) {                                 /* foreach (var b in bullets) */
        int c = a + 2, d = 0;
        for (; s[c]; c++) { if (s[c] == '(') d++; else if (s[c] == ')') { if (d == 0) break; d--; } }
        char coll[48];
        return resolve_end(ci, t, c, coll, sizeof coll, depth + 1) && element_type(coll, type, cap);
    }
    if (s[a] != '=') return 0;
    int v = skip_sp(s, a + 1);
    if (!strncmp(s + v, "new ", 4)) {
        int ns = skip_sp(s, v + 4), ne = read_type(s, ns);
        if (ne == ns) return 0;
        if (s[ne] == '[') snprintf(type, cap, "%.*s[]", ne - ns, s + ns);
        else snprintf(type, cap, "%.*s", ne - ns, s + ns);
        return 1;
    }
    if (s[v] == '"') { snprintf(type, cap, "string"); return 1; }
    int e = v, d = 0;                           /* the initializer ends at ';' */
    for (; s[e] && !(d == 0 && (s[e] == ';' || s[e] == ',')); e++) { if (strchr("([{", s[e])) d++; if (strchr(")]}", s[e])) d--; }
    return resolve_end(ci, t, e, type, cap, depth + 1);
}

/* the declaration of 'name' closest before 'before' in text t */
static int find_decl(const CodeIndex* ci, int t, int before, const char* name, char* type, int cap, int depth) {
    const char* s = ci->texts[t];
    int n = (int)strlen(name);
    for (int p = before - n; p >= 0; p--) {
        if (memcmp(s + p, name, n) || (p > 0 && is_id((unsigned char)s[p - 1])) || is_id((unsigned char)s[p + n])) continue;
        if (decl_at(ci, t, p, n, type, cap, depth)) return 1;
    }
    return 0;
}

/* the value named s[ns..ne): a variable, field, class or (isCall) method */
static int name_type(const CodeIndex* ci, int t, int ns, int ne, char* type, int cap, int depth) {
    const char* s = ci->texts[t];
    char name[48];
    snprintf(name, sizeof name, "%.*s", ne - ns, s + ns);
    int q = ns;
    while (q > 0 && s[q - 1] == ' ') q--;
    if (q > 0 && s[q - 1] == '.') {             /* owner.name */
        char owner[48];
        return resolve_end(ci, t, q - 1, owner, sizeof owner, depth + 1) && type_of_member(ci, owner, name, type, cap);
    }
    if (!strcmp(name, "this")) {
        const char* c = ci_class_at(ci, t, ns);
        if (c) snprintf(type, cap, "%s", c);
        return c != NULL;
    }
    if (!strcmp(name, "base")) {                /* the first name after ':' */
        const char* c = ci_class_at(ci, t, ns);
        for (int i = 0; c && i < ci->nclasses; i++)
            if (!strcmp(ci->classes[i], c) && ci->bases[i][0]) {
                int len = (int)strcspn(ci->bases[i], " ");
                snprintf(type, cap, "%.*s", len, ci->bases[i]);
                return 1;
            }
        return 0;
    }
    if (is_user_class(ci, s + ns, ne - ns) || is_builtin_class(s + ns, ne - ns)) { snprintf(type, cap, "%s", name); return 1; }
    if (find_decl(ci, t, ns, name, type, cap, depth)) return 1;
    const char* c = ci_class_at(ci, t, ns);     /* a field or method of this class or a base class */
    return c && type_of_member(ci, c, name, type, cap);
}

static int resolve_end(const CodeIndex* ci, int t, int end, char* type, int cap, int depth) {
    if (depth > MAX_DEPTH) return 0;
    const char* s = ci->texts[t];
    int p = end;
    while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t')) p--;
    if (p == 0) return 0;
    char c = s[p - 1];
    if (c == ']' || c == ')') {
        char open = c == ']' ? '[' : '(';
        int o = p - 1, d = 0;
        for (; o >= 0; o--) { if (s[o] == c) d++; else if (s[o] == open && --d == 0) break; }
        if (o < 0) return 0;
        if (c == ']') {
            char base[48];
            return resolve_end(ci, t, o, base, sizeof base, depth + 1) && element_type(base, type, cap);
        }
        int ne = o;                             /* a call: the method's return type */
        while (ne > 0 && s[ne - 1] == ' ') ne--;
        int ns = ne;
        while (ns > 0 && is_id((unsigned char)s[ns - 1])) ns--;
        return ns < ne && name_type(ci, t, ns, ne, type, cap, depth);
    }
    if (c == '"') { snprintf(type, cap, "string"); return 1; }
    if (!is_id((unsigned char)c)) return 0;
    int ns = p;
    while (ns > 0 && is_id((unsigned char)s[ns - 1])) ns--;
    if (isdigit((unsigned char)s[ns])) return 0;
    return name_type(ci, t, ns, p, type, cap, depth);
}

int ci_type_before(const CodeIndex* ci, int t, int dot, char* type, int cap) {
    return resolve_end(ci, t, dot, type, cap, 0);
}

void ci_member_names(const CodeIndex* ci, const char* type, char* out, int cap) {
    int n = 0;
    out[0] = 0;
#define ADD(s) do { int l_ = snprintf(out + n, cap - n, "%s ", (s)); if (l_ > 0 && l_ < cap - n) n += l_; } while (0)
    if (!strncmp(type, "List<", 5)) { ADD("Add Count RemoveAt Clear Contains IndexOf Insert Remove AddRange Find FindAll FindIndex FindLast FindLastIndex Exists TrueForAll RemoveAll ForEach Sort Reverse"); return; }
    if (!strncmp(type, "Dictionary<", 11)) { ADD("ContainsKey TryGetValue Add Remove Count Keys Values Clear ContainsValue"); return; }
    if (!strncmp(type, "KeyValuePair<", 13)) { ADD("Key Value"); return; }
    if (!strncmp(type, "Func<", 5) || !strncmp(type, "Action", 6) || !strncmp(type, "Predicate<", 10) || !strncmp(type, "Comparison<", 11)) { ADD("Invoke"); return; }
    int tl = (int)strlen(type);
    if (tl > 2 && !strcmp(type + tl - 2, "[]")) { ADD("Length"); return; }
    if (!strcmp(type, "string")) ADD("Length");
    const char* from[16];
    int nf = lineage(ci, type, from, 16);
    for (int f = 0; f < nf; f++)
        for (int i = 0; i < ci->n; i++)
            if (!strcmp(ci->m[i].cls, from[f]) && !has_word(out, ci->m[i].name)) ADD(ci->m[i].name);   /* an override once */
    const char* cls = !strcmp(type, "string") ? ".string" : type;
    for (int i = 0; i < ms_nnatives; i++)
        if (!strcmp(ms_natives[i].cls, cls) && !strstr(out, ms_natives[i].name)) ADD(ms_natives[i].name);
    for (int i = 0; i < ms_nconsts; i++) if (!strcmp(ms_consts[i].cls, type)) ADD(ms_consts[i].name);
#undef ADD
}

int ci_complete(const CodeIndex* ci, int t, int cursor, int* start, char out[][REPL_WORD], int max) {
    const char* s = ci->texts[t];
    int ws = cursor;
    while (ws > 0 && is_id((unsigned char)s[ws - 1])) ws--;
    *start = ws;
    if (ws == 0 || s[ws - 1] != '.') return -1;
    char type[48], names[2048];
    if (!ci_type_before(ci, t, ws - 1, type, sizeof type)) return -1;
    ci_member_names(ci, type, names, sizeof names);
    int plen = cursor - ws, n = 0;
    for (const char* p = names; *p && n < max; ) {
        const char* e = strchr(p, ' ');
        int len = e ? (int)(e - p) : (int)strlen(p);
        int ok = len > plen && len < REPL_WORD;
        for (int i = 0; ok && i < plen; i++) ok = tolower((unsigned char)p[i]) == tolower((unsigned char)s[ws + i]);
        if (ok) { memcpy(out[n], p, len); out[n][len] = 0; n++; }
        p += len + (e ? 1 : 0);
    }
    return n;
}

const char* ci_signature(const CodeIndex* ci, const char* type, const char* name) {
    const char* from[16];
    int nf = lineage(ci, type, from, 16);
    for (int f = 0; f < nf; f++)
        for (int i = 0; i < ci->n; i++)
            if (ci->m[i].isMethod && !strcmp(ci->m[i].cls, from[f]) && !strcmp(ci->m[i].name, name)) return ci->m[i].sig;
    return NULL;
}
