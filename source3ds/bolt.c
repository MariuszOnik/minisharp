/* MiniSharp - "mini Bolt": the graph editor on the 3DS (see bolt.h and docs/mini-bolt.md).
   A port of web/grapheditor.js: same node sizes, colours, ports and gestures, adapted to one finger and buttons.
   Bottom screen: toolbar and the canvas.  Top screen: the selected node's description, the generator's notes.
   Touch: drag a node by its title bar; drag from a port to another port (or tap one port, then the other) to wire them;
   tap a wire to select it; drag from a wired input to an empty place to unwire it; tap a value to type it;
   drag the background to move the view.  L/R: zoom.  A: add a node.  X: delete the selection.  SELECT: save.  B/START: back. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "../src/fgraph.h"
#include "../src/platform.h"
#include "platform_3ds.h"
#include "keyboard.h"
#include "ui.h"
#include "help.h"
#include "bolt.h"

/* A compact layout for the small screen: in the browser a row is 26 px; here rows are 16 and the text keeps
   its size, so at 100 % about three times more nodes fit and stay readable. Positions in the file stay in the
   browser's units (K converts), so a graph looks the same on both, only denser here. */
#define K 0.6f
#define ROW 16.0f
#define HEAD 18.0f
#define CHAR 5.0f
#define GRID 6.0f
#define WEB_GRID 10.0f
#define BAR_H 24
#define SW 320.0f
#define SH 240.0f
#define TEXT_SCALE 0.37f                /* about 11 px at 100 %: fits a 16 px row */
#define FLOW_COLOR 0xDFE5EE
#define VALUE_COLOR 0x82AAFF

typedef struct {                        /* layout of one node, as layout() in grapheditor.js */
    const FDef* d;
    float w, h;
    int dataRows;
    C2D_Text title, data;
    C2D_Text* inText; C2D_Text* outText;   /* labels (empty label: not drawn) */
    char* inEmpty; char* outEmpty;
} Lay;

static const BoltHost* H;
static JVal* G;                         /* the graph (owned) */
static FCatalog CAT, USER;
static FGenResult ISS;                  /* the generator's notes for the current graph */
static Lay* L; static int nL;
static C2D_TextBuf TB;
static float vx = 20, vy = 40, vs = 1.0f; /* view: screen = world * vs + (vx, vy) */
enum { SEL_NONE, SEL_NODE, SEL_EDGE };
static int selKind, selIndex;           /* node index into graph.nodes, or edge index */
static int pendNode = -1, pendDir, pendPort; /* a port waiting for the second tap (dir 0 in, 1 out) */
static int tempOn; static float tx0, ty0, tx1, ty1; static int tempFlow;
static char g_note[200]; static uint32_t g_noteColor;

/* ---------------------------------------------------------------- graph access */
static JVal* nodes(void) { return json_get(G, "nodes"); }
static JVal* edges(void) { return json_get(G, "edges"); }
static int nnodes(void) { JVal* n = nodes(); return n ? n->n : 0; }
static int nedges(void) { JVal* e = edges(); return e ? e->n : 0; }
static JVal* node(int i) { return nodes()->items[i]; }
static const char* sget(const JVal* o, const char* k) { const JVal* v = json_get(o, k); return v && v->t == JS_STR ? v->s : ""; }
static float nx(int i) { return (float)json_number(json_get(node(i), "x"), 0) * K; }
static float ny(int i) { return (float)json_number(json_get(node(i), "y"), 0) * K; }
/* position in screen-layout units -> the file's (browser) units, snapped to its grid when asked */
static JVal* file_coord(float v, int snap) { v /= K; return json_num(snap ? roundf(v / WEB_GRID) * WEB_GRID : roundf(v)); }
static void set_pos(int i, float x, float y, int snap) { json_set(node(i), "x", file_coord(x, snap)); json_set(node(i), "y", file_coord(y, snap)); }
static int node_index(const char* id) { for (int i = nnodes() - 1; i >= 0; i--) if (!strcmp(sget(node(i), "id"), id)) return i; return -1; }
static const FDef* def_of(int i) { return fg_find(&CAT, sget(node(i), "type")); }
static JVal* data_of(int i) {
    JVal* d = json_get(node(i), "data");
    if (!d || d->t != JS_OBJ) { json_set(node(i), "data", json_new(JS_OBJ)); d = json_get(node(i), "data"); }
    return d;
}
static int is_connected(int i, int dirOut, const char* port) {
    const char* id = sget(node(i), "id");
    for (int k = 0; k < nedges(); k++) {
        const JVal* e = edges()->items[k];
        if (dirOut ? (!strcmp(sget(e, "from"), id) && !strcmp(sget(e, "out"), port)) : (!strcmp(sget(e, "to"), id) && !strcmp(sget(e, "in"), port))) return 1;
    }
    return 0;
}
static void note(uint32_t c, const char* m) { snprintf(g_note, sizeof g_note, "%s", m); g_noteColor = c; }

/* ---------------------------------------------------------------- text helpers */
static int ulen(const char* s) { int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n; }
static void short_text(char* out, int cap, const char* t, int n) {         /* shortText: n characters, "…" when cut */
    if (ulen(t) <= n) { snprintf(out, (size_t)cap, "%s", t); return; }
    int k = 0, c = 0;
    while (t[k] && c < n - 1) { k++; while ((t[k] & 0xC0) == 0x80) k++; c++; }
    snprintf(out, (size_t)cap, "%.*s…", k, t);
}
static const char* default_text(int i, const FPort* p, char* buf, int cap) {
    const JVal* data = json_get(node(i), "data");
    const JVal* defs = data ? json_get(data, "defaults") : NULL;
    const JVal* given = defs ? json_get(defs, p->name) : NULL;
    if (given && !(given->t == JS_STR && !given->s[0])) { const char* t = json_text(given, buf, cap); return t ? t : "null"; }
    return p->def ? p->def : "";
}
static const char* data_text(int i, const FDef* d, char* buf, int cap) {
    const JVal* data = json_get(node(i), "data");
    if (d->custom) { const JVal* v = data ? json_get(data, "name") : NULL; return v && json_truthy(v) ? json_text(v, buf, cap) : "(nazwa metody)"; }
    if (!strcmp(d->kind, "lit")) { const JVal* v = data ? json_get(data, "value") : NULL; const char* t = v ? json_text(v, buf, cap) : NULL; return t ? t : (v ? "null" : "0"); }
    const JVal* v = data ? json_get(data, "name") : NULL;
    return v && json_truthy(v) ? json_text(v, buf, cap) : "(wybierz zmienną)";
}
static int hidden_label(const char* n) { return !strcmp(n, "enter") || !strcmp(n, "exit") || !strcmp(n, "result") || !strcmp(n, "value"); }
static uint32_t head_color(const FDef* d) {
    if (!d) return 0xFF7A7A;
    const char* c = d->category;
    if (!strcmp(c, "Zdarzenia")) return 0xE5736B;
    if (!strcmp(c, "Sterowanie")) return 0xC792EA;
    if (!strcmp(c, "Zmienne")) return 0x7FD1C7;
    if (!strcmp(c, "Matematyka")) return 0xFFD479;
    if (!strcmp(c, "Porównania") || !strcmp(c, "Logika")) return 0xF78C6C;
    return VALUE_COLOR;
}
static uint32_t mix(uint32_t a, uint32_t b, float t) {   /* a over b with opacity t */
    int r = (int)(((a >> 16) & 255) * t + ((b >> 16) & 255) * (1 - t));
    int g = (int)(((a >> 8) & 255) * t + ((b >> 8) & 255) * (1 - t));
    int bl = (int)((a & 255) * t + (b & 255) * (1 - t));
    return (uint32_t)((r << 16) | (g << 8) | bl);
}

