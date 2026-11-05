/* MiniSharp - code editor on the console. The code is on the top screen (colours, line numbers),
 * the bottom screen has the tools, name suggestions and the touch keyboard.
 * A file is one char buffer; an edit moves the rest with memmove (the files are small). */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../src/platform.h"
#include "../src/repl.h"
#include "../src/codeindex.h"
#include "platform_3ds.h"
#include "keyboard.h"
#include "ui.h"
#include "help.h"
#include "app.h"
#include "editor.h"

#define MAX_DOCS 32
#define MAX_TEXT (512 * 1024)
#define UNDO 32
#define LINE_H 14
#define CODE_Y 18
#define VISIBLE ((240 - CODE_Y) / LINE_H)
#define GUTTER 30
#define SCALE 0.45f
#define NSUGGEST 4

enum { C_PLAIN = 0xD4D4D4, C_KEYWORD = 0x569CD6, C_TYPE = 0x4EC9B0, C_STRING = 0xCE9178,
       C_NUMBER = 0xB5CEA8, C_COMMENT = 0x6A9955 };

typedef struct { char* text; int len, cur; } Snap;

typedef struct {
    char name[64];
    char* text; int len, cap;
    int cur, top, want;             /* cursor (byte), first line on screen, column kept by Up/Down (-1: none) */
    float scrollX;
    int dirty, errLine;             /* errLine: 1-based line of the last error, 0: none */
    Snap undo[UNDO]; int nundo;
} Doc;

static Doc g_docs[MAX_DOCS];
static int g_ndocs, g_cd;           /* g_cd: the file being edited */
static char g_dir[160];             /* folder of the files */
static char g_runName[80];          /* for run_file: "Name/" or "file.cs" */
static int g_isProject;
static char g_status[320];
static uint32_t g_statusColor;
static int g_lastEditCur = -1;      /* typing on from where the last edit ended joins its undo step */
static u64 g_lastEditTime;
static char g_words[4096];          /* identifiers in the project, for suggestions */
static int g_wordsDirty;            /* 1: after typing (gathered later), 2: now */
static unsigned g_textGen;          /* bumped on every change of any file's text (redraws the code) */
static char g_sug[NSUGGEST][REPL_WORD];
static int g_nsug, g_sugStart;

#define D (&g_docs[g_cd])

static const char* KEYWORDS[] = {
    "class", "static", "public", "private", "void", "int", "float", "double", "bool", "char", "string",
    "var", "new", "return", "if", "else", "for", "foreach", "in", "while", "do", "switch", "case",
    "default", "break", "continue", "true", "false", "null", "this", "const", "using", "namespace",
    "yield", "out", "interface", "abstract", "virtual", "override", "sealed", "base", "is", "as",
    "protected", "readonly", NULL
};

static int isword(int c) { return isalnum(c) || c == '_'; }
static int is_keyword(const char* s, int n) {
    for (int i = 0; KEYWORDS[i]; i++) if ((int)strlen(KEYWORDS[i]) == n && !memcmp(KEYWORDS[i], s, n)) return 1;
    return 0;
}

static void set_status(uint32_t rgb, const char* fmt, const char* arg) {
    snprintf(g_status, sizeof g_status, fmt, arg);
    g_statusColor = rgb;
}

/* ---------- files ---------- */
static int reserve(Doc* d, int need) {
    if (need + 1 <= d->cap) return 1;
    if (need > MAX_TEXT) return 0;
    int cap = need * 2 + 1024;
    char* t = (char*)realloc(d->text, cap);
    if (!t) return 0;
    d->text = t; d->cap = cap;
    return 1;
}

static void doc_path(const Doc* d, char* path, int cap) { snprintf(path, cap, "%s/%s", g_dir, d->name); }

/* tabs become 4 spaces and '\r' goes away, so every character has one width */
static int doc_load(Doc* d, const char* name) {
    memset(d, 0, sizeof *d);
    snprintf(d->name, sizeof d->name, "%s", name);
    d->want = -1;
    char path[256];
    doc_path(d, path, sizeof path);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    int c;
    reserve(d, 4096);
    while ((c = fgetc(f)) != EOF) {
        if (c == '\r') continue;
        int n = c == '\t' ? 4 : 1;
        if (!reserve(d, d->len + n)) break;
        for (int i = 0; i < n; i++) d->text[d->len++] = c == '\t' ? ' ' : (char)c;
    }
    fclose(f);
    if (!d->text) return 0;
    d->text[d->len] = 0;
    return 1;
}

static int doc_save(Doc* d) {
    char path[256];
    doc_path(d, path, sizeof path);
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(d->text, 1, d->len, f) == (size_t)d->len;
    ok = fclose(f) == 0 && ok;
    if (ok) d->dirty = 0;
    return ok;
}

static void doc_free(Doc* d) {
    free(d->text);
    for (int i = 0; i < d->nundo; i++) free(d->undo[i].text);
    memset(d, 0, sizeof *d);
}

static int save_all(void) {
    for (int i = 0; i < g_ndocs; i++)
        if (g_docs[i].dirty && !doc_save(&g_docs[i])) { set_status(0xFF6060, "Nie można zapisać %.60s", g_docs[i].name); return 0; }
    return 1;
}

static int any_dirty(void) {
    for (int i = 0; i < g_ndocs; i++) if (g_docs[i].dirty) return 1;
    return 0;
}

/* ---------- lines ---------- */
static int line_start(Doc* d, int p) { while (p > 0 && d->text[p - 1] != '\n') p--; return p; }
static int line_end(Doc* d, int p) { while (p < d->len && d->text[p] != '\n') p++; return p; }
static int line_index(Doc* d, int p) { int n = 0; for (int i = 0; i < p; i++) n += d->text[i] == '\n'; return n; }
static int line_count(Doc* d) { return line_index(d, d->len) + 1; }
static int pos_of_line(Doc* d, int line) {
    int p = 0;
    for (int n = 0; n < line && p < d->len; p++) if (d->text[p] == '\n') n++;
    return p;
}

static float width_of(const char* s, int n) {
    char buf[512];
    if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    memcpy(buf, s, n); buf[n] = 0;
    return plat3ds_text_width(buf, SCALE);
}

/* ---------- editing ---------- */
/* one undo step per run of typing: a new step when the cursor moved or after a pause */
static void snapshot(void) {
    Doc* d = D;
    u64 now = osGetTime();
    if (d->nundo && d->cur == g_lastEditCur && now - g_lastEditTime < 1500) { g_lastEditTime = now; return; }
    g_lastEditTime = now;
    if (d->nundo == UNDO) {
        free(d->undo[0].text);
        memmove(d->undo, d->undo + 1, sizeof(Snap) * (UNDO - 1));
        d->nundo--;
    }
    Snap* s = &d->undo[d->nundo];
    s->text = (char*)malloc(d->len + 1);
    if (!s->text) return;
    memcpy(s->text, d->text, d->len + 1);
    s->len = d->len; s->cur = d->cur;
    d->nundo++;
}

static void undo(void) {
    Doc* d = D;
    if (!d->nundo) { set_status(0xC0C8D0, "%s", "Nie ma czego cofnąć"); return; }
    Snap* s = &d->undo[--d->nundo];
    free(d->text);
    d->text = s->text; d->len = s->len; d->cap = s->len + 1; d->cur = s->cur;
    d->dirty = 1; d->want = -1;
    g_lastEditCur = -1; g_wordsDirty = 1; g_textGen++;
}

static void edited(Doc* d) { g_textGen++; d->dirty = 1; d->errLine = 0; d->want = -1; g_wordsDirty = 1; g_status[0] = 0; }

static void raw_insert(const char* s, int n) {
    Doc* d = D;
    if (!reserve(d, d->len + n)) { set_status(0xFF6060, "%s", "Plik jest za duży"); return; }
    memmove(d->text + d->cur + n, d->text + d->cur, d->len - d->cur + 1);
    memcpy(d->text + d->cur, s, n);
    d->len += n; d->cur += n;
    edited(d);
}

static void raw_delete(int from, int to) {
    Doc* d = D;
    memmove(d->text + from, d->text + to, d->len - to + 1);
    d->len -= to - from;
    d->cur = from;
    edited(d);
}

static int only_spaces_before(Doc* d, int p) {
    for (int i = line_start(d, p); i < p; i++) if (d->text[i] != ' ') return 0;
    return 1;
}

static void edit_insert(const char* s) {
    Doc* d = D;
    snapshot();
    /* '}' on an empty line goes one level left */
    if (!strcmp(s, "}") && only_spaces_before(d, d->cur) && d->cur - line_start(d, d->cur) >= 4) raw_delete(d->cur - 4, d->cur);
    raw_insert(s, (int)strlen(s));
    g_lastEditCur = d->cur;
}

static void edit_backspace(void) {
    Doc* d = D;
    if (d->cur == 0) return;
    snapshot();
    int ls = line_start(d, d->cur), n = 1;
    if (d->cur > ls && only_spaces_before(d, d->cur)) { n = (d->cur - ls) % 4; if (!n) n = 4; }   /* one indent level */
    else while (n < d->cur && (d->text[d->cur - n] & 0xC0) == 0x80) n++;                        /* whole UTF-8 char */
    raw_delete(d->cur - n, d->cur);
    g_lastEditCur = d->cur;
}