/* ---------------------------------------------------------------- layout and text (rebuilt after every change) */
static void free_layout(void) {
    for (int i = 0; i < nL; i++) { free(L[i].inText); free(L[i].outText); free(L[i].inEmpty); free(L[i].outEmpty); }
    free(L); L = NULL; nL = 0;
}
static void parse(C2D_Text* t, const char* s) { C2D_TextParse(t, TB, s); C2D_TextOptimize(t); }

static void relayout(void) {
    free_layout();
    if (TB) C2D_TextBufDelete(TB);
    int glyphs = 256;
    for (int i = 0; i < nnodes(); i++) { const FDef* d = def_of(i); glyphs += 80 + (d ? (d->nin + d->nout) * 40 : 0); }
    TB = C2D_TextBufNew(glyphs);
    nL = nnodes();
    L = (Lay*)calloc((size_t)(nL ? nL : 1), sizeof(Lay));
    char buf[160], b2[96], b3[64];
    for (int i = 0; i < nL; i++) {
        Lay* l = &L[i];
        const FDef* d = l->d = def_of(i);
        if (!d) {
            l->w = 100; l->h = HEAD + ROW;
            snprintf(buf, sizeof buf, "? %s", sget(node(i), "type"));
            parse(&l->title, buf);
            continue;
        }
        l->dataRows = !strcmp(d->kind, "lit") || !strcmp(d->kind, "getvar") || !strcmp(d->kind, "setvar") || d->custom ? 1 : 0;
        l->inText = (C2D_Text*)calloc((size_t)(d->nin ? d->nin : 1), sizeof(C2D_Text));
        l->outText = (C2D_Text*)calloc((size_t)(d->nout ? d->nout : 1), sizeof(C2D_Text));
        l->inEmpty = (char*)calloc((size_t)(d->nin ? d->nin : 1), 1);
        l->outEmpty = (char*)calloc((size_t)(d->nout ? d->nout : 1), 1);
        int left = 0, right = 0;
        for (int k = 0; k < d->nin; k++) {
            const FPort* p = &d->in[k];
            if (p->flow) snprintf(buf, sizeof buf, "%s", hidden_label(p->name) ? "" : p->name);
            else if (!is_connected(i, 0, p->name)) { short_text(b2, sizeof b2, default_text(i, p, b3, sizeof b3), 10); snprintf(buf, sizeof buf, "%s  %s", p->name, b2); }
            else snprintf(buf, sizeof buf, "%s", p->name);
            if (ulen(buf) > left) left = ulen(buf);
            l->inEmpty[k] = !buf[0];
            if (buf[0]) parse(&l->inText[k], buf);
        }
        for (int k = 0; k < d->nout; k++) {
            const FPort* p = &d->out[k];
            snprintf(buf, sizeof buf, "%s", hidden_label(p->name) ? "" : p->name);
            if (ulen(buf) > right) right = ulen(buf);
            l->outEmpty[k] = !buf[0];
            if (buf[0]) parse(&l->outText[k], buf);
        }
        int dataLen = 0;
        if (l->dataRows) { short_text(buf, sizeof buf, data_text(i, d, b3, sizeof b3), 24); parse(&l->data, buf); short_text(b2, sizeof b2, data_text(i, d, b3, sizeof b3), 16); dataLen = ulen(b2) + 2; }
        parse(&l->title, d->title);
        float w = fmaxf(76, fmaxf(ulen(d->title) * CHAR * 1.05f + 16, fmaxf((left + right) * CHAR + 34, dataLen * CHAR + 18)));
        if (w > 210) w = 210;
        l->w = ceilf(w / GRID) * GRID;
        int rows = d->nin > d->nout ? d->nin : d->nout;
        l->h = HEAD + (l->dataRows + rows) * ROW + 5;
    }
}
static float in_y(int i, int k) { return HEAD + L[i].dataRows * ROW + k * ROW + ROW / 2; }
static float out_y(int i, int k) { return HEAD + L[i].dataRows * ROW + k * ROW + ROW / 2; }
static int port_index(const FDef* d, int out, const char* name) {
    int n = out ? d->nout : d->nin; const FPort* ps = out ? d->out : d->in;
    for (int k = 0; k < n; k++) if (!strcmp(ps[k].name, name)) return k;
    return -1;
}

/* ---------------------------------------------------------------- the code behind the graph */
static void regenerate_issues(void) {
    fg_result_free(&ISS);
    fg_generate(G, &CAT, 0, &ISS);
}

/* every change: the file's code made again from the graph (one undo step), new layout and notes */
static void changed(void) {
    FGenResult r;
    fg_generate(G, &CAT, 1, &r);
    H->replace(r.code);
    fg_result_free(&r);
    regenerate_issues();
    relayout();
}

static void build_catalog(void) {
    fg_catalog_free(&CAT); fg_catalog_free(&USER);
    for (int i = 0; i < H->ndocs(); i++) fg_user_nodes(&USER, H->doc_name(i), H->doc_text(i));
    int nh; const FHelpEntry* he = (const FHelpEntry*)help_api_entries(&nh);
    fg_catalog_build(&CAT, he, nh, &USER);
}

static int load_from_text(void) {
    JVal* g = fg_extract(H->text());
    if (!g) return 0;
    json_free(G); G = g;
    if (!json_get(G, "variables")) json_set(G, "variables", json_new(JS_ARR));
    if (selKind == SEL_NODE && selIndex >= nnodes()) selKind = SEL_NONE;
    if (selKind == SEL_EDGE && selIndex >= nedges()) selKind = SEL_NONE;
    pendNode = -1;
    regenerate_issues();
    relayout();
    return 1;
}

/* ---------------------------------------------------------------- view */
static float SX(float wx) { return wx * vs + vx; }
static float SY(float wy) { return wy * vs + vy; }
static void to_world(float x, float y, float* wx, float* wy) { *wx = (x - vx) / vs; *wy = (y - vy) / vs; }
static void zoom_at(float cx, float cy, float k) {
    float s = vs * k;
    if (s < 0.25f) s = 0.25f;
    if (s > 2.0f) s = 2.0f;
    float f = s / vs;
    vx = cx - (cx - vx) * f; vy = cy - (cy - vy) * f; vs = s;
}
static void fit(void) {
    if (!nnodes()) { vx = 20; vy = BAR_H + 20; vs = 0.6f; return; }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < nnodes(); i++) {
        x0 = fminf(x0, nx(i)); y0 = fminf(y0, ny(i));
        x1 = fmaxf(x1, nx(i) + L[i].w); y1 = fmaxf(y1, ny(i) + L[i].h);
    }
    float pad = 12, h = SH - BAR_H;
    float s = fminf((SW - 2 * pad) / (x1 - x0), (h - 2 * pad) / (y1 - y0));
    s = fmaxf(0.35f, fminf(1.3f, s));
    vs = s; vx = (SW - (x1 - x0) * s) / 2 - x0 * s; vy = BAR_H + (h - (y1 - y0) * s) / 2 - y0 * s;
}
static void center_on(int i) {
    vx = SW / 2 - (nx(i) + L[i].w / 2) * vs;
    vy = BAR_H + (SH - BAR_H) / 2 - (ny(i) + L[i].h / 2) * vs;
}