/* new line with the same indent; one level more after '{', and "{|}" opens into three lines */
static void edit_enter(void) {
    Doc* d = D;
    snapshot();
    int ls = line_start(d, d->cur), ind = 0;
    while (ls + ind < d->cur && d->text[ls + ind] == ' ') ind++;
    int p = d->cur;
    while (p > ls && d->text[p - 1] == ' ') p--;
    int open = p > ls && d->text[p - 1] == '{';
    int closeNext = d->cur < d->len && d->text[d->cur] == '}';
    char buf[128];
    int n = 0;
    buf[n++] = '\n';
    for (int i = 0; i < ind + (open ? 4 : 0) && n < (int)sizeof buf - 1; i++) buf[n++] = ' ';
    raw_insert(buf, n);
    if (open && closeNext) {
        int keep = d->cur;
        n = 0;
        buf[n++] = '\n';
        for (int i = 0; i < ind && n < (int)sizeof buf - 1; i++) buf[n++] = ' ';
        raw_insert(buf, n);
        d->cur = keep;
    }
    g_lastEditCur = d->cur;
}

/* ---------- cursor ---------- */
static void move_left(Doc* d) {
    if (d->cur == 0) return;
    d->cur--;
    while (d->cur > 0 && (d->text[d->cur] & 0xC0) == 0x80) d->cur--;
    d->want = -1;
}
static void move_right(Doc* d) {
    if (d->cur >= d->len) return;
    d->cur++;
    while (d->cur < d->len && (d->text[d->cur] & 0xC0) == 0x80) d->cur++;
    d->want = -1;
}
static void move_vert(Doc* d, int dl) {
    int col = d->want >= 0 ? d->want : d->cur - line_start(d, d->cur);
    int line = line_index(d, d->cur) + dl, n = line_count(d);
    if (line < 0) line = 0;
    if (line >= n) line = n - 1;
    int ls = pos_of_line(d, line), le = line_end(d, ls);
    d->cur = ls + (col < le - ls ? col : le - ls);
    while (d->cur > ls && (d->text[d->cur] & 0xC0) == 0x80) d->cur--;
    d->want = col;
}

/* "Gfx.Cir" just before the cursor (letters, digits, '_' and '.') */
static int word_before_cursor(Doc* d, char* out, int cap) {
    int s = d->cur;
    while (s > 0 && (isword((unsigned char)d->text[s - 1]) || d->text[s - 1] == '.')) s--;
    int n = d->cur - s < cap - 1 ? d->cur - s : cap - 1;
    memcpy(out, d->text + s, n); out[n] = 0;
    return s;
}

/* ---------- suggestions and hints ---------- */
static void collect_words(void) {
    int n = 1;
    strcpy(g_words, " ");
    for (int di = 0; di < g_ndocs; di++) {
        const char* t = g_docs[di].text;
        for (int i = 0; t[i]; ) {
            if (!(isalpha((unsigned char)t[i]) || t[i] == '_')) { i++; continue; }
            int j = i;
            while (isword((unsigned char)t[j])) j++;
            int len = j - i;
            if (len >= 3 && len < REPL_WORD && !is_keyword(t + i, len) && n + len + 2 < (int)sizeof g_words) {
                char pat[REPL_WORD + 2];
                snprintf(pat, sizeof pat, " %.*s ", len, t + i);
                if (!strstr(g_words, pat)) { memcpy(g_words + n, t + i, len); n += len; g_words[n++] = ' '; g_words[n] = 0; }
            }
            i = j;
        }
    }
    g_wordsDirty = 0;
}

/* the index reads the docs' buffers, which move when they grow: point it at them before each use */
static CodeIndex g_ci;
static const char* g_texts[MAX_DOCS];
static CodeIndex* code_index(void) {
    for (int i = 0; i < g_ndocs; i++) g_texts[i] = g_docs[i].text;
    g_ci.texts = g_texts; g_ci.ntexts = g_ndocs;
    return &g_ci;
}

static void update_suggestions(void) {
    Doc* d = D;
    static u64 lastCollect;             /* while typing, gather the names at most every 2 s */
    if (g_wordsDirty && (g_wordsDirty > 1 || osGetTime() - lastCollect > 2000)) {
        collect_words();
        ci_build(code_index(), g_texts, g_ndocs);
        lastCollect = osGetTime();
    }
    /* after "ship." the members of its type */
    int start, n = ci_complete(code_index(), g_cd, d->cur, &start, g_sug, NSUGGEST);
    if (n >= 0) { g_nsug = n; g_sugStart = start; return; }
    int ls = line_start(d, d->cur);
    char line[256];
    int from = d->cur - ls > 255 ? d->cur - 255 : ls;
    memcpy(line, d->text + from, d->cur - from); line[d->cur - from] = 0;
    g_nsug = ms_complete(line, d->cur - from, &start, g_sug, NSUGGEST, g_words);
    g_sugStart = from + start;
}

static void apply_suggestion(int i) {
    Doc* d = D;
    snapshot();
    if (d->cur > g_sugStart) raw_delete(g_sugStart, d->cur);
    raw_insert(g_sug[i], (int)strlen(g_sug[i]));
    g_lastEditCur = d->cur;
}

/* the signature of the call the cursor is in, e.g. "void Circle(float x, ...)" */
static const char* call_hint(char* out, int cap) {
    Doc* d = D;
    int ls = line_start(d, d->cur), depth = 0;
    for (int p = d->cur - 1; p >= ls; p--) {
        char c = d->text[p];
        if (c == ')') depth++;
        else if (c == '(' && depth > 0) depth--;
        else if (c == '(') {
            int e = p;
            while (e > ls && d->text[e - 1] == ' ') e--;
            int s = e;
            while (s > ls && isword((unsigned char)d->text[s - 1])) s--;
            if (s == e) return NULL;
            int cs = s, ce = s;
            if (s > ls && d->text[s - 1] == '.') { ce = s - 1; cs = ce; while (cs > ls && isword((unsigned char)d->text[cs - 1])) cs--; }
            int ov;
            const char* sig = help_signature(d->text + cs, ce - cs, d->text + s, e - s, &ov);
            if (!sig) {                         /* the project's own methods: ship.Fire(, Fire( */
                char name[32], type[48];
                snprintf(name, sizeof name, "%.*s", e - s, d->text + s);
                CodeIndex* ci = code_index();
                const char* cls = NULL;
                if (s > ls && d->text[s - 1] == '.') { if (ci_type_before(ci, g_cd, s - 1, type, sizeof type)) cls = type; }
                else cls = ci_class_at(ci, g_cd, s);
                if (cls) sig = ci_signature(ci, cls, name);
                ov = 1;
            }
            if (!sig) return NULL;
            if (ov > 1) snprintf(out, cap, "%s   (+%d)", sig, ov - 1);
            else snprintf(out, cap, "%s", sig);
            return out;
        }
    }
    return NULL;
}

/* ---------- drawing ---------- */
/* The coloured pieces of the visible lines are parsed into g_codeBuf only when the text, the file
 * or the scroll position changes; the frames in between just draw them. */
#define MAX_SEGS 1500
typedef struct { C2D_Text t; float x, y; u32 col; } Seg;
static Seg g_segs[MAX_SEGS];
static int g_nsegs;
static C2D_TextBuf g_codeBuf;

/* one line of code with colours into g_segs; x is where its first character goes */
static void draw_code_line(const char* s, int n, float x, float y) {
    char seg[256];
    int i = 0;
    while (i < n && x < 400) {
        int j = i + 1;
        uint32_t col = C_PLAIN;
        unsigned char c = (unsigned char)s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { j = n; col = C_COMMENT; }
        else if (c == '"' || c == '\'') {
            while (j < n && s[j] != (char)c) j += s[j] == '\\' ? 2 : 1;
            if (j < n) j++;
            col = C_STRING;
        } else if (isdigit(c)) { while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '.')) j++; col = C_NUMBER; }
        else if (isalpha(c) || c == '_') {
            while (j < n && isword((unsigned char)s[j])) j++;
            col = is_keyword(s + i, j - i) ? C_KEYWORD : isupper(c) ? C_TYPE : C_PLAIN;
        } else {
            while (j < n && !isword((unsigned char)s[j]) && s[j] != '"' && s[j] != '\'' && !(s[j] == '/' && j + 1 < n && s[j + 1] == '/')) j++;
        }
        if (j > n) j = n;
        int len = j - i < (int)sizeof seg - 1 ? j - i : (int)sizeof seg - 1;
        memcpy(seg, s + i, len); seg[len] = 0;
        float w = plat3ds_text_width(seg, SCALE);
        if (x + w > GUTTER && g_nsegs < MAX_SEGS) {
            Seg* g = &g_segs[g_nsegs++];
            C2D_TextParse(&g->t, g_codeBuf, seg);
            C2D_TextOptimize(&g->t);
            g->x = x; g->y = y; g->col = plat3ds_color(col);
        }
        x += w;
        i = j;
    }
}