/* ---------------------------------------------------------------- drawing */
static u32 C(uint32_t rgb) { return plat3ds_color(rgb); }
static void bez(float ax, float ay, float bx, float by, float t, float* x, float* y) {   /* wirePath of the browser */
    float dx = fmaxf(24, fabsf(bx - ax) / 2), u = 1 - t;
    float c1x = ax + dx, c2x = bx - dx;
    *x = u * u * u * ax + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * bx;
    *y = u * u * u * ay + 3 * u * u * t * ay + 3 * u * t * t * by + t * t * t * by;
}
static void draw_wire(float ax, float ay, float bx, float by, uint32_t col, float width) {   /* world coordinates */
    float minx = fminf(ax, bx) - 40, maxx = fmaxf(ax, bx) + 40, miny = fminf(ay, by), maxy = fmaxf(ay, by);
    if (SX(maxx) < 0 || SX(minx) > SW || SY(maxy) < BAR_H || SY(miny) > SH) return;
    int segs = vs >= 0.5f ? 14 : 6;
    float lx = SX(ax), ly = SY(ay);
    for (int s = 1; s <= segs; s++) {
        float x, y; bez(ax, ay, bx, by, (float)s / segs, &x, &y);
        C2D_DrawLine(lx, ly, C(col), SX(x), SY(y), C(col), width * vs, 0.5f);
        lx = SX(x); ly = SY(y);
    }
}
static void port_pos(int i, int out, const char* name, float* x, float* y) {
    const FDef* d = L[i].d;
    int k = d ? port_index(d, out, name) : -1;
    *x = nx(i) + (out ? L[i].w : 0);
    *y = ny(i) + (k >= 0 ? (out ? out_y(i, k) : in_y(i, k)) : HEAD);
}
static int edge_is_flow(const JVal* e) {
    int a = node_index(sget(e, "from"));
    if (a < 0 || !L[a].d) return 0;
    int k = port_index(L[a].d, 1, sget(e, "out"));
    return k >= 0 && L[a].d->out[k].flow;
}
static void ring(float x, float y, float r, uint32_t col) {
    float px = x + r, py = y;
    for (int s = 1; s <= 16; s++) {
        float a = s * 6.2831853f / 16, qx = x + cosf(a) * r, qy = y + sinf(a) * r;
        C2D_DrawLine(px, py, C(col), qx, qy, C(col), 2.0f, 0.5f);
        px = qx; py = qy;
    }
}
static void port_shape(float x, float y, int flow, int filled, uint32_t col) {          /* screen coordinates */
    float r = 4.5f * vs;
    if (flow) {
        C2D_DrawTriangle(x - r, y - r * 7 / 6, C(col), x + r, y, C(col), x - r, y + r * 7 / 6, C(col), 0.5f);
        if (!filled) { float q = r * 0.55f; C2D_DrawTriangle(x - q, y - q * 7 / 6, C(0x171C25), x + q * 0.8f, y, C(0x171C25), x - q, y + q * 7 / 6, C(0x171C25), 0.5f); }
    } else {
        C2D_DrawCircleSolid(x, y, 0.5f, r, C(col));
        if (!filled) C2D_DrawCircleSolid(x, y, 0.5f, r * 0.6f, C(0x171C25));
    }
}
static int has_issue(const char* id) {
    for (int k = 0; k < ISS.nwarnings; k++) if (ISS.warnings[k].node && !strcmp(ISS.warnings[k].node, id)) return 1;
    for (int k = 0; k < ISS.nerrors; k++) if (ISS.errors[k].node && !strcmp(ISS.errors[k].node, id)) return 1;
    return 0;
}
static void text_at(C2D_Text* t, float x, float y, float sc, uint32_t col, int alignRight) {
    float w = 0;
    if (alignRight) { C2D_TextGetDimensions(t, sc, sc, &w, NULL); x -= w; }
    C2D_DrawText(t, C2D_WithColor, x, y, 0.5f, sc, sc, C(col));
}

static void draw_graph(void) {
    /* wires */
    for (int k = 0; k < nedges(); k++) {
        const JVal* e = edges()->items[k];
        int a = node_index(sget(e, "from")), b = node_index(sget(e, "to"));
        if (a < 0 || b < 0) continue;
        float ax, ay, bx, by;
        port_pos(a, 1, sget(e, "out"), &ax, &ay); port_pos(b, 0, sget(e, "in"), &bx, &by);
        int flow = edge_is_flow(e), sel = selKind == SEL_EDGE && selIndex == k;
        draw_wire(ax, ay, bx, by, sel ? 0xFFFFFF : flow ? FLOW_COLOR : VALUE_COLOR, sel ? 4 : flow ? 3 : 2.2f);
    }
    if (tempOn) draw_wire(tx0, ty0, tx1, ty1, tempFlow ? 0x9098A8 : 0x5A78B0, 2.5f);
    /* nodes */
    float ts = TEXT_SCALE * vs;
    int withText = vs >= 0.55f;           /* smaller text cannot be read on this screen */
    for (int i = 0; i < nnodes(); i++) {
        Lay* l = &L[i];
        float x = SX(nx(i)), y = SY(ny(i)), w = l->w * vs, h = l->h * vs;
        if (x > SW || y > SH || x + w < 0 || y + h < BAR_H) continue;
        int sel = selKind == SEL_NODE && selIndex == i;
        uint32_t color = head_color(l->d);
        uint32_t border = sel ? 0xFFFFFF : has_issue(sget(node(i), "id")) ? 0xFFB454 : 0x3C4A60;
        float bw = sel ? 2.5f : 1.5f;
        C2D_DrawRectSolid(x - bw, y - bw, 0.5f, w + 2 * bw, h + 2 * bw, C(border));
        C2D_DrawRectSolid(x, y, 0.5f, w, h, C(0x1E2531));
        C2D_DrawRectSolid(x, y, 0.5f, w, HEAD * vs, C(mix(color, 0x1E2531, 0.28f)));
        if (withText) text_at(&l->title, x + 6 * vs, y + 3 * vs, ts, color, 0);
        const FDef* d = l->d;
        if (!d) continue;
        if (l->dataRows) {
            C2D_DrawRectSolid(x + 5 * vs, y + (HEAD + 1) * vs, 0.5f, w - 10 * vs, (ROW - 2) * vs, C(0x2A3342));
            C2D_DrawRectSolid(x + 6 * vs, y + (HEAD + 2) * vs, 0.5f, w - 12 * vs, (ROW - 4) * vs, C(0x11151C));
            if (withText) {
                float tw; C2D_TextGetDimensions(&l->data, ts, ts, &tw, NULL);
                text_at(&l->data, x + (w - tw) / 2, y + (HEAD + 2) * vs, ts, 0xE8ECF2, 0);
            }
        }
        for (int k = 0; k < d->nin; k++) {
            const FPort* p = &d->in[k];
            float py = y + in_y(i, k) * vs;
            int conn = is_connected(i, 0, p->name);
            if (withText && !l->inEmpty[k]) text_at(&l->inText[k], x + 9 * vs, py - 6 * vs, ts, !p->flow && !conn ? 0xC3E88D : 0xA6ACCD, 0);
            port_shape(x, py, p->flow, conn, p->flow ? FLOW_COLOR : VALUE_COLOR);
            if (pendNode == i && pendDir == 0 && pendPort == k) ring(x, py, 8 * vs, 0xFFD479);
        }
        for (int k = 0; k < d->nout; k++) {
            const FPort* p = &d->out[k];
            float py = y + out_y(i, k) * vs;
            int conn = is_connected(i, 1, p->name);
            if (withText && !l->outEmpty[k]) text_at(&l->outText[k], x + w - 9 * vs, py - 6 * vs, ts, 0xA6ACCD, 1);
            port_shape(x + w, py, p->flow, conn, p->flow ? FLOW_COLOR : VALUE_COLOR);
            if (pendNode == i && pendDir == 1 && pendPort == k) ring(x + w, py, 8 * vs, 0xFFD479);
        }
    }
}

static const char* BAR[] = { "Kod", "+ Węzeł", "Usuń", "Cofnij", "Więcej" };
static void draw_bar(void) {
    for (int i = 0; i < 5; i++)
        ui_button(i * 64, 0, 64, BAR_H, BAR[i], i == 1 ? 0x2F6B3A : i == 2 ? (selKind ? 0x8B3A3A : 0x3A2A2A) : 0x34405A);
}

static void draw_top(void) {
    plat_rect(0, 0, 400, 16, 0x1C2230);
    char s[200];
    snprintf(s, sizeof s, "Graf: %s", H->fileName);
    plat_text(6, 1, s, 0.45f, 0xF0D080);
    snprintf(s, sizeof s, "zoom %.0f%%", vs * 100);
    plat_text(400 - 6 - plat3ds_text_width(s, 0.45f), 1, s, 0.45f, 0x8090A0);
    float y = 22;
    if (selKind == SEL_NODE && selIndex < nnodes()) {
        const FDef* d = L[selIndex].d;
        if (d) {
            plat_text(8, y, d->title, 0.6f, head_color(d)); y += 20;
            plat_text(8, y, d->category, 0.42f, 0x8090A0); y += 14;
            if (d->sig) { y += 15 * ui_wrap(8, y, 384, d->sig, 0.45f, 0xC0D0E8, 0, 2); }
            if (d->doc && d->doc[0]) y += 15 * ui_wrap(8, y, 384, d->doc, 0.45f, 0xA0A8B0, 0, 4);
        } else plat_text(8, y, "Nieznany węzeł (nie ma go w katalogu)", 0.5f, 0xFF7A7A), y += 18;
    } else if (selKind == SEL_EDGE) { plat_text(8, y, "Zaznaczone połączenie: X albo Usuń je kasuje", 0.5f, 0xFFFFFF); y += 18; }
    else {
        y += 15 * ui_wrap(8, y, 384, "Dotknij węzła albo połączenia, żeby je zaznaczyć. Przeciągnij z portu do portu, żeby połączyć "
                          "(albo dotknij jednego, potem drugiego). Przeciągnij z podłączonego wejścia w puste miejsce, żeby odpiąć. "
                          "Dotknij wartości, żeby ją wpisać.", 0.45f, 0xA0A8B0, 0, 5);
    }
    y += 6;
    int n = ISS.nerrors + ISS.nwarnings;
    if (n) {
        snprintf(s, sizeof s, "Uwagi (%d):", n);
        plat_text(8, y, s, 0.45f, 0xFFB454); y += 15;
        for (int k = 0; k < n && y < 200; k++) {
            const FMsg* m = k < ISS.nerrors ? &ISS.errors[k] : &ISS.warnings[k - ISS.nerrors];
            y += 14 * ui_wrap(14, y, 378, m->message, 0.4f, k < ISS.nerrors ? 0xFF7A7A : 0xE0C080, 0, 2);
        }
    }
    if (g_note[0]) { plat_rect(0, 206, 400, 16, g_noteColor == 0xFF6060 ? 0x401818 : 0x183020); plat_text(6, 207, g_note, 0.42f, g_noteColor); }
    plat_text(6, 224, "A: węzeł  X: usuń  L/R: zoom  Y: dopasuj  SELECT: zapisz  B: kod", 0.4f, 0x7080A0);
}

static void frame(void) {
    ui_frame_begin(0x0F1218, 0x141922);
    draw_top();
    plat_screen(1);
    draw_graph();
    draw_bar();
    ui_frame_end();
}

/* ---------------------------------------------------------------- editing */
static void next_id(char* out, int cap) {
    for (int k = 1; ; k++) { snprintf(out, (size_t)cap, "n%d", k); if (node_index(out) < 0) return; }
}

static int wire_ports(int na, int da, int pa, int nb, int db, int pb) {
    if (da == db) { note(0xFF6060, "Połącz wyjście (po prawej) z wejściem (po lewej)."); return 0; }
    int on = da ? na : nb, op = da ? pa : pb, in = da ? nb : na, ip = da ? pb : pa;
    if (on == in) { note(0xFF6060, "Nie można połączyć węzła z samym sobą."); return 0; }
    const FDef* dout = L[on].d, *din = L[in].d;
    if (!dout || !din) return 0;
    const FPort* po = &dout->out[op], *pi = &din->in[ip];
    if (!!po->flow != !!pi->flow) { note(0xFF6060, po->flow ? "Przepływ (trójkąt) łączy się tylko z przepływem." : "Wartość (kółko) łączy się tylko z wartością."); return 0; }
    const char* oid = sget(node(on), "id"), *iid = sget(node(in), "id");
    for (int k = nedges() - 1; k >= 0; k--) {
        const JVal* e = edges()->items[k];
        int same = !strcmp(sget(e, "from"), oid) && !strcmp(sget(e, "out"), po->name) && !strcmp(sget(e, "to"), iid) && !strcmp(sget(e, "in"), pi->name);
        if (same) return 0;
    }
    for (int k = nedges() - 1; k >= 0; k--) {
        const JVal* e = edges()->items[k];
        if (!pi->flow && !strcmp(sget(e, "to"), iid) && !strcmp(sget(e, "in"), pi->name)) json_remove_at(edges(), k);      /* one wire into a value input */
        else if (po->flow && !strcmp(sget(e, "from"), oid) && !strcmp(sget(e, "out"), po->name)) json_remove_at(edges(), k); /* one wire out of a flow output */
    }
    JVal* e = json_new(JS_OBJ);
    json_set(e, "from", json_str(oid)); json_set(e, "out", json_str(po->name));
    json_set(e, "to", json_str(iid)); json_set(e, "in", json_str(pi->name));
    json_push(edges(), e);
    selKind = SEL_NONE;
    return 1;
}

static void unwire_input(int i, int k) {          /* every wire into this input */
    const char* id = sget(node(i), "id"), *name = L[i].d->in[k].name;
    for (int e = nedges() - 1; e >= 0; e--)
        if (!strcmp(sget(edges()->items[e], "to"), id) && !strcmp(sget(edges()->items[e], "in"), name)) json_remove_at(edges(), e);
}

static void remove_selection(void) {
    if (selKind == SEL_NODE) {
        char id[64]; snprintf(id, sizeof id, "%s", sget(node(selIndex), "id"));
        json_remove_at(nodes(), selIndex);
        for (int e = nedges() - 1; e >= 0; e--)
            if (!strcmp(sget(edges()->items[e], "from"), id) || !strcmp(sget(edges()->items[e], "to"), id)) json_remove_at(edges(), e);
    } else if (selKind == SEL_EDGE) json_remove_at(edges(), selIndex);
    else { note(0xC0C8D0, "Najpierw dotknij węzła albo połączenia."); return; }
    selKind = SEL_NONE; pendNode = -1;
    changed();
}