static void draw_top(void) {
    Doc* d = D;
    int curLine = line_index(d, d->cur);
    if (curLine < d->top) d->top = curLine;
    if (curLine >= d->top + VISIBLE) d->top = curLine - VISIBLE + 1;
    int ls = line_start(d, d->cur);
    float cx = width_of(d->text + ls, d->cur - ls), viewW = 400 - GUTTER - 12;
    if (cx - d->scrollX > viewW) d->scrollX = cx - viewW + 40;
    if (cx < d->scrollX) d->scrollX = cx > 40 ? cx - 40 : 0;
    float x0 = GUTTER + 4 - d->scrollX;

    static unsigned cGen = ~0u;
    static int cDoc = -1, cTop = -1, cShown;
    static float cScroll = -1;
    if (cGen != g_textGen || cDoc != g_cd || cTop != d->top || cScroll != d->scrollX) {
        C2D_TextBufClear(g_codeBuf);
        g_nsegs = 0;
        int p = pos_of_line(d, d->top);
        for (cShown = 0; cShown < VISIBLE; ) {
            int e = line_end(d, p);
            draw_code_line(d->text + p, e - p, x0, CODE_Y + cShown * LINE_H);
            cShown++;
            if (e >= d->len) break;
            p = e + 1;
        }
        cGen = g_textGen; cDoc = g_cd; cTop = d->top; cScroll = d->scrollX;
    }
    int shown = cShown;
    for (int i = 0; i < shown; i++) {
        int line = d->top + i;
        float y = CODE_Y + i * LINE_H;
        if (line + 1 == d->errLine) plat_rect(GUTTER, y, 400 - GUTTER, LINE_H, 0x4A1E1E);
        else if (line == curLine) plat_rect(GUTTER, y, 400 - GUTTER, LINE_H, 0x1C2333);
    }
    for (int i = 0; i < g_nsegs; i++) C2D_DrawText(&g_segs[i].t, C2D_WithColor, g_segs[i].x, g_segs[i].y, 0.5f, SCALE, SCALE, g_segs[i].col);
    if ((osGetTime() / 500) % 2 == 0) plat_rect(x0 + cx, CODE_Y + (curLine - d->top) * LINE_H + 1, 1.5f, LINE_H - 2, 0xFFFFFF);
    /* line numbers on top of code scrolled to the left */
    plat_rect(0, CODE_Y, GUTTER, 240 - CODE_Y, 0x14181F);
    for (int i = 0; i < shown; i++) {
        char num[12];
        snprintf(num, sizeof num, "%d", d->top + i + 1);
        float w = plat3ds_text_width(num, 0.4f);
        plat_text(GUTTER - 4 - w, CODE_Y + i * LINE_H + 1, num, 0.4f, d->top + i == curLine ? 0xC0C8D0 : 0x5A6270);
    }
    /* title bar */
    plat_rect(0, 0, 400, 16, 0x1C2230);
    char info[96];
    snprintf(info, sizeof info, "%s%s", d->name, d->dirty ? " *" : "");
    plat_text(6, 1, info, 0.45f, 0xF0D080);
    if (g_isProject) {
        snprintf(info, sizeof info, "plik %d/%d", g_cd + 1, g_ndocs);
        plat_text(220, 1, info, 0.45f, 0x8090A0);
    }
    snprintf(info, sizeof info, "%d:%d", curLine + 1, d->cur - ls + 1);
    plat_text(400 - 6 - plat3ds_text_width(info, 0.45f), 1, info, 0.45f, 0x8090A0);
    if (g_status[0]) {
        plat_rect(0, 222, 400, 18, g_statusColor == 0xFF6060 ? 0x401818 : 0x183020);
        ui_wrap(6, 223, 388, g_status, 0.45f, g_statusColor, 0, 1);
    }
}

static const char* TOOLS[] = { "Pliki", "Uruchom", "Cofnij", "Help", "Zapisz" };

static void draw_bottom(void) {
    plat_screen(1);
    for (int i = 0; i < 5; i++) ui_button(i * 64, 0, 64, 30, TOOLS[i], i == 1 ? 0x2F6B3A : 0x34405A);
    char hint[200];
    const char* h = call_hint(hint, sizeof hint);
    plat_text(4, 31, h ? h : "B: usuń  Y: cofnij  X: Help  R: podpowiedź  L+D-pad: szybciej", 0.4f, h ? 0xF0D080 : 0x8090A0);
    for (int i = 0; i < NSUGGEST; i++) {
        float x = i * 80;
        plat_rect(x + 1, 45, 78, 31, i < g_nsug ? 0x24324A : 0x15181E);
        if (i >= g_nsug) continue;
        float scale = 0.5f, w = plat3ds_text_width(g_sug[i], scale);
        if (w > 74) { scale = 0.5f * 74 / w; w = 74; }
        plat_text(x + (80 - w) / 2, 52, g_sug[i], scale, i == 0 ? 0xFFFFFF : 0xC0D0E8);
    }
    kbd_draw();
}