static int is_ident(const char* s) {
    if (!(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (s++; *s; s++) if (!(isalnum((unsigned char)*s) || *s == '_')) return 0;
    return 1;
}
static void trim(char* s) {
    char* p = s; while (*p == ' ') p++;
    memmove(s, p, strlen(p) + 1);
    int n = (int)strlen(s); while (n && s[n - 1] == ' ') s[--n] = 0;
}

/* text typed for a string (or object) input gets its quotes; a number, true/false/null, a name or a call stays as typed */
static int needs_quotes(const char* type, const char* v) {
    if (v[0] == '"' || v[0] == '\'') return 0;
    if (type && !strcmp(type, "string")) return !is_ident(v);
    if (type && !strcmp(type, "object")) {
        const char* p = v; if (*p == '-') p++;
        int num = isdigit((unsigned char)*p) != 0;
        for (const char* q = p; num && *q; q++) if (!(isalnum((unsigned char)*q) || *q == '_' || *q == '.')) num = 0;
        if (num || !strcmp(v, "true") || !strcmp(v, "false") || !strcmp(v, "null")) return 0;
        if (isalpha((unsigned char)v[0]) || v[0] == '_') {           /* Name.Name or a call Name(...) */
            const char* q = v; while (isalnum((unsigned char)*q) || *q == '_' || *q == '.') q++;
            if (!*q || (*q == '(' && v[strlen(v) - 1] == ')')) return 0;
        }
        return 1;
    }
    return 0;
}

static void edit_default(int i, int k) {
    const FDef* d = L[i].d;
    const FPort* p = &d->in[k];
    char buf[200], b2[64], title[120];
    snprintf(buf, sizeof buf, "%s", default_text(i, p, b2, sizeof b2));
    snprintf(title, sizeof title, "%s: %s%s%s%s", d->title, p->name, p->type && p->type[0] ? " (" : "", p->type ? p->type : "", p->type && p->type[0] ? ")" : "");
    const char* hint = p->type && !strcmp(p->type, "string") ? "Tekst: cudzysłów dodam sam, chyba że to nazwa zmiennej. Puste = wartość domyślna."
                     : "Wyrażenie C#, np. 10, 1.5f, x + 1, Btn.A. Puste = wartość domyślna.";
    if (!ui_prompt(title, hint, buf, sizeof buf)) { kbd_reset(); return; }
    kbd_reset();
    trim(buf);
    JVal* data = data_of(i);
    JVal* defs = json_get(data, "defaults");
    if (!defs || defs->t != JS_OBJ) { json_set(data, "defaults", json_new(JS_OBJ)); defs = json_get(data, "defaults"); }
    if (!buf[0]) {
        for (int q = 0; q < defs->n; q++) if (!strcmp(defs->keys[q], p->name)) { json_remove_at(defs, q); break; }
    } else if (needs_quotes(p->type, buf)) {
        char q[420]; int n = 0; q[n++] = '"';
        for (char* s = buf; *s && n < (int)sizeof q - 3; s++) { if (*s == '\\' || *s == '"') q[n++] = '\\'; q[n++] = *s; }
        q[n++] = '"'; q[n] = 0;
        json_set(defs, p->name, json_str(q));
    } else json_set(defs, p->name, json_str(buf));
    changed();
}

static void edit_data(int i) {
    const FDef* d = L[i].d;
    JVal* data = data_of(i);
    char buf[160], b2[64];
    snprintf(buf, sizeof buf, "%s", d->custom || strcmp(d->kind, "lit") ? (json_truthy(json_get(data, "name")) ? json_text(json_get(data, "name"), b2, sizeof b2) : "")
                                            : data_text(i, d, b2, sizeof b2));
    if (d->custom) {
        if (!ui_prompt("Nazwa metody", "Z kodu wywołasz ją jako Klasa.Nazwa(), np. Graph.Tick().", buf, sizeof buf)) { kbd_reset(); return; }
        trim(buf);
        if (!is_ident(buf)) { note(0xFF6060, "Nazwa metody: litery, cyfry i _, nie może zaczynać się od cyfry."); kbd_reset(); return; }
        if (!strcmp(buf, "Main") || !strcmp(buf, "Update") || !strcmp(buf, "Draw")) { note(0xFF6060, "Main, Update i Draw mają własne węzły zdarzeń."); kbd_reset(); return; }
        json_set(data, "name", json_str(buf));
    } else if (!strcmp(d->kind, "lit")) {
        if (!ui_prompt("Wartość", "Wyrażenie, np. 10, 1.5f, true, \"tekst\", Btn.A", buf, sizeof buf)) { kbd_reset(); return; }
        trim(buf);
        json_set(data, "value", json_str(buf[0] ? buf : "0"));
    } else {
        char hint[300] = "Nazwa zmiennej grafu";
        JVal* vars = json_get(G, "variables");
        if (vars && vars->n) {
            strcat(hint, ": ");
            for (int k = 0; k < vars->n && strlen(hint) < 250; k++) { if (k) strcat(hint, ", "); strcat(hint, sget(vars->items[k], "name")); }
        } else strcat(hint, " (najpierw dodaj ją: Więcej > Zmienne grafu).");
        if (!buf[0] && vars && vars->n) snprintf(buf, sizeof buf, "%s", sget(vars->items[0], "name"));
        if (!ui_prompt("Zmienna", hint, buf, sizeof buf)) { kbd_reset(); return; }
        trim(buf);
        if (!is_ident(buf)) { note(0xFF6060, "Nazwa zmiennej: litery, cyfry i _, nie może zaczynać się od cyfry."); kbd_reset(); return; }
        json_set(data, "name", json_str(buf));
    }
    kbd_reset();
    changed();
}

/* the nearest free place around world point (cx, cy) for a node w x h (freeSpot of the browser) */
static void free_spot(float cx, float cy, float w, float h, float* ox, float* oy) {
    float bx = roundf((cx - w / 2) / GRID) * GRID, by = roundf((cy - h / 2) / GRID) * GRID;
    for (int r = 0; r < 14; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if ((abs(dx) > abs(dy) ? abs(dx) : abs(dy)) != r) continue;
                float x = bx + dx * 30, y = by + dy * 30;
                int busy = 0;
                for (int i = 0; i < nnodes() && !busy; i++)
                    if (x < nx(i) + L[i].w + 12 && x + w + 12 > nx(i) && y < ny(i) + L[i].h + 12 && y + h + 12 > ny(i)) busy = 1;
                if (!busy) { *ox = x; *oy = y; return; }
            }
    *ox = bx; *oy = by;
}


static JVal* make_node(const char* id, const FDef* d, float x, float y, const char* methodName) {
    JVal* n = json_new(JS_OBJ);
    json_set(n, "id", json_str(id)); json_set(n, "type", json_str(d->id));
    json_set(n, "x", file_coord(x, 1)); json_set(n, "y", file_coord(y, 1));
    JVal* data = json_new(JS_OBJ);
    if (!strcmp(d->kind, "lit")) json_set(data, "value", json_str("0"));
    if (!strcmp(d->kind, "getvar") || !strcmp(d->kind, "setvar")) {
        JVal* vars = json_get(G, "variables");
        json_set(data, "name", json_str(vars && vars->n ? sget(vars->items[0], "name") : ""));
    }
    if (d->custom) json_set(data, "name", json_str(methodName));
    json_set(n, "data", data);
    return n;
}

static void add_node(const FDef* d) {
    char id[16], mname[32] = "";
    next_id(id, sizeof id);
    if (d->custom)
        for (int k = 1; ; k++) {
            snprintf(mname, sizeof mname, "Metoda%d", k);
            int used = 0;
            for (int i = 0; i < nnodes(); i++) { const JVal* dd = json_get(node(i), "data"); if (dd && !strcmp(sget(dd, "name"), mname)) used = 1; }
            if (!used) break;
        }
    /* measure the new node, then find it a free place */
    json_push(nodes(), make_node(id, d, 0, 0, mname));
    relayout();
    int me = nnodes() - 1;
    float nw = L[me].w, nh = L[me].h, cx, cy, x, y;
    to_world(SW / 2, BAR_H + (SH - BAR_H) / 2, &cx, &cy);
    if (selKind == SEL_NODE && selIndex < me) {             /* to the right of the selected node: chains grow from left to right */
        cx = nx(selIndex) + L[selIndex].w + 42 + nw / 2;
        cy = ny(selIndex) + nh / 2;
    }
    json_remove_at(nodes(), me);
    relayout();
    free_spot(cx, cy, nw, nh, &x, &y);
    json_push(nodes(), make_node(id, d, x, y, mname));
    selKind = SEL_NODE; selIndex = nnodes() - 1;
    changed();
    center_on(selIndex);
}

/* ---------------------------------------------------------------- what is under a finger */
enum { H_BG, H_PORT, H_NODE, H_DATA, H_DEFAULT, H_WIRE };
typedef struct { int kind, node, dir, port, edge; } Hit;

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, t = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float qx = ax + t * dx - px, qy = ay + t * dy - py;
    return sqrtf(qx * qx + qy * qy);
}

static Hit hit_test(float x, float y) {
    Hit h = { H_BG, -1, 0, -1, -1 };
    float wx, wy;
    to_world(x, y, &wx, &wy);
    float pr = fmaxf(11, 14 / vs);                          /* at least 14 screen pixels for a stylus or finger */
    for (int i = nnodes() - 1; i >= 0; i--) {             /* ports first; the node drawn last wins */
        const FDef* d = L[i].d;
        if (!d) continue;
        for (int k = 0; k < d->nin; k++) {
            float px = nx(i), py = ny(i) + in_y(i, k);
            if ((wx - px) * (wx - px) + (wy - py) * (wy - py) <= pr * pr) { h.kind = H_PORT; h.node = i; h.dir = 0; h.port = k; return h; }
        }
        for (int k = 0; k < d->nout; k++) {
            float px = nx(i) + L[i].w, py = ny(i) + out_y(i, k);
            if ((wx - px) * (wx - px) + (wy - py) * (wy - py) <= pr * pr) { h.kind = H_PORT; h.node = i; h.dir = 1; h.port = k; return h; }
        }
    }
    for (int i = nnodes() - 1; i >= 0; i--) {
        float lx = wx - nx(i), ly = wy - ny(i), slack = 6 / vs;   /* a few pixels around the node still grab it */
        if (lx < -slack || ly < -slack || lx > L[i].w + slack || ly > L[i].h + slack) continue;
        h.node = i;
        h.kind = H_NODE;
        const FDef* d = L[i].d;
        if (!d || ly < HEAD) return h;
        if (L[i].dataRows && ly >= HEAD && ly < HEAD + ROW && lx > 5 && lx < L[i].w - 5) { h.kind = H_DATA; return h; }
        for (int k = 0; k < d->nin; k++) {
            float ry = in_y(i, k);
            if (!d->in[k].flow && !is_connected(i, 0, d->in[k].name) && ly >= ry - ROW / 2 && ly < ry + ROW / 2 && lx >= 5 && lx < L[i].w / 2 + 18) {
                h.kind = H_DEFAULT; h.port = k; return h;
            }
        }
        return h;
    }
    float tol = fmaxf(10, 10 / vs);
    for (int k = nedges() - 1; k >= 0; k--) {
        const JVal* e = edges()->items[k];
        int a = node_index(sget(e, "from")), b = node_index(sget(e, "to"));
        if (a < 0 || b < 0) continue;
        float ax, ay, bx, by;
        port_pos(a, 1, sget(e, "out"), &ax, &ay); port_pos(b, 0, sget(e, "in"), &bx, &by);
        float lx = ax, ly = ay;
        for (int s = 1; s <= 16; s++) {
            float qx, qy; bez(ax, ay, bx, by, s / 16.0f, &qx, &qy);
            if (seg_dist(wx, wy, lx, ly, qx, qy) <= tol) { h.kind = H_WIRE; h.edge = k; return h; }
            lx = qx; ly = qy;
        }
    }
    return h;
}

/* ---------------------------------------------------------------- palette: search and add a node */
static const FDef* palette(void) {
    char q[64] = "";
    int idx[60], n = fg_search(&CAT, q, idx, 60), cur = 0, top = 0;
    kbd_reset();
    kbd_set_enter_label("Dodaj");
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down(), rep = plat3ds_repeat();
        if (k & KEY_START) return NULL;
        int changedQ = 0;
        const char* t = kbd_update();
        if ((k & KEY_A) || (t && !strcmp(t, KBD_ENTER))) { if (n) return &CAT.d[idx[cur]]; }
        else if ((rep & KEY_B) || (t && !strcmp(t, KBD_BACKSPACE))) {
            int L2 = (int)strlen(q);
            if (!L2 && (k & KEY_B)) return NULL;              /* B with nothing typed: back */
            if (L2) { L2--; while (L2 > 0 && (q[L2] & 0xC0) == 0x80) L2--; q[L2] = 0; changedQ = 1; }
        } else if (t && strlen(q) + strlen(t) < sizeof q - 1) { strcat(q, t); changedQ = 1; }
        if (changedQ) { n = fg_search(&CAT, q, idx, 60); cur = top = 0; }
        if ((rep & KEY_DOWN) && cur < n - 1) cur++;
        if ((rep & KEY_UP) && cur > 0) cur--;
        for (int r = 0; r < 3 && r < n; r++) if (ui_tapped(0, 30 + r * 16, 320, 16)) return &CAT.d[idx[r]];
        if (cur < top) top = cur;
        if (cur >= top + 11) top = cur - 10;

        ui_frame_begin(0x101418, 0x0C0C0C);
        plat_text(8, 4, "Dodaj węzeł", 0.6f, 0x7FB2FF);
        if (!q[0]) plat_text(120, 8, "pusto = wszystkie; wpisz np. rect, held, jeśli, dodaj", 0.4f, 0x8090A0);
        for (int r = 0; r < 11 && top + r < n; r++) {
            const FDef* d = &CAT.d[idx[top + r]];
            float y = 26 + r * 15;
            if (top + r == cur) plat_rect(0, y - 1, 400, 15, 0x2D4A80);
            plat_text(8, y, d->title, 0.45f, head_color(d));
            plat_text(400 - 8 - plat3ds_text_width(d->category, 0.4f), y + 1, d->category, 0.4f, 0x8090A0);
        }
        if (!n) plat_text(8, 30, "Nic nie znaleziono.", 0.5f, 0xFF9080);
        else {
            const FDef* d = &CAT.d[idx[cur]];
            plat_rect(0, 192, 400, 48, 0x161B24);
            ui_wrap(6, 193, 388, d->sig && d->sig[0] ? d->sig : d->doc ? d->doc : "", 0.42f, 0xC0D0E8, 0, 1);
            if (d->sig && d->doc && d->doc[0]) ui_wrap(6, 208, 388, d->doc, 0.4f, 0xA0A8B0, 0, 2);
        }
        plat_screen(1);
        plat_rect(0, 0, 320, 28, 0x1C2230);
        plat_text(6, 5, q, 0.6f, 0xFFFFFF);
        if ((osGetTime() / 500) % 2 == 0) plat_rect(6 + plat3ds_text_width(q, 0.6f), 5, 1.5f, 18, 0xFFFFFF);
        for (int r = 0; r < 3 && r < n; r++) {
            plat_rect(0, 30 + r * 16, 320, 15, r == cur ? 0x2D4A80 : 0x1A2030);
            plat_text(6, 31 + r * 16, CAT.d[idx[r]].title, 0.45f, head_color(&CAT.d[idx[r]]));
        }
        kbd_draw();
        ui_frame_end();
    }
    return NULL;
}

/* ---------------------------------------------------------------- "Więcej": variables, class, notes, save */
static int ask_var(JVal* v) {
    char name[64], type[64], init[128], b[64];
    snprintf(name, sizeof name, "%s", sget(v, "name"));
    snprintf(type, sizeof type, "%s", sget(v, "type")[0] ? sget(v, "type") : "float");
    const JVal* iv = json_get(v, "init");
    const char* it = iv ? json_text(iv, b, sizeof b) : NULL;
    snprintf(init, sizeof init, "%s", it ? it : "");
    for (;;) {
        if (!ui_prompt("Zmienna: nazwa", "Np. x, hp, wynik. Litery, cyfry i _.", name, sizeof name)) return 0;
        trim(name);
        if (!is_ident(name)) { ui_ask("Zła nazwa", "Nazwa zmiennej: litery, cyfry i _, nie może zaczynać się od cyfry.", "OK", "Popraw", 0); continue; }
        int dup = 0;
        JVal* vars = json_get(G, "variables");
        for (int k = 0; k < vars->n; k++) if (vars->items[k] != v && !strcmp(sget(vars->items[k], "name"), name)) dup = 1;
        if (dup) { ui_ask("Już jest", "Zmienna o tej nazwie już istnieje.", "OK", "Popraw", 0); continue; }
        break;
    }
    if (!ui_prompt("Zmienna: typ", "int, float, bool, string, List<int> ...", type, sizeof type)) return 0;
    if (!ui_prompt("Zmienna: wartość początkowa", "Może być pusta (wtedy 0, false albo null).", init, sizeof init)) return 0;
    trim(type); trim(init);
    json_set(v, "name", json_str(name)); json_set(v, "type", json_str(type[0] ? type : "float")); json_set(v, "init", json_str(init));
    return 1;
}