/* ---------- actions ---------- */
static int valid_name(const char* s) {
    if (!*s || !(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (; *s; s++) if (!isword((unsigned char)*s)) return 0;
    return 1;
}

/* "namespace Kosmos;" of the project's first file, for new files */
static void project_namespace(char* out, int cap) {
    out[0] = 0;
    for (int i = 0; i < g_ndocs && !out[0]; i++) {
        const char* p = strstr(g_docs[i].text, "namespace ");
        if (!p) continue;
        p += 10;
        int n = 0;
        while (isword((unsigned char)p[n]) || p[n] == '.') n++;
        if (n && n < cap) { memcpy(out, p, n); out[n] = 0; }
    }
}

static void new_file(void) {
    if (!g_isProject) { ui_ask("Pojedynczy plik", "Ten program to jeden plik. Żeby mieć wiele plików, zrób projekt: L w menu.", "OK", "Wróć", 0); return; }
    if (g_ndocs == MAX_DOCS) { set_status(0xFF6060, "%s", "Za dużo plików w projekcie"); return; }
    char name[40] = "";
    for (;;) {
        if (!ui_prompt("Nowy plik", "Nazwa klasy, np. Player. Powstanie plik Player.cs z pustą klasą.", name, sizeof name)) return;
        int bad = !valid_name(name), exists = 0;
        for (int i = 0; i < g_ndocs; i++) {
            char cs[64];
            snprintf(cs, sizeof cs, "%s.cs", name);
            if (!strcasecmp(g_docs[i].name, cs)) exists = 1;
        }
        if (!bad && !exists) break;
        ui_ask(bad ? "Zła nazwa" : "Taki plik już jest",
               bad ? "Nazwa musi zaczynać się literą i mieć tylko litery, cyfry i _." : "Wybierz inną nazwę.", "OK", "Popraw", 0);
    }
    Doc* d = &g_docs[g_ndocs];
    memset(d, 0, sizeof *d);
    snprintf(d->name, sizeof d->name, "%s.cs", name);
    d->want = -1;
    char ns[64], text[256];
    project_namespace(ns, sizeof ns);
    int n = 0;
    if (ns[0]) n += snprintf(text + n, sizeof text - n, "namespace %s;\n\n", ns);
    n += snprintf(text + n, sizeof text - n, "class %s {\n    ", name);
    int cursor = n;
    n += snprintf(text + n, sizeof text - n, "\n}\n");
    if (!reserve(d, n)) return;
    memcpy(d->text, text, n + 1);
    d->len = n; d->cur = cursor;
    if (!doc_save(d)) { doc_free(d); set_status(0xFF6060, "%s", "Nie można utworzyć pliku"); return; }
    g_cd = g_ndocs++;
    g_wordsDirty = 2; g_lastEditCur = -1; g_textGen++;
    set_status(0x60E060, "Utworzono %s", d->name);
}

static void delete_file(int i) {
    if (g_ndocs == 1) { set_status(0xFF6060, "%s", "Projekt musi mieć choć jeden plik"); return; }
    char q[128];
    snprintf(q, sizeof q, "Plik %s zniknie z karty SD. Tego nie da się cofnąć.", g_docs[i].name);
    if (ui_ask("Usunąć plik?", q, "Usuń", "Nie", 0) != 1) return;
    char path[256];
    doc_path(&g_docs[i], path, sizeof path);
    if (remove(path) != 0) { set_status(0xFF6060, "Nie można usunąć %s", g_docs[i].name); return; }
    set_status(0x60E060, "Usunięto %s", g_docs[i].name);
    doc_free(&g_docs[i]);
    memmove(&g_docs[i], &g_docs[i + 1], sizeof(Doc) * (g_ndocs - i - 1));
    g_ndocs--;
    if (g_cd >= g_ndocs) g_cd = g_ndocs - 1;
    else if (g_cd > i) g_cd--;
    g_wordsDirty = 2; g_lastEditCur = -1; g_textGen++;
}

/* list of the project's files: open, add, delete */
static void files_screen(void) {
    int sel = g_cd;
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down(), rep = plat3ds_repeat();
        if ((k & (KEY_B | KEY_START | KEY_SELECT)) || ui_tapped(214, 200, 106, 40)) return;
        if ((rep & KEY_DOWN) && sel < g_ndocs - 1) sel++;
        if ((rep & KEY_UP) && sel > 0) sel--;
        if (k & KEY_A) { g_cd = sel; g_lastEditCur = -1; return; }
        for (int i = 0; i < g_ndocs && i < 7; i++)
            if (ui_tapped(0, 30 + i * 24, 320, 24)) { if (sel == i) { g_cd = sel; g_lastEditCur = -1; return; } sel = i; }
        if ((k & KEY_X) || ui_tapped(0, 200, 107, 40)) { new_file(); sel = g_cd; if (g_cd == g_ndocs - 1) return; }
        if ((k & KEY_Y) || ui_tapped(107, 200, 107, 40)) { delete_file(sel); if (sel >= g_ndocs) sel = g_ndocs - 1; }

        ui_frame_begin(0x101418, 0x0C0C0C);
        plat_text(10, 8, g_isProject ? "Pliki projektu" : "Plik", 0.7f, 0x7FB2FF);
        plat_text(10, 34, g_dir, 0.5f, 0xC0C8D0);
        ui_wrap(10, 60, 380, "A: otwórz   X: nowy plik   Y: usuń plik   B: wróć\n\n"
                "Wszystkie pliki .cs w folderze projektu kompilują się razem, więc klasy z różnych plików widzą się nawzajem.",
                0.5f, 0xA0A8B0, 0, 8);
        if (g_status[0]) ui_wrap(10, 200, 380, g_status, 0.5f, g_statusColor, 0, 2);
        plat_screen(1);
        plat_rect(0, 0, 320, 28, 0x1C2230);
        plat_text(8, 6, g_runName, 0.55f, 0xF0D080);
        for (int i = 0; i < g_ndocs && i < 7; i++) {
            float y = 30 + i * 24;
            if (i == sel) plat_rect(0, y, 320, 24, 0x2D4A80);
            char row[80];
            snprintf(row, sizeof row, "%.60s%s", g_docs[i].name, g_docs[i].dirty ? " *" : "");
            plat_text(10, y + 4, row, 0.5f, i == g_cd ? 0xFFFFFF : 0xC0C8D0);
        }
        ui_button(0, 200, 107, 40, "Nowy plik", 0x2F6B3A);
        ui_button(107, 200, 107, 40, "Usuń", 0x6B2F2F);
        ui_button(214, 200, 106, 40, "Wróć", 0x34405A);
        ui_frame_end();
    }
}

/* after a run: open the file and line the error names ("Ship.cs line 12: ...", "line 3 (Game.Update): ...") */
static void show_error(const char* e) {
    char file[64] = "";
    int line = 0, di = -1;
    if (!strncmp(e, "line ", 5)) { line = atoi(e + 5); di = 0; }
    else if (sscanf(e, "%63s line %d", file, &line) == 2)
        for (int i = 0; i < g_ndocs; i++) if (!strcmp(g_docs[i].name, file)) di = i;
    set_status(0xFF6060, "%s", e);
    if (di < 0 || line <= 0) return;
    g_cd = di;
    Doc* d = D;
    d->cur = pos_of_line(d, line - 1);
    d->errLine = line;
    d->want = -1;
    g_lastEditCur = -1;
}

static void run_program(void) {
    if (!save_all()) return;
    run_file(g_runName);
    kbd_reset();
    if (app_run_error[0]) show_error(app_run_error);
    else set_status(0x60E060, "%s", "Program zakończony bez błędów");
}

static void open_help(void) {
    Doc* d = D;
    char word[64];
    word_before_cursor(d, word, sizeof word);
    if (!word[0]) {                                   /* or the word the cursor is in */
        int s = d->cur, e = d->cur;
        while (s > 0 && (isword((unsigned char)d->text[s - 1]) || d->text[s - 1] == '.')) s--;
        while (e < d->len && isword((unsigned char)d->text[e])) e++;
        snprintf(word, sizeof word, "%.*s", e - s, d->text + s);
    }
    const char* ins = help_browse(word);
    kbd_reset();
    if (!ins) return;
    /* "Gfx.Ci" typed before the cursor is replaced by "Gfx.Circle(" */
    char before[64];
    int s = word_before_cursor(d, before, sizeof before);
    snapshot();
    if (before[0] && !strncmp(ins, before, strlen(before))) raw_delete(s, d->cur);
    raw_insert(ins, (int)strlen(ins));
    g_lastEditCur = -1;
}

static int confirm_exit(void) {
    if (!any_dirty()) return 1;
    int a = ui_ask("Niezapisane zmiany", "Zapisać pliki przed wyjściem?", "Zapisz", "Nie zapisuj", 1);
    if (a == 1) return save_all();
    return a == 0;
}

/* ---------- open / main loop ---------- */
static int cmpdoc(const void* a, const void* b) { return strcmp(((const Doc*)a)->name, ((const Doc*)b)->name); }

static void load_docs(const char* name) {
    g_ndocs = 0;
    if (!g_isProject) { if (doc_load(&g_docs[0], name)) g_ndocs = 1; return; }
    DIR* dir = opendir(g_dir);
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) && g_ndocs < MAX_DOCS) {
        size_t n = strlen(e->d_name);
        if (n > 3 && n < 64 && !strcmp(e->d_name + n - 3, ".cs") && doc_load(&g_docs[g_ndocs], e->d_name)) g_ndocs++;
    }
    closedir(dir);
    qsort(g_docs, g_ndocs, sizeof(Doc), cmpdoc);
}

void editor_open(const char* name) {
    size_t n = strlen(name);
    g_isProject = n > 0 && name[n - 1] == '/';
    snprintf(g_runName, sizeof g_runName, "%s", name);
    if (g_isProject) snprintf(g_dir, sizeof g_dir, "%s/%.*s", APP_DIR, (int)n - 1, name);
    else snprintf(g_dir, sizeof g_dir, "%s", APP_DIR);
    load_docs(name);
    if (!g_ndocs) { flog("editor: cannot open %s", name); return; }
    g_cd = 0;
    for (int i = 0; i < g_ndocs; i++) if (!strcmp(g_docs[i].name, "Game.cs")) g_cd = i;
    g_status[0] = 0;
    g_lastEditCur = -1;
    g_wordsDirty = 2;
    g_textGen++;
    flog("editor: %s (%d file(s))", name, g_ndocs);
    if (!g_codeBuf) g_codeBuf = C2D_TextBufNew(6000);

    hidSetRepeatParameters(20, 3);
    kbd_reset();
    int frames = 0, minFps = 0;
    u64 start = osGetTime(), cpu = 0, worst = 0;
    while (aptMainLoop()) {
        frames++;
        if (frames > 120 && (minFps == 0 || plat_fps() < minFps)) minFps = plat_fps();
        plat3ds_input();
        u32 k = plat3ds_down(), rep = plat3ds_repeat(), held = plat3ds_held();
        Doc* d = D;
        kbd_set_enter_label("Enter");
        if ((k & KEY_START) && confirm_exit()) break;

        const char* t = kbd_update();
        if (t) {
            if (!strcmp(t, KBD_BACKSPACE)) edit_backspace();
            else if (!strcmp(t, KBD_ENTER)) edit_enter();
            else edit_insert(t);
        }
        if (rep & KEY_B) edit_backspace();
        if (held & KEY_L) {
            if (rep & KEY_LEFT) { d->cur = line_start(d, d->cur); d->want = -1; }
            if (rep & KEY_RIGHT) { d->cur = line_end(d, d->cur); d->want = -1; }
            if (rep & KEY_UP) move_vert(d, -(VISIBLE - 1));
            if (rep & KEY_DOWN) move_vert(d, VISIBLE - 1);
        } else {
            if (rep & KEY_LEFT) move_left(d);
            if (rep & KEY_RIGHT) move_right(d);
            if (rep & KEY_UP) move_vert(d, -1);
            if (rep & KEY_DOWN) move_vert(d, 1);
        }
        if (k & KEY_Y) undo();
        if ((k & KEY_R) && g_nsug) apply_suggestion(0);
        for (int i = 0; i < NSUGGEST; i++) if (i < g_nsug && ui_tapped(i * 80, 44, 80, 33)) apply_suggestion(i);
        int tool = -1;
        for (int i = 0; i < 5; i++) if (ui_tapped(i * 64, 0, 64, 30)) tool = i;
        if ((k & KEY_SELECT) || tool == 0) files_screen();
        if (tool == 1) run_program();
        if (tool == 2) undo();
        if ((k & KEY_X) || tool == 3) open_help();
        if (tool == 4) { if (save_all()) set_status(0x60E060, "%s", "Zapisano"); }
        update_suggestions();

        ui_frame_begin(0x0F1218, 0x0C0C0C);
        u64 t0 = svcGetSystemTick();    /* CPU time of the drawing calls (not the wait for the GPU) */
        draw_top();
        draw_bottom();
        u64 dt = svcGetSystemTick() - t0;
        cpu += dt; if (dt > worst) worst = dt;
        ui_frame_end();
    }
    double secs = (osGetTime() - start) / 1000.0;
    flog("editor closed: %d frames in %.1f s = %.1f FPS (min %d), drawing avg %.2f ms, worst %.2f ms, %d lines in %s",
         frames, secs, secs > 0 ? frames / secs : 0, minFps, frames ? cpu * 1000.0 / SYSCLOCK_ARM11 / frames : 0,
         worst * 1000.0 / SYSCLOCK_ARM11, line_count(D), D->name);
    for (int i = 0; i < g_ndocs; i++) doc_free(&g_docs[i]);
    g_ndocs = 0;
}