static void variables_screen(void) {
    int sel = 0;
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down();
        JVal* vars = json_get(G, "variables");
        int n = vars ? vars->n : 0;
        if ((k & (KEY_B | KEY_START)) || ui_tapped(240, 200, 80, 40)) return;
        if ((k & KEY_DOWN) && sel < n - 1) sel++;
        if ((k & KEY_UP) && sel > 0) sel--;
        for (int i = 0; i < n && i < 7; i++) if (ui_tapped(0, 4 + i * 26, 320, 26)) sel = i;
        if ((k & KEY_Y) || ui_tapped(0, 200, 80, 40)) {
            JVal* v = json_new(JS_OBJ);
            json_set(v, "name", json_str("")); json_set(v, "type", json_str("float")); json_set(v, "init", json_str("0"));
            if (ask_var(v)) { json_push(vars, v); sel = vars->n - 1; changed(); } else json_free(v);
            kbd_reset();
            continue;
        }
        if (n && ((k & KEY_A) || ui_tapped(80, 200, 80, 40))) { if (ask_var(vars->items[sel])) changed(); kbd_reset(); continue; }
        if (n && ((k & KEY_X) || ui_tapped(160, 200, 80, 40))) { json_remove_at(vars, sel); if (sel >= vars->n && sel) sel--; changed(); continue; }

        ui_frame_begin(0x101418, 0x0C0C0C);
        plat_text(8, 6, "Zmienne grafu", 0.7f, 0x7FB2FF);
        ui_wrap(8, 36, 384, "To pola klasy grafu (static, albo zwykłe w klasie z obiektami). Węzły „Pobierz zmienną” i „Ustaw zmienną” "
                "odwołują się do nich po nazwie.\n\nY: dodaj   A: zmień   X: usuń   B: wróć", 0.5f, 0xA0A8B0, 0, 10);
        plat_screen(1);
        for (int i = 0; i < n && i < 7; i++) {
            const JVal* v = vars->items[i];
            char row[200], b[64];
            const JVal* iv = json_get(v, "init");
            const char* init = iv ? json_text(iv, b, sizeof b) : NULL;
            snprintf(row, sizeof row, "%s %s%s%s", sget(v, "type"), sget(v, "name"), init && init[0] ? " = " : "", init && init[0] ? init : "");
            if (i == sel) plat_rect(0, 4 + i * 26, 320, 26, 0x2D4A80);
            plat_text(10, 9 + i * 26, row, 0.5f, 0xFFFFFF);
        }
        if (!n) plat_text(10, 60, "Brak zmiennych. Dodaj pierwszą (Y).", 0.5f, 0xA0A8B0);
        ui_button(0, 200, 80, 40, "+ Zmienna", 0x2F6B3A);
        ui_button(80, 200, 80, 40, "Zmień", 0x34405A);
        ui_button(160, 200, 80, 40, "Usuń", 0x6B2F2F);
        ui_button(240, 200, 80, 40, "Wróć", 0x34405A);
        ui_frame_end();
    }
}

static void next_issue(void) {
    int n = ISS.nerrors + ISS.nwarnings;
    if (!n) { note(0x60E060, "Brak uwag do grafu."); return; }
    static int at = -1;
    at = (at + 1) % n;
    const FMsg* m = at < ISS.nerrors ? &ISS.errors[at] : &ISS.warnings[at - ISS.nerrors];
    note(at < ISS.nerrors ? 0xFF6060 : 0xE0C080, m->message);
    int i = m->node ? node_index(m->node) : -1;
    if (i >= 0) { selKind = SEL_NODE; selIndex = i; center_on(i); }
}

static void more_menu(void) {
    static const char* ITEMS[] = { "Zmienne grafu", "Dopasuj widok", "Następna uwaga", "Nazwa klasy", "Klasa z obiektami", "Zapisz" };
    int sel = 0;
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down();
        if (k & (KEY_B | KEY_START)) return;
        if ((k & KEY_DOWN) && sel < 5) sel++;
        if ((k & KEY_UP) && sel > 0) sel--;
        int pick = (k & KEY_A) ? sel : -1;
        for (int i = 0; i < 6; i++) if (ui_tapped(0, 4 + i * 36, 320, 34)) pick = i;
        if (pick == 0) { variables_screen(); return; }
        if (pick == 1) { fit(); return; }
        if (pick == 2) { next_issue(); return; }
        if (pick == 3) {
            char name[64];
            snprintf(name, sizeof name, "%s", sget(G, "className")[0] ? sget(G, "className") : "Game");
            if (ui_prompt("Nazwa klasy", "Klasa, którą zrobi ten graf, np. Gracz. Z kodu: Gracz.x, Gracz.Metoda().", name, sizeof name)) {
                trim(name);
                if (is_ident(name)) { json_set(G, "className", json_str(name)); changed(); }
                else note(0xFF6060, "Nazwa klasy: litery, cyfry i _.");
            }
            kbd_reset();
            return;
        }
        if (pick == 4) {
            int on = !json_truthy(json_get(G, "instance"));
            json_set(G, "instance", json_new(on ? JS_TRUE : JS_FALSE));
            note(0x60E060, on ? "Klasa z obiektami: var o = new Klasa(); o.Metoda();" : "Klasa statyczna: Klasa.Metoda()");
            changed();
            return;
        }
        if (pick == 5) { if (H->save()) note(0x60E060, "Zapisano"); return; }

        ui_frame_begin(0x101418, 0x0C0C0C);
        draw_top();
        plat_screen(1);
        for (int i = 0; i < 6; i++) {
            char label[80];
            if (i == 4) snprintf(label, sizeof label, "Klasa z obiektami: %s", json_truthy(json_get(G, "instance")) ? "tak" : "nie");
            else if (i == 3) snprintf(label, sizeof label, "Nazwa klasy: %.40s", sget(G, "className")[0] ? sget(G, "className") : "Game");
            else snprintf(label, sizeof label, "%s", ITEMS[i]);
            ui_button(0, 4 + i * 36, 320, 34, label, i == sel ? 0x2D4A80 : 0x34405A);
        }
        ui_frame_end();
    }
}