int editor_new_project(char* made, int cap) {
    char name[40] = "";
    char path[200];
    for (;;) {
        if (!ui_prompt("Nowy projekt", "Nazwa projektu (litery, cyfry, _), np. MojaGra. "
                       "Powstanie folder sdmc:/minisharp/MojaGra/ z plikiem Game.cs.", name, sizeof name)) return 0;
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", APP_DIR, name);
        int bad = !valid_name(name), exists = !bad && stat(path, &st) == 0;
        if (!bad && !exists) break;
        ui_ask(bad ? "Zła nazwa" : "Taki projekt już jest",
               bad ? "Nazwa musi zaczynać się literą i mieć tylko litery, cyfry i _." : "Wybierz inną nazwę.", "OK", "Popraw", 0);
    }
    if (mkdir(path, 0777) != 0) { ui_ask("Błąd", "Nie można utworzyć folderu na karcie SD.", "OK", "Wróć", 0); return 0; }
    char file[240];
    snprintf(file, sizeof file, "%s/Game.cs", path);
    FILE* f = fopen(file, "wb");
    if (!f) { ui_ask("Błąd", "Nie można zapisać Game.cs.", "OK", "Wróć", 0); return 0; }
    fprintf(f,
        "// %s: nowy projekt MiniSharp. Kolejne pliki dodasz w edytorze (Pliki).\n"
        "namespace %s;\n\n"
        "class Game {\n"
        "    static float x = 200;\n\n"
        "    static void Main() {\n"
        "    }\n\n"
        "    static void Update() {\n"
        "        if (Input.Held(Btn.Left)) x -= 3;\n"
        "        if (Input.Held(Btn.Right)) x += 3;\n"
        "    }\n\n"
        "    static void Draw() {\n"
        "        Gfx.Clear(Color.Black);\n"
        "        Gfx.Circle(x, 120, 20, Color.Yellow);\n"
        "        Gfx.Text(10, 10, \"%s\", Color.White);\n"
        "    }\n"
        "}\n", name, name, name);
    fclose(f);
    flog("new project %s", name);
    snprintf(made, cap, "%s/", name);
    editor_open(made);
    return 1;
}