/* ---------------------------------------------------------------- the loop */
void bolt_mode(const BoltHost* h) {
    H = h;
    memset(&ISS, 0, sizeof ISS);
    selKind = SEL_NONE; pendNode = -1; tempOn = 0; g_note[0] = 0;
    build_catalog();
    if (!load_from_text()) { H->status(0xFF6060, "Ten plik nie ma grafu (linii // @graph)"); fg_catalog_free(&CAT); fg_catalog_free(&USER); return; }
    fit();
    int touching = 0, moved = 0, sx0 = 0, sy0 = 0, lx = 0, ly = 0, lost = 0;
    Hit g = { H_BG, -1, 0, -1, -1 };
    float x0 = 0, y0 = 0;
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down(), held = plat3ds_held();
        if (k & (KEY_START | KEY_B)) break;
        if (ui_tapped(0, 0, 64, BAR_H)) break;
        if ((k & KEY_A) || ui_tapped(64, 0, 64, BAR_H)) { const FDef* d = palette(); kbd_reset(); if (d) add_node(d); continue; }
        if ((k & KEY_X) || ui_tapped(128, 0, 64, BAR_H)) remove_selection();
        if (ui_tapped(192, 0, 64, BAR_H)) { H->undo(); if (!load_from_text()) break; }
        if (ui_tapped(256, 0, 64, BAR_H)) { more_menu(); kbd_reset(); continue; }
        if (k & KEY_Y) fit();
        if (k & KEY_SELECT) { if (H->save()) note(0x60E060, "Zapisano"); }
        if (held & KEY_R) zoom_at(SW / 2, BAR_H + (SH - BAR_H) / 2, 1.03f);
        if (held & KEY_L) zoom_at(SW / 2, BAR_H + (SH - BAR_H) / 2, 1 / 1.03f);
        circlePosition cp; hidCircleRead(&cp);
        if (abs(cp.dx) > 24) vx -= (cp.dx - (cp.dx > 0 ? 24 : -24)) / 132.0f * 6;
        if (abs(cp.dy) > 24) vy += (cp.dy - (cp.dy > 0 ? 24 : -24)) / 132.0f * 6;

        int px, py;
        plat3ds_touch(&px, &py);
        if ((k & KEY_TOUCH) && py >= BAR_H) {
            if (touching && lost) { lost = 0; lx = px; ly = py; goto drawn; }   /* contact back within the grace: the same gesture */
            touching = 1; moved = 0; lost = 0; sx0 = lx = px; sy0 = ly = py;
            g = hit_test(px, py);
            if (g.kind == H_NODE) { selKind = SEL_NODE; selIndex = g.node; x0 = nx(g.node); y0 = ny(g.node); }
        } else if (touching && !(held & KEY_TOUCH) && ++lost < 5) {
            /* the resistive screen loses contact for a frame or two while dragging: keep the gesture a moment */
        } else if (touching && (held & KEY_TOUCH)) {
            lost = 0;
            if (!moved && abs(px - sx0) + abs(py - sy0) > 6) moved = 1;
            if (moved) {
                if (g.kind == H_BG || g.kind == H_WIRE || g.kind == H_DATA || g.kind == H_DEFAULT) { vx += px - lx; vy += py - ly; }
                else if (g.kind == H_NODE) {
                    set_pos(g.node, x0 + (px - sx0) / vs, y0 + (py - sy0) / vs, 0);
                } else if (g.kind == H_PORT) {
                    float wx, wy, ax, ay;
                    to_world(px, py, &wx, &wy);
                    const FDef* d = L[g.node].d;
                    port_pos(g.node, g.dir, g.dir ? d->out[g.port].name : d->in[g.port].name, &ax, &ay);
                    tempFlow = g.dir ? d->out[g.port].flow : d->in[g.port].flow;
                    if (g.dir) { tx0 = ax; ty0 = ay; tx1 = wx; ty1 = wy; } else { tx0 = wx; ty0 = wy; tx1 = ax; ty1 = ay; }
                    tempOn = 1;
                }
            }
            lx = px; ly = py;
        } else if (touching) {                                     /* released (at lx, ly) */
            touching = 0;
            tempOn = 0;
            if (g.kind == H_BG) { if (!moved) { selKind = SEL_NONE; pendNode = -1; } }
            else if (g.kind == H_NODE) {
                if (moved) {
                    set_pos(g.node, nx(g.node), ny(g.node), 1);
                    changed();
                }
            } else if (g.kind == H_PORT) {
                if (moved) {
                    Hit t = hit_test(lx, ly);
                    if (t.kind == H_PORT && !(t.node == g.node && t.dir == g.dir && t.port == g.port)) {
                        pendNode = -1;
                        if (wire_ports(g.node, g.dir, g.port, t.node, t.dir, t.port)) changed();
                    } else if (t.kind != H_PORT && g.dir == 0 && is_connected(g.node, 0, L[g.node].d->in[g.port].name)) {
                        unwire_input(g.node, g.port);              /* dragged off a wired input: unwired */
                        note(0xC0C8D0, "Odpięto połączenie (Cofnij przywraca).");
                        changed();
                    }
                } else if (pendNode < 0) { pendNode = g.node; pendDir = g.dir; pendPort = g.port; }
                else if (pendNode == g.node && pendDir == g.dir && pendPort == g.port) pendNode = -1;
                else {
                    int a = pendNode, ad = pendDir, ap = pendPort;
                    pendNode = -1;
                    if (wire_ports(a, ad, ap, g.node, g.dir, g.port)) changed();
                }
            } else if (g.kind == H_DATA) { if (!moved) { selKind = SEL_NODE; selIndex = g.node; edit_data(g.node); } }
            else if (g.kind == H_DEFAULT) { if (!moved) { selKind = SEL_NODE; selIndex = g.node; edit_default(g.node, g.port); } }
            else if (g.kind == H_WIRE) { if (!moved) { selKind = SEL_EDGE; selIndex = g.edge; } }
        }
    drawn:
        frame();
    }
    free_layout();
    if (TB) { C2D_TextBufDelete(TB); TB = NULL; }
    json_free(G); G = NULL;
    fg_result_free(&ISS);
    fg_catalog_free(&CAT); fg_catalog_free(&USER);
}

char* bolt_new_graph_file(const char* name) {
    JVal* g = json_new(JS_OBJ);
    json_set(g, "className", json_str(name));
    json_set(g, "variables", json_new(JS_ARR));
    JVal* ns = json_new(JS_ARR);
    const char* types[] = { "event.Update", "event.Draw" };
    for (int i = 0; i < 2; i++) {
        JVal* n = json_new(JS_OBJ);
        char id[8]; snprintf(id, sizeof id, "n%d", i + 1);
        json_set(n, "id", json_str(id)); json_set(n, "type", json_str(types[i]));
        json_set(n, "x", json_num(40)); json_set(n, "y", json_num(40 + i * 200));
        json_set(n, "data", json_new(JS_OBJ));
        json_push(ns, n);
    }
    json_set(g, "nodes", ns);
    json_set(g, "edges", json_new(JS_ARR));
    FCatalog c;
    int nh; const FHelpEntry* he = (const FHelpEntry*)help_api_entries(&nh);
    fg_catalog_build(&c, he, nh, NULL);
    FGenResult r;
    fg_generate(g, &c, 1, &r);
    char* code = r.code; r.code = NULL;
    fg_result_free(&r);
    fg_catalog_free(&c);
    json_free(g);
    return code;
}
